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
#                             Forward declarations                             #
################################################################################
*/
sai_status_t
_brcm_sai_create_ingress_map(int map_type, int list_size,
                             _brcm_sai_global_data_type_t count_type,
                             _brcm_sai_indexed_data_type_t map_data_type,
                             sai_qos_map_t *list,
                             sai_object_id_t* qos_map_id);

sai_status_t
_brcm_sai_create_egress_map(int map_type, int list_size,
                            _brcm_sai_global_data_type_t count_type,
                            _brcm_sai_indexed_data_type_t map_data_type,
                            sai_qos_map_t *list,
                            sai_object_id_t* qos_map_id);

/*
################################################################################
#                             QOS MAPS functions                               #
################################################################################
*/

/**
 * @brief Create Qos Map
 *
 * @param[out] qos_map_id Qos Map Id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list array of attributes
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_qos_map(_Out_ sai_object_id_t* qos_map_id,
                        _In_ sai_object_id_t switch_id,
                        _In_ uint32_t attr_count,
                        _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, map_type = -1;
    BRCM_SAI_FUNCTION_ENTER(SAI_API_QOS_MAP);
    BRCM_SAI_SWITCH_INIT_CHECK;

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_QOS_MAP_ATTR_TYPE:
                map_type = attr_list[i].value.u32;
            case SAI_QOS_MAP_ATTR_MAP_TO_VALUE_LIST:
                break;
            default:
                BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                                  "Unknown qos map attribute %d passed\n",
                                  attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
        if (-1 != map_type)
        {
            break;
        }
    }
    if (-1 == map_type)
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR, "No map type provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_QOS_MAP_ATTR_TYPE:
                break;
            case SAI_QOS_MAP_ATTR_MAP_TO_VALUE_LIST:
                switch (map_type)
                {
                    case SAI_QOS_MAP_TYPE_DOT1P_TO_TC:
                        if (SAI_STATUS_SUCCESS != 
                            _brcm_sai_create_ingress_map(map_type,
                                                         attr_list[i].value.qosmap.count, 
                                                         _BRCM_SAI_DOT1P_TC_MAP_COUNT, 
                                                         _BRCM_SAI_INDEXED_DOT1P_TC_MAP,
                                                         attr_list[i].value.qosmap.list,
                                                         qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_DOT1P_TO_COLOR:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_ingress_map(map_type,
                                                         attr_list[i].value.qosmap.count, 
                                                         _BRCM_SAI_DOT1P_COLOR_MAP_COUNT, 
                                                         _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP,
                                                         attr_list[i].value.qosmap.list,
                                                         qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_DSCP_TO_TC:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_ingress_map(map_type,
                                                         attr_list[i].value.qosmap.count, 
                                                         _BRCM_SAI_DSCP_TC_MAP_COUNT, 
                                                         _BRCM_SAI_INDEXED_DSCP_TC_MAP,
                                                         attr_list[i].value.qosmap.list,
                                                         qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_DSCP_TO_COLOR:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_ingress_map(map_type,
                                                         attr_list[i].value.qosmap.count, 
                                                         _BRCM_SAI_DSCP_COLOR_MAP_COUNT, 
                                                         _BRCM_SAI_INDEXED_DSCP_COLOR_MAP,
                                                         attr_list[i].value.qosmap.list,
                                                         qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DSCP:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_egress_map(map_type,
                                                        attr_list[i].value.qosmap.count, 
                                                        _BRCM_SAI_TC_DSCP_MAP_COUNT, 
                                                        _BRCM_SAI_INDEXED_TC_DSCP_MAP,
                                                        attr_list[i].value.qosmap.list,
                                                        qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DOT1P:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_egress_map(map_type,
                                                        attr_list[i].value.qosmap.count, 
                                                        _BRCM_SAI_TC_DOT1P_MAP_COUNT, 
                                                        _BRCM_SAI_INDEXED_TC_DOT1P_MAP,
                                                        attr_list[i].value.qosmap.list,
                                                        qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_TC_TO_QUEUE:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_egress_map(map_type,
                                                        attr_list[i].value.qosmap.count, 
                                                        _BRCM_SAI_TC_QUEUE_MAP_COUNT, 
                                                        _BRCM_SAI_INDEXED_TC_QUEUE_MAP,
                                                        attr_list[i].value.qosmap.list,
                                                        qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_TC_TO_PRIORITY_GROUP:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_egress_map(map_type,
                                                        attr_list[i].value.qosmap.count, 
                                                        _BRCM_SAI_TC_PG_MAP_COUNT, 
                                                        _BRCM_SAI_INDEXED_TC_PG_MAP,
                                                        attr_list[i].value.qosmap.list,
                                                        qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_QUEUE:
                        if (SAI_STATUS_SUCCESS !=
                            _brcm_sai_create_egress_map(map_type,
                                                        attr_list[i].value.qosmap.count, 
                                                        _BRCM_SAI_PFC_QUEUE_MAP_COUNT, 
                                                        _BRCM_SAI_INDEXED_PFC_QUEUE_MAP,
                                                        attr_list[i].value.qosmap.list,
                                                        qos_map_id))
                        {
                            return SAI_STATUS_INSUFFICIENT_RESOURCES;
                        }
                        break;
                    case SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_PRIORITY_GROUP:
                    default:
                        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                                          "Unsupported qos map attribute %d passed\n",
                                          attr_list[i].id);
                        return SAI_STATUS_NOT_SUPPORTED;
                }
                break;
            default:
                BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                                  "Unknown qos map attribute %d passed\n",
                                  attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_QOS_MAP);
    return rv;
}

/**
 * @brief Remove Qos Map
 *
 *  @param[in] qos_map_id Qos Map id to be removed.
 *
 *  @return  SAI_STATUS_SUCCESS on success
 *           Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_qos_map(_In_ sai_object_id_t qos_map_id)
{
    sai_status_t rv;
    bool dir = FALSE;
    int idx, map_type;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_global_data_type_t count_type;
    _brcm_sai_indexed_data_type_t map_data_type;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_QOS_MAP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(qos_map_id, SAI_OBJECT_TYPE_QOS_MAP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    idx = BRCM_SAI_GET_OBJ_VAL(int, qos_map_id);
    map_type = BRCM_SAI_GET_OBJ_SUB_TYPE(qos_map_id);
    switch (map_type)
    {
        case SAI_QOS_MAP_TYPE_DOT1P_TO_TC:
            map_data_type = _BRCM_SAI_INDEXED_DOT1P_TC_MAP;
            count_type = _BRCM_SAI_DOT1P_TC_MAP_COUNT;
            break;
        case SAI_QOS_MAP_TYPE_DOT1P_TO_COLOR:
            map_data_type = _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP;
            count_type = _BRCM_SAI_DOT1P_COLOR_MAP_COUNT;
            break;
        case SAI_QOS_MAP_TYPE_DSCP_TO_TC:
            map_data_type = _BRCM_SAI_INDEXED_DSCP_TC_MAP;
            count_type = _BRCM_SAI_DSCP_TC_MAP_COUNT;
            break;
        case SAI_QOS_MAP_TYPE_DSCP_TO_COLOR:
            map_data_type = _BRCM_SAI_INDEXED_DSCP_COLOR_MAP;
            count_type = _BRCM_SAI_DSCP_COLOR_MAP_COUNT;
            break;
        case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DSCP:
            map_data_type = _BRCM_SAI_INDEXED_TC_DSCP_MAP;
            count_type = _BRCM_SAI_TC_DSCP_MAP_COUNT;
            dir = TRUE;
            break;
        case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DOT1P:
            map_data_type = _BRCM_SAI_INDEXED_TC_DOT1P_MAP;
            count_type = _BRCM_SAI_TC_DOT1P_MAP_COUNT;
            dir = TRUE;
            break;
        case SAI_QOS_MAP_TYPE_TC_TO_QUEUE:
            map_data_type = _BRCM_SAI_INDEXED_TC_QUEUE_MAP;
            count_type = _BRCM_SAI_TC_QUEUE_MAP_COUNT;
            dir = TRUE;
            break;
        case SAI_QOS_MAP_TYPE_TC_TO_PRIORITY_GROUP:
            map_data_type = _BRCM_SAI_INDEXED_TC_PG_MAP;
            count_type = _BRCM_SAI_TC_PG_MAP_COUNT;
            dir = TRUE;
            break;
        case SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_QUEUE:
            map_data_type = _BRCM_SAI_INDEXED_PFC_QUEUE_MAP;
            count_type = _BRCM_SAI_PFC_QUEUE_MAP_COUNT;
            dir = TRUE;
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }
    rv = _brcm_sai_indexed_data_get(map_data_type,
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_QOS_MAP, "qos map data get", rv);
    if (dir)
    {
        if (FALSE == data.egress_map->valid)
        {
            return SAI_STATUS_ITEM_NOT_FOUND;
        }
    }
    else
    {
        if (FALSE == data.ingress_map->valid)
        {
            return SAI_STATUS_ITEM_NOT_FOUND;
        }
    }
    _brcm_sai_indexed_data_free_index(map_data_type, idx);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(count_type, DEC))
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                          "Error decrementing qos map count %d global data.\n",
                          count_type);
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_QOS_MAP);
    return rv;
}

/**
 * @brief Set attributes for qos map
 *
 * @param[in] qos_map_id Qos Map Id
 * @param[in] attr attribute to set
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */

STATIC sai_status_t
brcm_sai_set_qos_map_attribute(_In_ sai_object_id_t qos_map_id,
                               _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_QOS_MAP);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_QOS_MAP);
    return rv;
}

/**
 * @brief  Get attrbutes of qos map
 *
 * @param[in] qos_map_id  map id
 * @param[in] attr_count  number of attributes
 * @param[inout] attr_list  array of attributes
 *
 * @return SAI_STATUS_SUCCESS on success
 *        Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_qos_map_attribute(_In_ sai_object_id_t qos_map_id,
                               _In_ uint32_t attr_count,
                               _Inout_ sai_attribute_t *attr_list)
{
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, map_id, map_type, map_data_type;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_QOS_MAP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(qos_map_id, SAI_OBJECT_TYPE_QOS_MAP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    map_id = BRCM_SAI_GET_OBJ_VAL(int, qos_map_id);
    BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_DEBUG,
                      "Get qos map: %d\n", map_id);
    map_type = BRCM_SAI_GET_OBJ_SUB_TYPE(qos_map_id);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_QOS_MAP_ATTR_TYPE:
                attr_list[i].value.u32 = map_type;
                break;
            case SAI_QOS_MAP_ATTR_MAP_TO_VALUE_LIST:
            {
                int m, act, limit;
                sai_qos_map_t *map;
                bool ingress = TRUE;

                switch (map_type)
                {
                    case SAI_QOS_MAP_TYPE_DOT1P_TO_TC:
                        map_data_type = _BRCM_SAI_INDEXED_DOT1P_TC_MAP;
                        break;
                    case SAI_QOS_MAP_TYPE_DOT1P_TO_COLOR:
                        map_data_type = _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP;
                        break;
                    case SAI_QOS_MAP_TYPE_DSCP_TO_TC:
                        map_data_type = _BRCM_SAI_INDEXED_DSCP_TC_MAP;
                        break;
                    case SAI_QOS_MAP_TYPE_DSCP_TO_COLOR:
                        map_data_type = _BRCM_SAI_INDEXED_DSCP_COLOR_MAP;
                        break;
                    case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DOT1P:
                        map_data_type = _BRCM_SAI_INDEXED_TC_DOT1P_MAP;
                        ingress = FALSE;
                        break;
                    case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DSCP:
                        map_data_type = _BRCM_SAI_INDEXED_TC_DSCP_MAP;
                        ingress = FALSE;
                        break;
                    case SAI_QOS_MAP_TYPE_TC_TO_QUEUE:
                        map_data_type = _BRCM_SAI_INDEXED_TC_QUEUE_MAP;
                        ingress = FALSE;
                        break;
                    case SAI_QOS_MAP_TYPE_TC_TO_PRIORITY_GROUP:
                        map_data_type = _BRCM_SAI_INDEXED_TC_PG_MAP;
                        ingress = FALSE;
                        break;
                    case SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_QUEUE:
                        map_data_type = _BRCM_SAI_INDEXED_PFC_QUEUE_MAP;
                        ingress = FALSE;
                        break;
                    default:
                        return SAI_STATUS_INVALID_OBJECT_ID;
                }
                rv = _brcm_sai_indexed_data_get(map_data_type,
                                                &map_id, &data);
                BRCM_SAI_RV_CHK(SAI_API_UDF, "qos map data get", rv);
                if ((ingress && !data.ingress_map->valid) ||
                    (!ingress && !data.egress_map->valid))
                {
                    rv = SAI_STATUS_INVALID_OBJECT_ID;
                    break;
                }
                act = limit = ingress ? data.ingress_map->count : data.egress_map->count;
                map = ingress ? data.ingress_map->map : data.egress_map->map;
                if (attr_list[i].value.qosmap.count < limit)
                {
                    limit = attr_list[i].value.qosmap.count;
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                for(m=0; m<limit; m++)
                {
                    attr_list[i].value.qosmap.list[m] = map[m];
                }
                attr_list[i].value.qosmap.count = act;
                break;
            }
            default:
                BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                                  "Unknown qos map attribute %d passed\n",
                                  attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_INFO,
                              "Error processing qos map attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_QOS_MAP);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_qos_map()
{
    sai_status_t rv;

    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_DOT1P_TC_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing dot1p tc map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_DOT1P_COLOR_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing dot1p color map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_DSCP_TC_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing dscp tc map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_DSCP_COLOR_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing dscp color map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TC_DSCP_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing tc dscp map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TC_DOT1P_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing tc dot1p map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TC_QUEUE_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing tc queue map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TC_PG_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing tc pg map data", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_PFC_QUEUE_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "initializing pfc queue map data", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_qos_map()
{
    sai_status_t rv;

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_DOT1P_TC_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing dot1p tc map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_DOT1P_COLOR_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing dot1p color map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_DSCP_TC_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing dscp tc map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_DSCP_COLOR_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing dscp color map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TC_DSCP_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tc dscp map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TC_DOT1P_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tc dot1p map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TC_QUEUE_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tc queue map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TC_PG_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tc pg map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_PFC_QUEUE_MAP, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_QOS_MAP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing pfc queue map", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_ingress_qosmap_get(uint8_t map_type, uint32_t map_id,
                             _brcm_sai_qos_ingress_map_t *map)
{
    
    int i = map_id;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_indexed_data_type_t map_data_type;
    
    switch(map_type)
    {
        case SAI_QOS_MAP_TYPE_DOT1P_TO_TC:
            map_data_type = _BRCM_SAI_INDEXED_DOT1P_TC_MAP;
            break;
        case SAI_QOS_MAP_TYPE_DOT1P_TO_COLOR:
            map_data_type = _BRCM_SAI_INDEXED_DOT1P_COLOR_MAP;
            break;
        case SAI_QOS_MAP_TYPE_DSCP_TO_TC:
            map_data_type = _BRCM_SAI_INDEXED_DSCP_TC_MAP;
            break;
        case SAI_QOS_MAP_TYPE_DSCP_TO_COLOR:
            map_data_type = _BRCM_SAI_INDEXED_DSCP_COLOR_MAP;
            break;
        default:
            BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR, "Invalid ingress qos map type %d\n",
                              map_type);
            return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_indexed_data_get(map_data_type,
                                    &i, &data);
    BRCM_SAI_RV_CHK(SAI_API_QOS_MAP, "ingress map data get", rv);
    *map = *(data.ingress_map);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_egress_qosmap_get(uint8_t map_type, uint32_t map_id,
                            _brcm_sai_qos_egress_map_t *map)
{
    int i = map_id;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_indexed_data_type_t map_data_type;
    
    switch(map_type)
    {
        case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DSCP:
            map_data_type = _BRCM_SAI_INDEXED_TC_DSCP_MAP;
            break;
        case SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DOT1P:
            map_data_type = _BRCM_SAI_INDEXED_TC_DOT1P_MAP;
            break;
        case SAI_QOS_MAP_TYPE_TC_TO_QUEUE:
            map_data_type = _BRCM_SAI_INDEXED_TC_QUEUE_MAP;
            break;
        case SAI_QOS_MAP_TYPE_TC_TO_PRIORITY_GROUP:
            map_data_type = _BRCM_SAI_INDEXED_TC_PG_MAP;
            break;
        case SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_QUEUE: 
            map_data_type = _BRCM_SAI_INDEXED_PFC_QUEUE_MAP;
            break;
        default:
            BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR, "Invalid egress qos map type %d\n",
                              map_type);
            return SAI_STATUS_FAILURE;
    }

    rv = _brcm_sai_indexed_data_get(map_data_type,
                                    &i, &data);
    BRCM_SAI_RV_CHK(SAI_API_QOS_MAP, "egress map data get", rv);
    *map = *(data.egress_map);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_create_ingress_map(int map_type, int list_size, 
                             _brcm_sai_global_data_type_t count_type,
                             _brcm_sai_indexed_data_type_t map_data_type,
                             sai_qos_map_t *list,
                             sai_object_id_t* qos_map_id)
{
    int m, j;
    sai_status_t rv;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;
    _brcm_sai_qos_ingress_map_t *map;
    

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(count_type, &data))
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                          "Error getting ingress map count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    if (_BRCM_SAI_MAX_PORTS < data.u32)
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR, "Max map count exceeded.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(map_data_type, 1,
                                              _BRCM_SAI_MAX_PORTS, &m);
    BRCM_SAI_RV_CHK(SAI_API_QOS_MAP, "ingress map data index reserve", rv);
    rv = _brcm_sai_indexed_data_get(map_data_type,
                                    &m, &idata);
    BRCM_SAI_RV_CHK(SAI_API_QOS_MAP, "ingress map data get", rv);
    map = idata.ingress_map;
    for (j=0; j<list_size; j++)
    {
        map->map[j] = list[j];
    }
    map->count = list_size;
    map->idx = m;
    *qos_map_id = BRCM_SAI_CREATE_OBJ_SUB(SAI_OBJECT_TYPE_QOS_MAP, map_type, m);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(count_type, INC))
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                          "Error incrementing ingress map count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_create_egress_map(int map_type, int list_size, 
                            _brcm_sai_global_data_type_t count_type,
                            _brcm_sai_indexed_data_type_t map_data_type,
                            sai_qos_map_t *list,
                            sai_object_id_t* qos_map_id)
{
    int m, j;
    sai_status_t rv;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;
    _brcm_sai_qos_egress_map_t *map;

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(count_type, &data))
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                          "Error getting egress map count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    if (_BRCM_SAI_MAX_PORTS < data.u32)
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR, "Max map count exceeded.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(map_data_type, 1,
                                              _BRCM_SAI_MAX_PORTS, &m);
    BRCM_SAI_RV_CHK(SAI_API_QOS_MAP, "ingress map data index reserve", rv);
    rv = _brcm_sai_indexed_data_get(map_data_type,
                                    &m, &idata);
    BRCM_SAI_RV_CHK(SAI_API_QOS_MAP, "egress map data get", rv);
    map = idata.egress_map;
    for (j=0; j<list_size; j++)
    {
        map->map[j] = list[j];
    }
    map->count = list_size;
    map->idx = m;
    *qos_map_id = BRCM_SAI_CREATE_OBJ_SUB(SAI_OBJECT_TYPE_QOS_MAP, map_type, m);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(count_type, INC))
    {
        BRCM_SAI_LOG_QMAP(SAI_LOG_LEVEL_ERROR,
                          "Error incrementing egress map count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_qos_map_api_t qos_map_apis = {
    brcm_sai_create_qos_map,
    brcm_sai_remove_qos_map,
    brcm_sai_set_qos_map_attribute,
    brcm_sai_get_qos_map_attribute
};
