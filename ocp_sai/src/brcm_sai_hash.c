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
#                                Hash functions                                #
################################################################################
*/
/**
 * Routine Description:
 *    @brief Create hash
 *
 * Arguments:
 *    @param[out] hash_id - hash id
 *    @param[in] attr_count - number of attributes
 *    @param[in] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 *
 */
STATIC sai_status_t
brcm_sai_create_hash(_Out_ sai_object_id_t* hash_id,
                     _In_ sai_object_id_t switch_id,
                     _In_ uint32_t attr_count,
                     _In_ const sai_attribute_t *attr_list)
{
    int udf_id;
    int i, h, g, gid;
    bool hash = FALSE;
    sai_object_id_t *udf = NULL;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_list_key_t list_key;
    _brcm_sai_list_data_t list_data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HASH);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(hash_id);
    
    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_HASH_INFO, 1,
                                              _BRCM_SAI_MAX_HASH, &h);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR, "Unexpected hash resource issue.\n");
        return rv;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HASH_INFO,
                                    &h, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "hash data get", rv);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_HASH_ATTR_NATIVE_HASH_FIELD_LIST:
                if (0 == attr_list[i].value.s32list.count)
                {
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR, "No hash fields provided.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                data.hash.hash_fields_count = attr_list[i].value.s32list.count;
                for (g=0; g<attr_list[i].value.s32list.count; g++)
                {
                    data.hash.hash_fields[g] = attr_list[i].value.s32list.list[g];
                }
                hash = TRUE;
                break;
            case SAI_HASH_ATTR_UDF_GROUP_LIST:
            {
                _brcm_sai_indexed_data_t _data;

                if (0 == BRCM_SAI_ATTR_LIST_OBJ_COUNT(i))
                {
                    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR, "No udf groups provided.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                for (g=0; g<BRCM_SAI_ATTR_LIST_OBJ_COUNT(i); g++)
                {
                    gid = BRCM_SAI_ATTR_LIST_OBJ_LIST_VAL(int, i, g);
                    if (!gid)
                    {
                        return SAI_STATUS_INVALID_ATTR_VALUE_0;
                    }
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                                    &gid, &_data);
                    BRCM_SAI_RV_CHK(SAI_API_HASH, "udf group data get", rv);
                    rv = _brcm_sai_udfg_get_next_udf(gid, &udf);
                    BRCM_SAI_RV_CHK(SAI_API_HASH, "udf group get next udf obj", rv);
                    if (IS_NULL(udf))
                    {
                        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_DEBUG, "No udf in udfg %d\n", gid);
                    }
                    else
                    {
                        while (udf)
                        {
                            list_key.obj_id = *udf;
                            rv = _brcm_sai_list_get(_BRCM_SAI_LIST_UDFG_UDF_MAP, 
                                                    (_brcm_sai_list_data_t *)_data.udfg.refs, 
                                                    &list_key, &list_data);
                            if (SAI_STATUS_ERROR(rv) && (SAI_STATUS_ITEM_NOT_FOUND != rv))
                            {
                                BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR, "Error in udf list get.");
                                return rv;
                            }
                            udf_id = BRCM_SAI_GET_OBJ_VAL(int, *udf);
                            rv = _brcm_udf_hash_config_add(udf_id, _BRCM_SAI_UDF_HASH_MASK_SIZE,
                                                           list_data.udf_obj->hash_mask);
                            BRCM_SAI_RV_CHK(SAI_API_HASH, "udf hash config add", rv);
                            rv = _brcm_sai_udfg_get_next_udf(gid, &udf);
                            BRCM_SAI_RV_CHK(SAI_API_HASH, "udf group get next udf obj", rv);
                        }
                    }
                    _data.udfg.hid = h;
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_UDFG_INFO,
                                                    &gid, &_data);
                    BRCM_SAI_RV_CHK(SAI_API_HASH, "udf group data set", rv);
                }
                hash = TRUE;
                break;
            }
            default:
                break;
        }
    }
    if (FALSE == hash)
    {
        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR,
                          "No hash attribute provided.\n"); 
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_HASH_INFO, h);
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_DEBUG, "Using hash id: %d\n", h);
    *hash_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HASH, h);
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_bump(_BRCM_SAI_HASH_COUNT, INC))
    {
        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR,
                          "Error incrementing hash count global data.\n");
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_HASH_INFO, h);
        return SAI_STATUS_FAILURE;
    }
    data.hash.idx = h;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HASH_INFO,
                                    &h, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "hash data set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HASH);

    return rv;
}

/**
 * Routine Description:
 *    @brief Remove hash
 *
 * Arguments:
 *    @param[in] hash_id - hash id
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_hash(_In_ sai_object_id_t hash_id)
{
    int h;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HASH);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(hash_id, SAI_OBJECT_TYPE_HASH))
    {
        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR, 
                          "Invalid object type 0x%016lx passed\n", hash_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    h = BRCM_SAI_GET_OBJ_VAL(int, hash_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HASH_INFO,
                                    &h, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "hash data get", rv);
    if (FALSE == data.hash.valid)
    {
        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR, 
                          "Invalid object id 0x%016lx passed\n", hash_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_HASH_COUNT, DEC);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "global count dec", rv);
    (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_HASH_INFO, h);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HASH);

    return rv;
}

/**
 * Routine Description:
 *    @brief Set hash attribute
 *
 * Arguments:
 *    @param[in] hash_id - hash id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t 
brcm_sai_set_hash_attribute(_In_ sai_object_id_t hash_id,
                            _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HASH);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HASH);

    return rv;
}

/**
 * Routine Description:
 *    @brief Get hash attribute value
 *
 * Arguments:
 *    @param[in] hash_id - hash id
 *    @param[in] attr_count - number of attributes
 *    @param[inout] attrs - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_hash_attribute(_In_ sai_object_id_t hash_id,
                            _In_ uint32_t attr_count,
                            _Inout_ sai_attribute_t *attr_list)
{
    int i, c, hidx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HASH);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(hash_id, SAI_OBJECT_TYPE_HASH))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    hidx = BRCM_SAI_GET_OBJ_VAL(int, hash_id);
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG,
                     "Get hash: %d\n", hidx);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HASH_INFO,
                                    &hidx, &data);
    BRCM_SAI_RV_CHK(SAI_API_UDF, "hash data get", rv);
    if (!data.hash.valid)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_HASH_ATTR_NATIVE_HASH_FIELD_LIST:
            {
                int limit = data.hash.hash_fields_count;

                if (attr_list[i].value.s32list.count < limit)
                {
                    limit = attr_list[i].value.s32list.count;
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                for (c=0; c<limit; c++)
                {
                    attr_list[i].value.s32list.list[c] = data.hash.hash_fields[c];
                }
                attr_list[i].value.s32list.count = data.hash.hash_fields_count;
                break;
            }
            case SAI_HASH_ATTR_UDF_GROUP_LIST:
            {
                sai_status_t rv1;
                int c = 0, g, act = 0;
                _brcm_sai_indexed_data_t _data;

                for (g=1; g<=_BRCM_SAI_MAX_UDF_GROUPS; g++)
                {
                    rv1 = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_UDFG_INFO,
                                                    &g, &_data);
                    BRCM_SAI_RV_CHK(SAI_API_HASH, "udf group data get", rv1);
                    if (_data.udfg.valid && hidx == _data.udfg.hid)
                    {
                        if (c < BRCM_SAI_ATTR_LIST_OBJ_COUNT(i))
                        {
                            BRCM_SAI_ATTR_LIST_OBJ_LIST(i, c) =
                                BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_UDF_GROUP, g);
                            c++;
                        }
                        act++;
                    }
                }
                if (act > c)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = act;
                break;
            }
            default:
                BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_ERROR,
                                  "Unknown hash attribute %d passed\n",
                                  attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_INFO,
                              "Error processing hash attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HASH);

    return rv;
}

/*
################################################################################
#                              Internal functions                              #
################################################################################
*/
STATIC sai_uint32_t
_brcm_sai_hash_count_get()
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_get(_BRCM_SAI_HASH_COUNT, &data))
    {
        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_CRITICAL,
                          "Error getting hash count data.\n");
        return SAI_STATUS_SUCCESS;
    }
    return data.u32;
}

/* Routine to allocate hash state */
sai_status_t
_brcm_sai_alloc_hash()
{
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_HASH_INFO,
                                    _BRCM_SAI_MAX_HASH+1))
    {
        BRCM_SAI_LOG_HASH(SAI_LOG_LEVEL_CRITICAL,
                          "Error initializing hash state !!\n");
        return SAI_STATUS_FAILURE;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_hash()
{
    sai_status_t rv;
    
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_HASH_INFO, 
                                      1, _BRCM_SAI_MAX_HASH,
                                      _brcm_sai_hash_count_get());
    BRCM_SAI_RV_LVL_CHK(SAI_API_HASH, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hash state", rv);
    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_hash_api_t hash_api = {
    brcm_sai_create_hash,
    brcm_sai_remove_hash,
    brcm_sai_set_hash_attribute,
    brcm_sai_get_hash_attribute
};
