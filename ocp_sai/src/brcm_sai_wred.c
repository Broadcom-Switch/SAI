/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>


static sai_status_t
_brcm_sai_reapply_wred_profile(sai_object_id_t wred_id,
                               _brcm_sai_qos_wred_t* wred_p);
sai_status_t
_brcm_sai_wred_update_port_data(int id, bcm_port_t port, int queue,
                                int val);


/*
################################################################################
#                                  WRED functions                              #
################################################################################
*/

/**
 * @brief Create WRED Profile
 *
 * @param[out] wred_id - Wred profile Id.
 * @param[in] attr_count - number of attributes
 * @param[in] attr_list - array of attributes
 *
 *
 * @return SAI_STATUS_SUCCESS on success
 *         Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_wred(_Out_ sai_object_id_t *wred_id,
                     _In_ sai_object_id_t switch_id,
                     _In_ uint32_t attr_count,
                     _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    uint8_t wred_type = 0;
    int i, w;
    _brcm_sai_data_t gdata;
    _brcm_sai_qos_wred_t wred_p;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_WRED);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(wred_id);
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_WRED_ATTR_GREEN_ENABLE:
                if (TRUE == attr_list[i].value.booldata)
                {
                    wred_type |= _BRCM_SAI_WRED_ATTR_GREEN;
                }
                break;
            case SAI_WRED_ATTR_YELLOW_ENABLE:
                if (TRUE == attr_list[i].value.booldata)
                {
                    wred_type |= _BRCM_SAI_WRED_ATTR_YELLOW;
                }
                break;
            case SAI_WRED_ATTR_RED_ENABLE:
                if (TRUE == attr_list[i].value.booldata)
                {
                    wred_type |= _BRCM_SAI_WRED_ATTR_RED;
                }
                break;
            default:
                break;
        }
        if (0x7 == wred_type)
        {
            break;
        }
    }
    if (0 == wred_type)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "No active wred color profile provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_get(_BRCM_SAI_WRED_COUNT, &gdata))
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR,
                          "Error getting wred count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    if (_BRCM_SAI_MAX_WRED < gdata.u32)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Max wred count reached.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    /* Reserve an unused profile id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_WRED_PROF, 1,
                                              _BRCM_SAI_MAX_WRED+1, &w);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Unexpected wred resource issue.\n");
        return rv;
    }
    if (_BRCM_SAI_MAX_WRED < w)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Max wred count exceeded.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(&wred_p, 0, sizeof(_brcm_sai_qos_wred_t));
    wred_p.discard_g.rt = 8;
    wred_p.discard_y.rt = 8;
    wred_p.discard_r.rt = 8;
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_WRED_ATTR_GREEN_ENABLE:
                if (TRUE == attr_list[i].value.booldata)
                {
                    wred_p.discard_g.discard.flags |= BCM_COSQ_DISCARD_COLOR_GREEN |
                                                      BCM_COSQ_DISCARD_ENABLE |
                                                      BCM_COSQ_DISCARD_BYTES;
                }
                else
                {
                    wred_p.discard_g.discard.flags &= ~(BCM_COSQ_DISCARD_COLOR_GREEN |
                                                        BCM_COSQ_DISCARD_ENABLE |
                                                        BCM_COSQ_DISCARD_BYTES);
                }
                break;
            case SAI_WRED_ATTR_YELLOW_ENABLE:
                if (TRUE == attr_list[i].value.booldata)
                {
                    wred_p.discard_y.discard.flags |= BCM_COSQ_DISCARD_COLOR_YELLOW |
                                              BCM_COSQ_DISCARD_ENABLE |
                                              BCM_COSQ_DISCARD_BYTES;
                }
                else
                {
                    wred_p.discard_g.discard.flags &= ~(BCM_COSQ_DISCARD_COLOR_YELLOW |
                                                        BCM_COSQ_DISCARD_ENABLE |
                                                        BCM_COSQ_DISCARD_BYTES);
                }
                break;
            case SAI_WRED_ATTR_RED_ENABLE:
                if (TRUE == attr_list[i].value.booldata)
                {
                    wred_p.discard_r.discard.flags |= BCM_COSQ_DISCARD_COLOR_RED |
                                                      BCM_COSQ_DISCARD_ENABLE |
                                                      BCM_COSQ_DISCARD_BYTES;
                }
                else
                {
                    wred_p.discard_g.discard.flags &= ~(BCM_COSQ_DISCARD_COLOR_RED |
                                                        BCM_COSQ_DISCARD_ENABLE |
                                                        BCM_COSQ_DISCARD_BYTES);
                }
                break;
            case SAI_WRED_ATTR_GREEN_MIN_THRESHOLD:
                wred_p.discard_g.discard.min_thresh = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD:
                wred_p.discard_y.discard.min_thresh = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_RED_MIN_THRESHOLD:
                wred_p.discard_r.discard.min_thresh = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_GREEN_MAX_THRESHOLD:
                wred_p.discard_g.discard.max_thresh = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD:
                wred_p.discard_y.discard.max_thresh = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_RED_MAX_THRESHOLD:
                wred_p.discard_r.discard.max_thresh = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_GREEN_DROP_PROBABILITY:
                wred_p.discard_g.discard.drop_probability = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY:
                wred_p.discard_y.discard.drop_probability = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_RED_DROP_PROBABILITY:
                wred_p.discard_r.discard.drop_probability = attr_list[i].value.u32;
                break;
            case SAI_WRED_ATTR_WEIGHT:
                wred_p.gn = attr_list[i].value.u8;
                break;
            case SAI_WRED_ATTR_ECN_MARK_MODE:
                if (SAI_ECN_MARK_MODE_NONE != attr_list[i].value.booldata)
                {
                    switch (attr_list[i].value.u32)
                    {
                        case SAI_ECN_MARK_MODE_GREEN:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_YELLOW:
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_RED:
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_GREEN_YELLOW:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_GREEN_RED:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_YELLOW_RED:
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_ALL:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        default:
                            return SAI_STATUS_INVALID_PARAMETER;
                    }
                    wred_p.ect = TRUE;
                }
                break;
            default:
                BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR,
                                  "Unknown WRED attribute %d passed\n", attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }
    wred_p.idx = w;
    wred_p.valid = wred_type;
    data.wred_prof = wred_p;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_WRED_PROF, &w, &data);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Unable to save wred prof data.\n");
        return rv;
    }
    BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_INFO, "Created wred prof id: %d\n", w);
    *wred_id = BRCM_SAI_CREATE_OBJ_SUB(SAI_OBJECT_TYPE_WRED, wred_type, w);
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_bump(_BRCM_SAI_WRED_COUNT, INC))
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR,
                          "Error incrementing wred count global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_WRED);
    return rv;
}

/**
 * @brief Remove WRED Profile
 *
 * @param[in] wred_id Wred profile Id.
 *
 * @return SAI_STATUS_SUCCESS on success
 *         Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_wred(_In_ sai_object_id_t  wred_id)
{
    int w;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_WRED);
    BRCM_SAI_SWITCH_INIT_CHECK;
    
    w = BRCM_SAI_GET_OBJ_VAL(int, wred_id);
    if (_BRCM_SAI_MAX_WRED < w)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred id.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_WRED_PROF, w);
    if (SAI_STATUS_SUCCESS != _brcm_sai_global_data_bump(_BRCM_SAI_WRED_COUNT, DEC))
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR,
                          "Error decrementing wred count global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_WRED);
    return rv;
}

/**
 * @brief Set attributes to Wred profile.
 *
 * @param[out] wred_id Wred profile Id.
 * @param[in] attr attribute
 *
 *
 * @return SAI_STATUS_SUCCESS on success
 *         Failure status code on error
 */

STATIC sai_status_t
brcm_sai_set_wred_attribute(_In_ sai_object_id_t wred_id,
                            _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;
    int w;
    _brcm_sai_qos_wred_t wred_p;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_WRED);
    BRCM_SAI_SWITCH_INIT_CHECK;

    w = BRCM_SAI_GET_OBJ_VAL(int, wred_id);
    if (_BRCM_SAI_MAX_WRED < w)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred id.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_WRED_PROF,
                                    &w, &data);
    BRCM_SAI_RV_CHK(SAI_API_WRED, "wred data get", rv);
    wred_p = data.wred_prof;
    if (FALSE == wred_p.valid)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred obj id.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    switch (attr->id)
        {
            case SAI_WRED_ATTR_GREEN_ENABLE:
                if (TRUE == attr->value.booldata)
                {
                    wred_p.discard_g.discard.flags |= BCM_COSQ_DISCARD_COLOR_GREEN |
                      BCM_COSQ_DISCARD_ENABLE |
                      BCM_COSQ_DISCARD_BYTES;
                    wred_p.valid |= _BRCM_SAI_WRED_ATTR_GREEN;
                }
                else
                {
                    wred_p.discard_g.discard.flags &= ~(BCM_COSQ_DISCARD_COLOR_GREEN |
                                                        BCM_COSQ_DISCARD_ENABLE |
                                                        BCM_COSQ_DISCARD_BYTES);
                    wred_p.valid &= ~(_BRCM_SAI_WRED_ATTR_GREEN);
                }
                break;
            case SAI_WRED_ATTR_YELLOW_ENABLE:
                if (TRUE == attr->value.booldata)
                {
                    wred_p.discard_y.discard.flags |= BCM_COSQ_DISCARD_COLOR_YELLOW |
                      BCM_COSQ_DISCARD_ENABLE |
                      BCM_COSQ_DISCARD_BYTES;
                    wred_p.valid |= _BRCM_SAI_WRED_ATTR_YELLOW;
                }
                else
                {
                    wred_p.discard_g.discard.flags &= ~(BCM_COSQ_DISCARD_COLOR_YELLOW |
                                                        BCM_COSQ_DISCARD_ENABLE |
                                                        BCM_COSQ_DISCARD_BYTES);
                    wred_p.valid &= ~(_BRCM_SAI_WRED_ATTR_YELLOW);
                }
                break;
            case SAI_WRED_ATTR_RED_ENABLE:
                if (TRUE == attr->value.booldata)
                {
                    wred_p.discard_r.discard.flags |= BCM_COSQ_DISCARD_COLOR_RED |
                      BCM_COSQ_DISCARD_ENABLE |
                      BCM_COSQ_DISCARD_BYTES;
                    wred_p.valid |= _BRCM_SAI_WRED_ATTR_RED;
                }
                else
                {
                    wred_p.discard_g.discard.flags &= ~(BCM_COSQ_DISCARD_COLOR_RED |
                                                        BCM_COSQ_DISCARD_ENABLE |
                                                        BCM_COSQ_DISCARD_BYTES);
                    wred_p.valid &= ~(_BRCM_SAI_WRED_ATTR_RED);;
                }
                break;
            case SAI_WRED_ATTR_GREEN_MIN_THRESHOLD:
                wred_p.discard_g.discard.min_thresh = attr->value.u32;
                break;
            case SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD:
                wred_p.discard_y.discard.min_thresh = attr->value.u32;
                break;
            case SAI_WRED_ATTR_RED_MIN_THRESHOLD:
                wred_p.discard_r.discard.min_thresh = attr->value.u32;
                break;
            case SAI_WRED_ATTR_GREEN_MAX_THRESHOLD:
                wred_p.discard_g.discard.max_thresh = attr->value.u32;
                break;
            case SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD:
                wred_p.discard_y.discard.max_thresh = attr->value.u32;
                break;
            case SAI_WRED_ATTR_RED_MAX_THRESHOLD:
                wred_p.discard_r.discard.max_thresh = attr->value.u32;
                break;
            case SAI_WRED_ATTR_GREEN_DROP_PROBABILITY:
                wred_p.discard_g.discard.drop_probability = attr->value.u32;
                break;
            case SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY:
                wred_p.discard_y.discard.drop_probability = attr->value.u32;
                break;
            case SAI_WRED_ATTR_RED_DROP_PROBABILITY:
                wred_p.discard_r.discard.drop_probability = attr->value.u32;
                break;
            case SAI_WRED_ATTR_WEIGHT:
                wred_p.gn = attr->value.u8;
                break;
            case SAI_WRED_ATTR_ECN_MARK_MODE:
                /* Reset all current ECN state */
                wred_p.discard_g.discard.flags &= ~(BCM_COSQ_DISCARD_MARK_CONGESTION);
                wred_p.discard_y.discard.flags &= ~(BCM_COSQ_DISCARD_MARK_CONGESTION);
                wred_p.discard_r.discard.flags &= ~(BCM_COSQ_DISCARD_MARK_CONGESTION);
                wred_p.ect = FALSE;
                if (SAI_ECN_MARK_MODE_NONE != attr->value.booldata)
                {
                    switch (attr->value.u32)
                    {
                        case SAI_ECN_MARK_MODE_GREEN:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_YELLOW:
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_RED:
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_GREEN_YELLOW:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_GREEN_RED:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_YELLOW_RED:
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        case SAI_ECN_MARK_MODE_ALL:
                            wred_p.discard_g.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_y.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            wred_p.discard_r.discard.flags |= (BCM_COSQ_DISCARD_MARK_CONGESTION);
                            break;
                        default:
                            return SAI_STATUS_INVALID_PARAMETER;
                    }
                    wred_p.ect = TRUE;
                }
                break;
            default:
                BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR,
                                  "Unknown WRED attribute %d passed\n", attr->id);
                return SAI_STATUS_INVALID_PARAMETER;
        }        
    
    data.wred_prof = wred_p;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_WRED_PROF, &w, &data);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Unable to save wred prof data.\n");
        return rv;
    }

    _brcm_sai_reapply_wred_profile(wred_id, &wred_p);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_WRED);
    return rv;
}

/**
 * @brief  Get Wred profile attribute
 *
 * @param[in] wred_id Wred Profile Id
 * @param[in] attr_count number of attributes
 * @param[inout] attr_list  array of attributes
 *
 * @return SAI_STATUS_SUCCESS on success
 *        Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_wred_attribute(_In_ sai_object_id_t wred_id,
                            _In_ uint32_t attr_count,
                            _Inout_ sai_attribute_t *attr_list)
{
    int i, w;
    _brcm_sai_qos_wred_t wred_p;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_WRED);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    
    w = BRCM_SAI_GET_OBJ_VAL(int, wred_id);
    if (_BRCM_SAI_MAX_WRED < w)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred id.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_WRED_PROF,
                                    &w, &data);
    BRCM_SAI_RV_CHK(SAI_API_WRED, "wred data get", rv);
    wred_p = data.wred_prof;
    if (FALSE == wred_p.valid)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred obj id.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_WRED_ATTR_GREEN_ENABLE:
                attr_list[i].value.booldata = 
                    (wred_p.discard_g.discard.flags & BCM_COSQ_DISCARD_ENABLE) ?
                    TRUE : FALSE;
                break;
            case SAI_WRED_ATTR_YELLOW_ENABLE:
                attr_list[i].value.booldata = 
                    (wred_p.discard_y.discard.flags & BCM_COSQ_DISCARD_ENABLE) ?
                    TRUE : FALSE;
                break;
            case SAI_WRED_ATTR_RED_ENABLE:
                attr_list[i].value.booldata = 
                    (wred_p.discard_r.discard.flags & BCM_COSQ_DISCARD_ENABLE) ?
                    TRUE : FALSE;
                break;
            case SAI_WRED_ATTR_GREEN_MIN_THRESHOLD:
                attr_list[i].value.u32 = wred_p.discard_g.discard.min_thresh;
                break;
            case SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD:
                 attr_list[i].value.u32 = wred_p.discard_y.discard.min_thresh;
                break;
            case SAI_WRED_ATTR_RED_MIN_THRESHOLD:
                 attr_list[i].value.u32 = wred_p.discard_r.discard.min_thresh;
                break;
            case SAI_WRED_ATTR_GREEN_MAX_THRESHOLD:
                 attr_list[i].value.u32 = wred_p.discard_g.discard.max_thresh;
                break;
            case SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD:
                 attr_list[i].value.u32 = wred_p.discard_y.discard.max_thresh;
                break;
            case SAI_WRED_ATTR_RED_MAX_THRESHOLD:
                 attr_list[i].value.u32 = wred_p.discard_r.discard.max_thresh;
                break;
            case SAI_WRED_ATTR_GREEN_DROP_PROBABILITY:
                 attr_list[i].value.u32 = wred_p.discard_g.discard.drop_probability;
                break;
            case SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY:
                 attr_list[i].value.u32 = wred_p.discard_y.discard.drop_probability;
                break;
            case SAI_WRED_ATTR_RED_DROP_PROBABILITY:
                 attr_list[i].value.u32 = wred_p.discard_r.discard.drop_probability;
                break;
            case SAI_WRED_ATTR_WEIGHT:
                attr_list[i].value.u8 = wred_p.gn;
                break;
            case SAI_WRED_ATTR_ECN_MARK_MODE:
                if (FALSE == wred_p.ect)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_NONE;
                    break;
                }
                if (wred_p.discard_g.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION &&
                    wred_p.discard_y.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION &&
                    wred_p.discard_r.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_ALL;
                }
                else if (wred_p.discard_g.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION &&
                         wred_p.discard_y.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_GREEN_YELLOW;
                }
                else if (wred_p.discard_g.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION &&
                         wred_p.discard_r.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_GREEN_RED;
                }
                else if (wred_p.discard_y.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION &&
                         wred_p.discard_r.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_YELLOW_RED;
                }
                else if (wred_p.discard_g.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_GREEN;
                }
                else if (wred_p.discard_y.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_YELLOW;
                }
                else if (wred_p.discard_r.discard.flags & BCM_COSQ_DISCARD_MARK_CONGESTION)
                {
                    attr_list[i].value.u32 = SAI_ECN_MARK_MODE_RED;
                }
                break;
            default:
                BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR,
                                  "Unknown WRED attribute %d passed\n", attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }        

    BRCM_SAI_FUNCTION_EXIT(SAI_API_WRED);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
sai_status_t 
_brcm_sai_alloc_wred()
{
    sai_status_t rv;

    rv =  _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_WRED_PROF,
                                      _BRCM_SAI_MAX_WRED+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_WRED, SAI_LOG_LEVEL_CRITICAL,
                        "initializing wred profile data", rv);
    return SAI_STATUS_SUCCESS;
}

sai_status_t 
_brcm_sai_free_wred()
{
    sai_status_t rv;
    
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_WRED_PROF, 
                                      1, _BRCM_SAI_MAX_WRED+1, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "freeing wred profile data", rv);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_wred_discard_get(int id, uint8_t *type, _brcm_sai_qos_wred_t *wred_p)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    if (_BRCM_SAI_MAX_WRED < id)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred id.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_WRED_PROF,
                                    &id, &data);
    BRCM_SAI_RV_CHK(SAI_API_WRED, "wred data get", rv);
    *wred_p = data.wred_prof;
    if (FALSE == wred_p->valid)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred obj id.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    *type = wred_p->valid;
    return SAI_STATUS_SUCCESS;
}


/* 
   Called when profile is applied to WRED 
*/
sai_status_t
_brcm_sai_wred_discard_update(int id, _brcm_sai_qos_wred_t *wred_p)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    
    if (_BRCM_SAI_MAX_WRED < id)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Invalid wred id.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    data.wred_prof = *wred_p;
    
    /* write it back */
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_WRED_PROF, &id, &data);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_ERROR, "Unable to save wred prof data.\n");
    }
    return rv;
}


/*
  Called when set attribute changes an existing profile 
 */
sai_status_t
_brcm_sai_reapply_wred_profile(sai_object_id_t wred_id,
                               _brcm_sai_qos_wred_t* wred_p)
{
    sai_status_t rv;
    int i;
    int queue;
    _brcm_sai_queue_bmap_t temp;
    
    for (i=0; i<_BRCM_SAI_MAX_PORTS;i++)
    {
        if (wred_p->port_data[i] == 0)
        {
            continue;
        }
        temp = wred_p->port_data[i];
        queue = 0;
        while (temp)
        {
            if (temp & 0x1)
            {                
                rv = _brcm_sai_qos_queue_wred_set(i, queue,  wred_p);
                BRCM_SAI_RV_CHK(SAI_API_WRED, "Apply queue config", rv);
                
                BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_DEBUG,
                                  "Applied wred profile on port %d qid %d\n",
                                  i, queue);
            }
            temp >>= 1;
            queue++;
        }
    }
    return SAI_STATUS_SUCCESS;
}


/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_wred_api_t wred_apis = {
    brcm_sai_create_wred,
    brcm_sai_remove_wred,
    brcm_sai_set_wred_attribute,
    brcm_sai_get_wred_attribute
};
