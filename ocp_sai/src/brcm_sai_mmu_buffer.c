/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

extern uint32 ingress_port_flex_counter_id_map[_BRCM_SAI_MAX_PORTS];
/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t
_brcm_sai_ingress_shared_limit_set(int pool_idx, int bp_size);


/*
*Routine Description :
*   Set ingress priority group attribute
* Arguments :
*   [in] ingress_pg_id ingress priority group id
*   [in] attr - attribute
*
* Return Values :
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_ingress_priority_group_attribute(_In_ sai_object_id_t ingress_pg_id,
                                              _In_ const sai_attribute_t *attr)
{
    int idx[2], index = 0;
    uint32_t val;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    int ie, port, pg, pool_idx, bp = -1;
    _brcm_sai_buf_pool_t *buf_pool;
    _brcm_sai_buf_profile_t *buf_profile;
    sai_buffer_pool_threshold_mode_t mode;
    bcm_gport_t gport;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "NULL params passed.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (SAI_INGRESS_PRIORITY_GROUP_ATTR_BUFFER_PROFILE != attr->id)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "Buffer profile not passed.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    
    pg = (BRCM_SAI_GET_OBJ_VAL(int, ingress_pg_id)) - 1; /* rebase to 0 */
    port = BRCM_SAI_GET_OBJ_MAP(ingress_pg_id);
    
    if (SAI_NULL_OBJECT_ID == attr->value.oid)
    {       
        goto _ing_pg_exit;
    }
    if (SAI_OBJECT_TYPE_NULL == BRCM_SAI_GET_OBJ_TYPE(attr->value.oid))
    {
        /* Unset this pg in buf_profile? */
        goto _ing_pg_exit;
    }
    bp = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
    if (-1 == bp)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "Buffer profile object invalid.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_PROFILES,
                                    &bp, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff profile data get", rv);
    if (FALSE == data.buf_prof->valid)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "Buffer profile not active.\n");
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
  
    buf_profile = data.buf_prof;
    pool_idx = BRCM_SAI_GET_OBJ_VAL(int, buf_profile->pool_id) - 1; /* rebase to 0 */
    ie = BRCM_SAI_GET_OBJ_SUB_TYPE(buf_profile->pool_id);
    mode = buf_profile->mode;
    idx[0] = ie;
    idx[1] = pool_idx;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_POOLS,
                                    idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff pool data get", rv);
    if (FALSE == data.buf_pool->valid)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "Associated buffer pool is not valid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (SAI_BUFFER_POOL_TYPE_INGRESS != data.buf_pool->type)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "Buffer pool is not of type INGRESS.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    rv = _brcm_sai_cosq_config(port, pg, 1, pool_idx);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "cosq config 1", rv);
    buf_pool = data.buf_pool;
    if (-1 == mode)
    {
        mode = buf_pool->mode;
    }
    rv = driverPGAttributeSet(port, pg, 1, (SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC == mode) ?
                              TRUE : FALSE);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "PG Shared Dynamic Enable Set", rv);
    val = buf_profile->shared_val;
    rv = driverPGAttributeSet(port, pg, (SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC == mode) ?
                              2 : 3, val);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "PG Shared Limit Set", rv);
    if (buf_pool->xoff_thresh == 0)
    {
        rv = driverPGAttributeSet(port, pg, 4, buf_profile->size - buf_profile->xoff_thresh);
    }
    else
    {
        rv = driverPGAttributeSet(port, pg, 4, buf_profile->size);
    }
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "PG Min Limit Set", rv);
    rv = driverPGAttributeSet(port, pg, 5, buf_profile->xoff_thresh);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "PG Hdrm Limit Set", rv);
    rv = driverPGAttributeSet(port, pg, 6, buf_profile->xon_thresh);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "PG Reset Floor Set", rv);
    rv = driverPGAttributeSet(port, pg, 7, buf_profile->xon_offset_thresh);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "PG Reset Offset Set", rv);
    /* total pvt space = pg_min * num_lossless */
    rv = _brcm_sai_ingress_shared_limit_set(pool_idx, buf_profile->size);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "ingress shared limit set", rv);
    /* Make port config in-effective */
    rv = _brcm_sai_port_ingress_buffer_config_set(port, pg, pool_idx, -1, 0, -1);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "port ingress buffer config set", rv);

    /* Set this PG in buf_profile */
    buf_profile->pg_data[port] |= (0x1 << pg);
    
    rv = bcm_port_gport_get(0, port, &gport);
    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "gport get", rv);
    rv = driverMMUInstMapIndexGet(0, gport, bcmBstStatIdIngPool, &index);
    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "mmu instance map index get", rv);

    buf_pool->bst_rev_gport_maps[index] = gport;
    buf_pool->bst_rev_cos_maps[index] = pg;

_ing_pg_exit:
    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*    Get ingress priority group attributes
* Arguments:
*   [in] ingress_pg_id – ingress priority group id
*   [in] attr_count - number of attributes
*   [inout] attr_list - array of attributes
*
* Return Values:
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_ingress_priority_group_attribute(_In_ sai_object_id_t ingress_pg_id,
                                              _In_ uint32_t attr_count,
                                              _Inout_ sai_attribute_t *attr_list)
{
    int i;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(ingress_pg_id, 
                                      SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP);

    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_INGRESS_PRIORITY_GROUP_ATTR_PORT:
                BRCM_SAI_ATTR_LIST_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT,
                                        BRCM_SAI_GET_OBJ_MAP(ingress_pg_id));
                break;
            case SAI_INGRESS_PRIORITY_GROUP_ATTR_INDEX:
                attr_list[i].value.u8 = (BRCM_SAI_GET_OBJ_VAL(int, ingress_pg_id)) - 1;
                break;
            default:
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                        "Unknown ing prio group attribute %d passed\n",
                                        attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error processing ing prio group attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/**
* @brief   Get ingress priority group statistics counters.
*
* @param[in] ingress_pg_id ingress priority group id
* @param[in] counter_ids specifies the array of counter ids
* @param[in] number_of_counters number of counters in the array
* @param[out] counters array of resulting counter values.
*
* @return SAI_STATUS_SUCCESS on success
*         Failure status code on error
*/

STATIC sai_status_t
brcm_sai_get_ingress_priority_group_stats(_In_ sai_object_id_t ingress_pg_id,
                                          _In_ uint32_t number_of_counters,
                                          _In_ const sai_ingress_priority_group_stat_t *counter_ids,
                                          _Out_ uint64_t* counters)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(ingress_pg_id, SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    int i;
    for (i = 0; i < number_of_counters; i++)
    {
        switch(counter_ids[i])
        {
            case SAI_INGRESS_PRIORITY_GROUP_STAT_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_CURR_OCCUPANCY_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_SHARED_CURR_OCCUPANCY_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_XOFF_ROOM_CURR_OCCUPANCY_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_CUSTOM_RANGE_BASE:
                rv = SAI_STATUS_NOT_IMPLEMENTED;
                break;
            case SAI_INGRESS_PRIORITY_GROUP_STAT_WATERMARK_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_SHARED_WATERMARK_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_XOFF_ROOM_WATERMARK_BYTES:
            {
                int port = BRCM_SAI_GET_OBJ_MAP(ingress_pg_id);
                uint32 pg = (BRCM_SAI_GET_OBJ_VAL(uint32, ingress_pg_id)) - 1;
                bcm_gport_t gport;


                if (_BRCM_SAI_IS_CPU_PORT(port))
                {
                    gport = port;
                }
                else
                {
                    rv = bcm_port_gport_get(0, port, &gport);
                    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "gport get", rv);
                }
                if (-1 == gport)
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                    return SAI_STATUS_FAILURE;
                }

                rv = _brcm_sai_ingress_pg_stat_get(port, pg, gport, counter_ids[i], &counters[i]);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cosq stat get", rv);

                break;
            }
            case SAI_INGRESS_PRIORITY_GROUP_STAT_DROPPED_PACKETS:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_PACKETS:
            {
                int port = BRCM_SAI_GET_OBJ_MAP(ingress_pg_id);
                uint32 pg = (BRCM_SAI_GET_OBJ_VAL(uint32, ingress_pg_id)) - 1;
                bcm_stat_value_t stat;
                if (counter_ids[i] == SAI_INGRESS_PRIORITY_GROUP_STAT_PACKETS)
                {
                    pg = pg + _BRCM_SAI_MAX_TC;
                }

                rv = bcm_stat_flex_counter_get(0, ingress_port_flex_counter_id_map[port],
                        bcmStatFlexStatPackets, 1, &pg, &stat);
                counters[i] = stat.packets64;
                break;
            }
            default:
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                        "Unknown ing prio group static %d passed\n",
                                        counter_ids[i]);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }

        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error processing ing prio group stat\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/**
* @brief   Clear ingress priority group statistics counters.
*
 * @param[in] ingress_pg_id Ingress priority group id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
*
* @return SAI_STATUS_SUCCESS on success
*         Failure status code on error
*/

STATIC sai_status_t
brcm_sai_clear_ingress_priority_group_stats(_In_ sai_object_id_t ingress_pg_id,
                                            _In_ uint32_t number_of_counters,
                                            _In_ const sai_ingress_priority_group_stat_t *counter_ids)
{
    int i;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_SET_STATS_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(ingress_pg_id, SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    for (i = 0; i < number_of_counters; i++)
    {
        switch(counter_ids[i])
        {
            case SAI_INGRESS_PRIORITY_GROUP_STAT_WATERMARK_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_SHARED_WATERMARK_BYTES:
            case SAI_INGRESS_PRIORITY_GROUP_STAT_XOFF_ROOM_WATERMARK_BYTES:
            {
                int port = BRCM_SAI_GET_OBJ_MAP(ingress_pg_id);
                uint32 pg = (BRCM_SAI_GET_OBJ_VAL(uint32, ingress_pg_id)) - 1;
                bcm_gport_t gport;


                if (_BRCM_SAI_IS_CPU_PORT(port))
                {
                    gport = port;
                }
                else
                {
                    rv = bcm_port_gport_get(0, port, &gport);
                    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "gport get", rv);
                }
                if (-1 == gport)
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                    return SAI_STATUS_FAILURE;
                }

                rv = _brcm_sai_ingress_pg_stat_set(port, pg, gport, counter_ids[i], 0);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cosq stat set", rv);

                break;
            }
            default:
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                        "Unknown ing prio group statistic %d passed\n",
                                        counter_ids[i]);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }

        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error processing ing prio group stat\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}
    
/*
* Routine Description:
*    Create buffer pool
*
* Arguments:
*   [out] pool_id  -pool id
*   [in] attr_count - number of attributes
*   [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_buffer_pool(_Out_ sai_object_id_t* pool_id,
                            _In_ sai_object_id_t switch_id,
                            _In_ uint32_t attr_count,
                            _In_ const sai_attribute_t *attr_list)
{
    int count, idx[2];
    int i, ie = -1, index = -1;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_buf_pool_t buf_pool;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(pool_id);
    sal_memset(&buf_pool, '\0', sizeof(buf_pool));
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
          case SAI_BUFFER_POOL_ATTR_TYPE:
            if (SAI_BUFFER_POOL_TYPE_EGRESS < attr_list[i].value.s32)
            {
              return SAI_STATUS_INVALID_PARAMETER;
            }
            ie = SAI_BUFFER_POOL_TYPE_INGRESS == attr_list[i].value.s32 ? 0 : 1;
            buf_pool.type = attr_list[i].value.s32;
            break;   
          case SAI_BUFFER_POOL_ATTR_SHARED_SIZE:
            return SAI_STATUS_INVALID_PARAMETER;         
            break;
          case SAI_BUFFER_POOL_ATTR_SIZE:
            buf_pool.size = attr_list[i].value.u32;
            break;
          case SAI_BUFFER_POOL_ATTR_XOFF_SIZE:
              if (!DEV_IS_THX() && !DEV_IS_TD3())
              {
                  return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
              }
            buf_pool.xoff_thresh = attr_list[i].value.u32;
            break;
          case SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE:
            if (SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC < attr_list[i].value.s32)
            {
              return SAI_STATUS_INVALID_PARAMETER;
            }
            buf_pool.mode = attr_list[i].value.s32;
            break;
            /* Custom pool id used only with BRCM */
          case SAI_BUFFER_POOL_ATTR_BRCM_CUSTOM_POOL_ID:
            index = attr_list[i].value.u8;                
            if (index < 0 || index > _BRCM_SAI_INDEXED_BUF_POOLS)
            {
              return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
          default:
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Unknown mmu buffer pool attribute %d passed\n",
                                    attr_list[i].id);
            rv = SAI_STATUS_INVALID_PARAMETER;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error processing mmu buffer pool attributes\n");
            return rv;
        }
    }
    if (-1 == ie)
    {
      BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "Buffer pool type not provided.\n");
      return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }  
    if (index == -1)
    {
      /* Reserve an unused pool id - this will skip over CPU pool*/
      rv = _brcm_sai_indexed_data_reserve_index2(_BRCM_SAI_INDEXED_BUF_POOLS, 0,
                                                 _BRCM_SAI_MAX_BUF_POOLS, ie, &index);
      BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff pool index reserve", rv);
    }   
    if (SAI_BUFFER_POOL_TYPE_INGRESS == buf_pool.type)
    {
        if (DEV_IS_THX())
        {
            rv = driverSPHeadroomSet(index, buf_pool.xoff_thresh);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "Pool Headroom Set", rv);
        }
        if (DEV_IS_THX() || DEV_IS_TD2())
        {
            rv = driverSPLimitSet(index, buf_pool.size);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "SP Limit Set", rv);
        }
    }
    if (SAI_BUFFER_POOL_TYPE_EGRESS == buf_pool.type)
    {
        rv = driverDBLimitSet(index, buf_pool.size);
        BRCM_SAI_API_CHK(SAI_API_BUFFER, "DB Limit Set", rv);
    }
    /* Get current number of pools in use */
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_POOL_COUNT, &ie, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "ingress pool count data get", rv);
    if (_BRCM_SAI_MAX_BUF_POOLS <= data.pool_count.count)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "Exceeded max available buffer pools.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    count = data.pool_count.count;   
    
    /* Get a free buf pool */
    idx[0] = ie;
    idx[1] = index;    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_POOLS,
                                    idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff pool data get", rv);
    buf_pool.valid = TRUE;
    buf_pool.idx1 = ie;
    buf_pool.idx2 = index;
    memcpy(data.buf_pool, &buf_pool, sizeof(buf_pool));
  
    /* Bump active pool count */
    data.pool_count.count = ++count;
    data.pool_count.idx = ie;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_POOL_COUNT, &ie, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buffer pool count data set", rv);
     
    *pool_id = BRCM_SAI_CREATE_OBJ_SUB(SAI_OBJECT_TYPE_BUFFER_POOL, ie, (index+1));

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*    Remove buffer pool
*
* Arguments:
*    [in] pool_id  -pool id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_buffer_pool(_In_ sai_object_id_t pool_id)
{
    sai_status_t rv;
    int ie, index, idx[2];
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(pool_id, SAI_OBJECT_TYPE_BUFFER_POOL))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    ie = BRCM_SAI_GET_OBJ_SUB_TYPE(pool_id);
    index = BRCM_SAI_GET_OBJ_VAL(int, pool_id) - 1; /* rebase to 0 */
    idx[0] = ie;
    idx[1] = index;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_POOLS,
                                    idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff pool data get", rv);
    if (FALSE == data.buf_pool->valid)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    _brcm_sai_indexed_data_free_index2(_BRCM_SAI_INDEXED_BUF_POOLS, ie, index);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_POOL_COUNT, &ie, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buffer pool count data get", rv);
    data.pool_count.count--;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_POOL_COUNT, &ie, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buffer pool count data set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*    Set buffer pool attribute
* Arguments:
*   [in] pool_id – pool id
*   [in] attr - attribute
*
* Return Values:
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_buffer_pool_attr(_In_ sai_object_id_t pool_id,
                              _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*    Get buffer pool attributes
* Arguments:
*   [in] pool_id – pool id
*   [in] attr_count - number of attributes
*   [inout] attr_list - array of attributes
*
* Return Values:
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_buffer_pool_attr(_In_ sai_object_id_t pool_id,
                              _In_ uint32_t attr_count,
                              _Inout_ sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/**
* @brief   Get buffer pool statistics counters.
*
* @param[in] pool_id buffer pool id
* @param[in] counter_ids specifies the array of counter ids
* @param[in] number_of_counters number of counters in the array
* @param[out] counters array of resulting counter values.
*
* @return SAI_STATUS_SUCCESS on success
*         Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_buffer_pool_stats(_In_ sai_object_id_t pool_id,
                               _In_ uint32_t number_of_counters,
                               _In_ const sai_buffer_pool_stat_t *counter_ids,
                               _Out_ uint64_t* counters)
{
    sai_status_t rv;
    bcm_bst_stat_id_t stat;
    int i, ie, index, idx[2];
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(pool_id, SAI_OBJECT_TYPE_BUFFER_POOL))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    ie = BRCM_SAI_GET_OBJ_SUB_TYPE(pool_id);
    index = BRCM_SAI_GET_OBJ_VAL(int, pool_id) - 1; /* rebase to 0 */
    idx[0] = ie;
    idx[1] = index;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_POOLS,
                                    idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff pool data get", rv);
    if (FALSE == data.buf_pool->valid)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    for (i = 0; i < number_of_counters; i++)
    {
        switch(counter_ids[i])
        {
            case SAI_BUFFER_POOL_STAT_CURR_OCCUPANCY_BYTES:
            case SAI_BUFFER_POOL_STAT_DROPPED_PACKETS:
            case SAI_BUFFER_POOL_STAT_GREEN_WRED_DROPPED_PACKETS:
            case SAI_BUFFER_POOL_STAT_GREEN_WRED_DROPPED_BYTES:
            case SAI_BUFFER_POOL_STAT_YELLOW_WRED_DROPPED_PACKETS:
            case SAI_BUFFER_POOL_STAT_YELLOW_WRED_DROPPED_BYTES:
            case SAI_BUFFER_POOL_STAT_RED_WRED_DROPPED_PACKETS:
            case SAI_BUFFER_POOL_STAT_RED_WRED_DROPPED_BYTES:
            case SAI_BUFFER_POOL_STAT_WRED_DROPPED_PACKETS:
            case SAI_BUFFER_POOL_STAT_WRED_DROPPED_BYTES:
            case SAI_BUFFER_POOL_STAT_GREEN_WRED_ECN_MARKED_PACKETS:
            case SAI_BUFFER_POOL_STAT_GREEN_WRED_ECN_MARKED_BYTES:
            case SAI_BUFFER_POOL_STAT_YELLOW_WRED_ECN_MARKED_PACKETS:
            case SAI_BUFFER_POOL_STAT_YELLOW_WRED_ECN_MARKED_BYTES:
            case SAI_BUFFER_POOL_STAT_RED_WRED_ECN_MARKED_PACKETS:
            case SAI_BUFFER_POOL_STAT_RED_WRED_ECN_MARKED_BYTES:
            case SAI_BUFFER_POOL_STAT_WRED_ECN_MARKED_PACKETS:
            case SAI_BUFFER_POOL_STAT_WRED_ECN_MARKED_BYTES:
                rv = SAI_STATUS_NOT_IMPLEMENTED;
                break;
            case SAI_BUFFER_POOL_STAT_WATERMARK_BYTES:
            {
                uint64 tmp_counter;
                uint64 total_counter;
                int k = 0;

                COMPILER_64_ZERO(tmp_counter);
                COMPILER_64_ZERO(total_counter);
                COMPILER_64_ZERO(counters[i]);

                for (k = 0; k < NUM_LAYER(0); k++) 
                {
                    if (data.buf_pool->bst_rev_gport_maps[k])
                    {
                        stat = data.buf_pool->type == SAI_BUFFER_POOL_TYPE_INGRESS ?
                                   bcmBstStatIdIngPool : bcmBstStatIdEgrPool;
                        rv = bcm_cosq_bst_stat_sync(0, stat);
                        BRCM_SAI_API_CHK(SAI_API_BUFFER, "buffer pool bst stat sync", rv);
                        rv = bcm_cosq_bst_stat_get(0, data.buf_pool->bst_rev_gport_maps[k],
                                                   data.buf_pool->bst_rev_cos_maps[k],
                                                   stat, 0, &tmp_counter);
                        BRCM_SAI_API_CHK(SAI_API_BUFFER, "buffer pool bst stat get", rv);
                        COMPILER_64_ADD_64(total_counter, tmp_counter);
                        COMPILER_64_ZERO(tmp_counter);
                    }
                }
                counters[i] = total_counter * BRCM_MMU_B_PER_C;
                break;
            }
            default:
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                        "Unknown buffer pool stat id %d passed\n",
                                        counter_ids[i]);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }

        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error processing ing prio group stat\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/**
 * @brief Clear buffer pool statistics counters.
 *
 * @param[in] buffer_pool_id Buffer pool id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_clear_buffer_pool_stats(_In_ sai_object_id_t pool_id,
                                 _In_ uint32_t number_of_counters,
                                 _In_ const sai_buffer_pool_stat_t *counter_ids)
{
    sai_status_t rv;
    bcm_bst_stat_id_t stat;
    int i, ie, index, idx[2];
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_SET_STATS_PARAM_CHK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(pool_id, SAI_OBJECT_TYPE_BUFFER_POOL))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    ie = BRCM_SAI_GET_OBJ_SUB_TYPE(pool_id);
    index = BRCM_SAI_GET_OBJ_VAL(int, pool_id) - 1; /* rebase to 0 */
    idx[0] = ie;
    idx[1] = index;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_POOLS,
                                    idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff pool data get", rv);
    if (FALSE == data.buf_pool->valid)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    for (i = 0; i < number_of_counters; i++)
    {
        switch(counter_ids[i])
        {
            case SAI_BUFFER_POOL_STAT_WATERMARK_BYTES:
            {
                int k = 0;

                if ((SAI_BUFFER_POOL_TYPE_INGRESS == ie) && DEV_IS_TD2())
                {
                    /* HW limitation */
                    return SAI_STATUS_NOT_SUPPORTED;
                }
                for (k = 0; k < NUM_LAYER(0); k++)
                {
                    if (data.buf_pool->bst_rev_gport_maps[k])
                    {
                        stat = data.buf_pool->type == SAI_BUFFER_POOL_TYPE_INGRESS ?
                               bcmBstStatIdIngPool : bcmBstStatIdEgrPool;
                        rv = bcm_cosq_bst_stat_clear(0, data.buf_pool->bst_rev_gport_maps[k],
                                                     data.buf_pool->bst_rev_cos_maps[k],
                                                     stat);
                        BRCM_SAI_API_CHK(SAI_API_BUFFER, "buffer pool bst stat clear", rv);
                    }
                }
                break;
            }
            default:
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                        "Unknown ing prio group statistic %d passed\n",
                                        counter_ids[i]);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }

        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error processing ing prio group stat\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*    Create buffer profile
*
* Arguments:
*   [Out] buffer_profile_id  - buffer profile id
*   [in] attr_count - number of attributes
*   [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/

STATIC sai_status_t
brcm_sai_create_buffer_profile(_Out_ sai_object_id_t* buffer_profile_id,
                               _In_ sai_object_id_t switch_id,
                               _In_ uint32_t attr_count,
                               _In_ const sai_attribute_t *attr_list)
{
    int i, idx;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_buf_profile_t *buf_profile;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(buffer_profile_id);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_MMU_BUFF_PROFILE_COUNT, &data))
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Error getting mmu buffer profile count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    if (_BRCM_SAI_MAX_BUF_PROFILES <= data.u32)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "MMU buffer profile resource exhausted.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    /* Reserve an unused profile id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_BUF_PROFILES, 1,
                                              _BRCM_SAI_MAX_BUF_PROFILES, &idx);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff profile index reserve", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_PROFILES,
                                    &idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff profile data get", rv);
    buf_profile = idata.buf_prof;
    PTR_CLEAR(buf_profile, _brcm_sai_buf_profile_t);
    buf_profile->mode = -1;
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_BUFFER_PROFILE_ATTR_POOL_ID:
                buf_profile->pool_id = attr_list[i].value.oid;
                break;
            case SAI_BUFFER_PROFILE_ATTR_BUFFER_SIZE:
                buf_profile->size = attr_list[i].value.u32;
                break;
            case SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH:
                buf_profile->shared_val = attr_list[i].value.s8;
                break;
            case SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH:
                buf_profile->shared_val = attr_list[i].value.u32;
                break;
            case SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE:
                buf_profile->mode = attr_list[i].value.u32;
                break;
            case SAI_BUFFER_PROFILE_ATTR_XOFF_TH:
                buf_profile->xoff_thresh = attr_list[i].value.u32;
                break;
            case SAI_BUFFER_PROFILE_ATTR_XON_TH:
                buf_profile->xon_thresh = attr_list[i].value.u32;
                break;
            case SAI_BUFFER_PROFILE_ATTR_XON_OFFSET_TH:
                buf_profile->xon_offset_thresh = attr_list[i].value.u32;
                break;
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                        "Unknown mmu buffer profile attribute %d passed\n",
                                        attr_list[i].id);
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error processing mmu buffer profile attributes\n");
            return rv;
        }
    }
    buf_profile->valid = TRUE;
    buf_profile->idx = idx;
    *buffer_profile_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_BUFFER_PROFILE, idx);
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_MMU_BUFF_PROFILE_COUNT, INC);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "incrementing mmu buffer profile count global data", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*    Remove buffer profile
*
* Arguments:
*   [in] buffer_profile_id  - buffer profile id
*
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/

STATIC sai_status_t
brcm_sai_remove_buffer_profile(_In_ sai_object_id_t buffer_profile_id)
{
    int idx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(buffer_profile_id, SAI_OBJECT_TYPE_BUFFER_PROFILE))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    idx = BRCM_SAI_GET_OBJ_VAL(int, buffer_profile_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_PROFILES,
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff profile data get", rv);
    if (FALSE == data.buf_prof->valid)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    /* Free profile id */
    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_BUF_PROFILES, idx);
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_MMU_BUFF_PROFILE_COUNT, DEC);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "decrementing mmu buffer profile count global data", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*    Set buffer profile attribute
* Arguments:
*   [in] buffer_profile_id  - buffer profile id
*   [in] attr -  buffer profile attribute
*
* Return Values:
* SAI_STATUS_SUCCESS on success
* Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_buffer_profile_attribute(_In_ sai_object_id_t buffer_profile_id,
                                      _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;
    int buf_idx, i;
    int8_t shared_val = 0;
    _brcm_sai_buf_profile_t* buf_profile;
    _brcm_sai_data_t data;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, "NULL attr passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    buf_idx = BRCM_SAI_GET_OBJ_VAL(int, buffer_profile_id);
    
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_MMU_BUFF_PROFILE_COUNT, &data))
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Error getting mmu buffer profile count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    
    if (data.u32 < buf_idx)
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Invalid profile id passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    switch(attr->id)
    {
        case  SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH:
            shared_val = attr->value.s8;
            break;
        default:
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR, 
                                    "Unknown buffer profile attribute %d passed\n",
                                    attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
    }
    rv = _brcm_sai_buffer_profile_get(buffer_profile_id,
                                      &buf_profile);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER,
                    "Getting buffer profile data", rv);
    
    /* for now only handling shared_val */
    buf_profile->shared_val = shared_val;

    /* re-apply to pgs if any */
    for (i=0; i<_BRCM_SAI_MAX_PORTS;i++)
    {
        _brcm_sai_queue_bmap_t temp, pg;
        if (buf_profile->pg_data[i] == 0)
        {
            continue;
        }
        temp = buf_profile->pg_data[i];
        pg = 0;
        while (temp)
        {
            if (temp & 0x1)
            {
                /* reapply dynamic thresh value on port/pg */
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_DEBUG,
                                        "Applying profile update to port %d, pg %d, value %d\n",
                                    i,pg,shared_val);
                rv = driverPGAttributeSet(i, pg,
                                          2, buf_profile->shared_val);
                BRCM_SAI_RV_CHK(SAI_API_BUFFER,
                                "Updating port pg buffer profile", rv);
    
            }
            temp >>= 1;
            pg++;
        }        
    } /* ends for */
    
    /* check queues and re-apply if any */
    for (i=0; i<_BRCM_SAI_MAX_PORTS;i++)
    {
        _brcm_sai_queue_bmap_t temp, queue;
        if (buf_profile->queue_data[i] == 0)
        {
            continue;
        }
        temp = buf_profile->queue_data[i];
        queue = 0;
        while (temp)
        {
            if (temp & 0x1)
            {
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_DEBUG,
                                        "Applying profile update to port %d, queue %d, value %d\n",
                                    i,queue,buf_profile->shared_val);
                /* reapply dynamic thresh value on port/queue */
                rv = driverEgressQueueSharedAlphaSet(i, queue,
                                                     buf_profile->shared_val);
                BRCM_SAI_RV_CHK(SAI_API_BUFFER,
                                "Updating port queue buffer profile", rv);
            }
            temp >>= 1;
            queue++;
        }        
    } /* ends for */
    
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
* Routine Description:
*     Get buffer profile attributes
* Arguments:
*   [in] buffer_profile_id  - buffer profile id
*   [in] attr_count - number of attributes
*   [inout] attr_list - array of attributes
*
* Return Values:
*   SAI_STATUS_SUCCESS on success
*   Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_buffer_profile_attribute(_In_ sai_object_id_t buffer_profile_id,
                                      _In_ uint32_t attr_count,
                                      _Inout_ sai_attribute_t *attr_list)
{
    int i, prof_id;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_buf_profile_t *buf_profile;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(buffer_profile_id, SAI_OBJECT_TYPE_BUFFER_PROFILE))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    prof_id = BRCM_SAI_GET_OBJ_VAL(int, buffer_profile_id);
    BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_DEBUG,
                     "Get buffer profile: %d\n", prof_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_PROFILES,
                                    &prof_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buffer profile data get", rv);
    buf_profile = data.buf_prof;
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_BUFFER_PROFILE_ATTR_POOL_ID:
                attr_list[i].value.oid = buf_profile->pool_id;
                break;
            case SAI_BUFFER_PROFILE_ATTR_BUFFER_SIZE:
                attr_list[i].value.u32 = buf_profile->size;
                break;
            case SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH:
                attr_list[i].value.s8 = buf_profile->shared_val;
                break;
            case SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH:
                attr_list[i].value.u32 = buf_profile->shared_val;
                break;
            case SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE:
                attr_list[i].value.u32 = buf_profile->mode;
                break;
            case SAI_BUFFER_PROFILE_ATTR_XOFF_TH:
                attr_list[i].value.u32 = buf_profile->xoff_thresh;
                break;
            case SAI_BUFFER_PROFILE_ATTR_XON_TH:
                attr_list[i].value.u32 = buf_profile->xon_thresh;
                break;
            case SAI_BUFFER_PROFILE_ATTR_XON_OFFSET_TH:
                attr_list[i].value.u32 = buf_profile->xon_offset_thresh;
                break;
            default:
                BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                        "Unknown buffer profile attribute %d passed\n",
                                        attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_UDF(SAI_LOG_LEVEL_INFO,
                             "Error processing buffer profile attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

/*
################################################################################
#                               Internal functions                             #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_buff_pools()
{
    sai_status_t rv;
    int i, j, idx[2];
    _brcm_sai_indexed_data_t data;
    _brcm_sai_buf_pool_t *buf_pool;


    rv = _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_BUF_POOLS,
                                      2, _BRCM_SAI_MAX_BUF_POOLS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_BUFFER, SAI_LOG_LEVEL_CRITICAL,
                        "initializing buff pool data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_BUF_PROFILES,
                                     _BRCM_SAI_MAX_BUF_PROFILES);
    BRCM_SAI_RV_LVL_CHK(SAI_API_BUFFER, SAI_LOG_LEVEL_CRITICAL,
                        "initializing buff profile data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_POOL_COUNT, 2);
    BRCM_SAI_RV_LVL_CHK(SAI_API_BUFFER, SAI_LOG_LEVEL_CRITICAL,
                        "initializing buff pool count data", rv);

    /* converting data over upgrade */
    for ( i = 0; i < 2; i++)
    {
        for (j = 0; j < _BRCM_SAI_MAX_BUF_POOLS; j++)
        {
            idx[0] = i;   
            idx[1] = j;

            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_POOLS,
                                            idx, &data);
            BRCM_SAI_RV_LVL_CHK(SAI_API_BUFFER, SAI_LOG_LEVEL_CRITICAL,
                                "Retrieving buff pool data", rv);
            buf_pool = data.buf_pool;

            if (TRUE == buf_pool->valid)
            {
                if (0 != buf_pool->bst_rev_gport_map)
                {
                    buf_pool->bst_rev_gport_maps[0] = buf_pool->bst_rev_gport_map;
                }
                if (0 != buf_pool->bst_rev_cos_map)
                {
                    buf_pool->bst_rev_cos_maps[0] = buf_pool->bst_rev_cos_map;
                }
            }
            buf_pool->bst_rev_gport_map = 0;
            buf_pool->bst_rev_cos_map = 0;
        }
    }
   
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_buff_pools()
{
    sai_status_t rv;
    _brcm_sai_data_t data;

    rv = _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_BUF_POOLS,
                                      2, _BRCM_SAI_MAX_BUF_POOLS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_BUFFER, SAI_LOG_LEVEL_CRITICAL,
                        "freeing buff pool data", rv);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_MMU_BUFF_PROFILE_COUNT, &data))
    {
        BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_ERROR,
                                "Error getting mmu buffer profile count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_BUF_PROFILES,
                                      1, _BRCM_SAI_MAX_BUF_PROFILES, data.u32);
    BRCM_SAI_RV_LVL_CHK(SAI_API_BUFFER, SAI_LOG_LEVEL_CRITICAL,
                        "freeing buff profile data", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_POOL_COUNT,
                                      0, 2, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_BUFFER, SAI_LOG_LEVEL_CRITICAL,
                        "freeing buff pool count data", rv);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_buffer_profile_get(sai_object_id_t buffer_profile_id,
                             _brcm_sai_buf_profile_t **buf_profile)
{
    int bp;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    if (SAI_NULL_OBJECT_ID == buffer_profile_id)
    {
        return SAI_STATUS_NOT_EXECUTED;
    }
    bp = BRCM_SAI_GET_OBJ_VAL(int, buffer_profile_id);
    if ((bp < 0) || (bp >= _BRCM_SAI_MAX_BUF_PROFILES))
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_PROFILES,
                                    &bp, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff profile data get", rv);
    if (FALSE == data.buf_prof->valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    *buf_profile = data.buf_prof;
    
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_buffer_pool_get(sai_object_id_t buffer_pool_id,
                          _brcm_sai_buf_pool_t **buf_pool)
{
    sai_status_t rv;
    int idx[2], ie;
    _brcm_sai_indexed_data_t data;
    int bp = BRCM_SAI_GET_OBJ_VAL(int, buffer_pool_id) - 1; /* rebase to 0 */
    if ((bp < 0) || (bp >= _BRCM_SAI_MAX_BUF_POOLS))
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    ie = BRCM_SAI_GET_OBJ_SUB_TYPE(buffer_pool_id);
    idx[0] = ie;
    idx[1] = bp;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_BUF_POOLS,
                                    idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_BUFFER, "buff pool data get", rv);
    if (FALSE == data.buf_pool->valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    *buf_pool = data.buf_pool;
    
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_ingress_shared_limit_set(int pool_idx, int bp_size)
{
    int val;
    sai_status_t rv;

    if (DEV_IS_THX() || DEV_IS_TD3() || !bp_size)
    {
        return SAI_STATUS_SUCCESS;
    }
    /* Only for TD2 */
    rv = driverSPLimitGet(pool_idx, &val);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "SP Limit Get", rv);
    val -= bp_size; 
    rv = driverSPLimitSet(pool_idx, val);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "SP Limit Set", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_egress_shared_limit_set(int pool_idx, int pool_size, int bp_size)
{
    int val;
    sai_status_t rv;

    val = pool_size - (bp_size * _brcm_sai_switch_fp_port_count());
    rv = driverDBLimitSet(pool_idx, val);
    BRCM_SAI_API_CHK(SAI_API_BUFFER, "DB Limit Set", rv);   

    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                                  Functions map                               #
################################################################################
*/
const sai_buffer_api_t buffer_apis = {
    brcm_sai_create_buffer_pool,
    brcm_sai_remove_buffer_pool,
    brcm_sai_set_buffer_pool_attr,
    brcm_sai_get_buffer_pool_attr,
    brcm_sai_get_buffer_pool_stats,
    NULL, /* get_buffer_pool_stats_ext */
    brcm_sai_clear_buffer_pool_stats,
    NULL, /* create_ingress_priority_group */
    NULL, /* remove ingress_priority_group */
    brcm_sai_set_ingress_priority_group_attribute,
    brcm_sai_get_ingress_priority_group_attribute,
    brcm_sai_get_ingress_priority_group_stats,
    NULL, /* get_ingress_priority_group_stats_ext */
    brcm_sai_clear_ingress_priority_group_stats,
    brcm_sai_create_buffer_profile,
    brcm_sai_remove_buffer_profile,
    brcm_sai_set_buffer_profile_attribute,
    brcm_sai_get_buffer_profile_attribute,
};

