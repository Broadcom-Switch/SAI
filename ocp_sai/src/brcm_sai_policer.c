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
STATIC sai_status_t
_brcm_sai_policer_action_delete(bcm_policer_t pol_id);

/*
################################################################################
#                              Policer functions                               #
################################################################################
*/

/*
* Routine Description:
*   Create Policer
*
* Arguments:
*   [out] policer_id - the policer id
*   [in] attr_count - number of attributes
*   [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_policer(_Out_ sai_object_id_t *policer_id,
                        _In_ sai_object_id_t switch_id,
                        _In_ uint32_t attr_count,
                        _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, pkt_byt = -1, gpa = -1, ypa = -1, rpa = -1;
    bcm_policer_t pol_id;
    bcm_policer_mode_t mode = -1;
    bcm_policer_config_t pol_cfg;
    uint64 cbs = 0, cir = 0, pbs = 0, pir = 0;
    uint32 flags = 0;
    uint8_t act = 0;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_POLICER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(policer_id);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_POLICER_ATTR_METER_TYPE:
                if (SAI_METER_TYPE_PACKETS == attr_list[i].value.s32)
                {
                    flags |= BCM_POLICER_MODE_PACKETS;
                    pkt_byt = SAI_METER_TYPE_PACKETS;
                }
                else if (SAI_METER_TYPE_BYTES == attr_list[i].value.s32)
                {
                    flags |= BCM_POLICER_MODE_BYTES;
                    pkt_byt = SAI_METER_TYPE_BYTES;
                }
                break;
            case SAI_POLICER_ATTR_MODE:
                switch (attr_list[i].value.s32)
                {
                    case SAI_POLICER_MODE_SR_TCM:
                        mode = bcmPolicerModeSrTcm;
                        break;
                    case SAI_POLICER_MODE_TR_TCM:
                        mode = bcmPolicerModeTrTcm;
                        break;
                    case SAI_POLICER_MODE_STORM_CONTROL:
                        mode = bcmPolicerModeCommitted;
                        break;
                    default: break;
                }
                break;
            default: break;
        }
        if ((-1 != pkt_byt) && (-1 != mode))
        {
            break;
        }
    }
    if (-1 == pkt_byt)
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, "No meter type found\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (-1 == mode)
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, "No policer mode specified\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_POLICER_ATTR_METER_TYPE:
                break;
            case SAI_POLICER_ATTR_MODE:
                break;
            case SAI_POLICER_ATTR_COLOR_SOURCE:
                if (SAI_POLICER_COLOR_SOURCE_BLIND == attr_list[i].value.s32)
                {
                    flags |= BCM_POLICER_COLOR_BLIND;
                }
                break;
            case SAI_POLICER_ATTR_CBS: 
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(pkt_byt, attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, 
                                         "Policer committed burst size %lu > max allowed.\n", 
                                          attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                if (bcmPolicerModeCommitted == mode) 
                {
                    if ((SAI_METER_TYPE_PACKETS == pkt_byt) && attr_list[i].value.u64)
                    {
                        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, 
                                             "CBS not supported for Storm Control Policers "
                                             "in packet mode.\n");
                        return SAI_STATUS_NOT_SUPPORTED;
                    }
                    else if ((SAI_METER_TYPE_BYTES == pkt_byt) &&
                             (_BRCM_SAI_STORMCONTROL_MAX_CBS < attr_list[i].value.u64))
                    {
                        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, 
                                             "Storm Control Policer CBS %lu > max supported "
                                             "in byte mode.\n",
                                             attr_list[i].value.u64);
                        return SAI_STATUS_NOT_SUPPORTED;
                    }
                }
                cbs = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                      attr_list[i].value.u64 : 8 * attr_list[i].value.u64;
                break;
            case SAI_POLICER_ATTR_CIR:
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(pkt_byt, attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, 
                                         "Policer committed information rate %lu > max allowed.\n", 
                                          attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                if (bcmPolicerModeCommitted == mode) 
                {
                    if ((SAI_METER_TYPE_PACKETS == pkt_byt) &&
                        (_BRCM_SAI_STORMCONTROL_MAX_PPS < attr_list[i].value.u64))
                    {
                        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, 
                                             "Storm Control Policer CIR %lu > max supported "
                                             "in packet mode.\n",
                                             attr_list[i].value.u64);
                        return SAI_STATUS_NOT_SUPPORTED;
                    }
                }
                cir = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                      attr_list[i].value.u64 : 8 * attr_list[i].value.u64;
                break;
            case SAI_POLICER_ATTR_PBS:
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(pkt_byt, attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, 
                                         "Policer peak burst size %lu > max allowed.\n", 
                                          attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                pbs = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                      attr_list[i].value.u64 : 8 * attr_list[i].value.u64;
                break;
            case SAI_POLICER_ATTR_PIR:
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(pkt_byt, attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, 
                                         "Policer peak information rate %lu > max allowed.\n", 
                                          attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                pir = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                      attr_list[i].value.u64 : 8 * attr_list[i].value.u64;
                break;
            case SAI_POLICER_ATTR_GREEN_PACKET_ACTION:
                gpa = attr_list[i].value.s32;
                break;
            case SAI_POLICER_ATTR_YELLOW_PACKET_ACTION:
                ypa = attr_list[i].value.s32;
                break;
            case SAI_POLICER_ATTR_RED_PACKET_ACTION:
                rpa = attr_list[i].value.s32;
                break;
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_INFO,
                                     "Unknown policer attribute %d passed\n",
                                     attr_list[i].id);
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_INFO,
                                 "Error processing policer attributes\n");
            return rv;
        }
    }
    
    bcm_policer_config_t_init(&pol_cfg);
    pol_cfg.mode = mode;
    pol_cfg.flags = flags;
    pol_cfg.ckbits_sec = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                         cir : cir/1000;
    pol_cfg.ckbits_burst = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                           cbs: cbs/1000;
    pol_cfg.pkbits_sec = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                         pir : pir/1000;
    pol_cfg.pkbits_burst = (SAI_METER_TYPE_PACKETS == pkt_byt) ?
                           pbs : pbs/1000;
    rv = bcm_policer_create(0, &pol_cfg, &pol_id);
    BRCM_SAI_API_CHK(SAI_API_POLICER, "policer create", rv);
    if ((-1 != gpa) || (-1 != ypa) || (-1 != rpa))
    {
        _brcm_sai_list_data_t data;
        _brcm_sai_policer_action_t action;
        
        memset(&action, 0, sizeof(action));
        if (-1 != gpa)
        {
            act |= 0x1;
            action.gpa = gpa;
        }
        if (-1 != ypa)
        {
            act |= 0x2;
            action.ypa = ypa;
        }
        if (-1 != rpa)
        {
            act |= 0x4;
            action.rpa = rpa;
        }
        action.pol_id = pol_id;
        data.policer_action = &action;
        rv = _brcm_sai_list_add(_BRCM_SAI_LIST_POLICER_ACTION, NULL, 
                                NULL, &data);
        BRCM_SAI_RV_CHK(SAI_API_POLICER, "policer action list add", rv);
        /* Note: Only counting policers with actions */
        if (SAI_STATUS_SUCCESS != 
            _brcm_sai_global_data_bump(_BRCM_SAI_POLICER_COUNT, INC))
        {
            BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR,
                                 "Error incrementing policer count global data.\n");
            return SAI_STATUS_FAILURE;
        }
    }
    *policer_id = BRCM_SAI_CREATE_OBJ_SUB(SAI_OBJECT_TYPE_POLICER, act, pol_id);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_POLICER);
    return rv;
}

/*
* Routine Description:
*   Delete policer
*
* Arguments:
 *  [in] policer_id - Policer id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_policer(_In_ sai_object_id_t policer_id)
{
    sai_status_t rv;
    _brcm_sai_list_data_t data;
    _brcm_sai_list_key_t list_key;
    uint8_t act = BRCM_SAI_GET_OBJ_SUB_TYPE(policer_id);
    bcm_policer_t pol_id = BRCM_SAI_GET_OBJ_VAL(uint32_t, policer_id);

    BRCM_SAI_FUNCTION_ENTER(SAI_API_POLICER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    
    list_key.pol_id = pol_id;
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_POLICER_ACTION, NULL, &list_key,
                            &data);
    if (SAI_STATUS_ERROR(rv) && (SAI_STATUS_ITEM_NOT_FOUND != rv))
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, "Error in policer action list get.");
        return rv;
    }
    else if ((SAI_STATUS_ITEM_NOT_FOUND != rv) && data.policer_action->ref_count)
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, "Policer %d in use ref count(%d).",
                             pol_id, data.policer_action->ref_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    rv = _brcm_sai_policer_action_delete(pol_id);
    if (SAI_STATUS_SUCCESS == rv)
    {
        rv = bcm_policer_destroy(0, pol_id);
        BRCM_SAI_API_CHK(SAI_API_POLICER, "policer destroy", rv);
    }
    if (act)
    {
        if (SAI_STATUS_SUCCESS != 
            _brcm_sai_global_data_bump(_BRCM_SAI_POLICER_COUNT, DEC))
        {
            BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR,
                                 "Error decrementing policer count global data.\n");
            return SAI_STATUS_FAILURE;
        }
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_POLICER);
    return rv;
}

/*
* Routine Description:
*   Set Policer attribute
*
* Arguments:
*    [in] policer_id - Policer id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_policer_attribute(_In_ sai_object_id_t policer_id,
                               _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_POLICER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_POLICER);
    return rv;
}

/*
* Routine Description:
*   Get Policer attribute
*
* Arguments:
*    [in] policer_id - policer id
*    [in] attr_count - number of attributes
*    [Out] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_policer_attribute(_In_ sai_object_id_t policer_id,
                               _In_ uint32_t attr_count,
                               _Out_ sai_attribute_t *attr_list)
{
    int i, pkt_byt;
    sai_status_t rv;
    bcm_policer_config_t pol_cfg;
    bcm_policer_t pol_id;
    _brcm_sai_list_data_t data;
    _brcm_sai_list_key_t list_key;
    bool policer_actions_found = TRUE; 

    BRCM_SAI_FUNCTION_ENTER(SAI_API_POLICER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    
    pol_id = BRCM_SAI_GET_OBJ_VAL(bcm_policer_t, policer_id);
    rv = bcm_policer_get(0, pol_id, &pol_cfg);
    BRCM_SAI_API_CHK(SAI_API_POLICER, "policer get", rv);
    list_key.pol_id = pol_id;
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_POLICER_ACTION, NULL, &list_key,
                            &data);
    if (SAI_STATUS_ERROR(rv) && (SAI_STATUS_ITEM_NOT_FOUND != rv))
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, "Error in policer action list get.");
        return rv;
    }
    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        policer_actions_found = FALSE;
        rv = SAI_STATUS_SUCCESS;
    }

    pkt_byt = (BCM_POLICER_MODE_BYTES & pol_cfg.flags) ?
              SAI_METER_TYPE_BYTES : SAI_METER_TYPE_PACKETS;
    for (i = 0; i < attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_POLICER_ATTR_METER_TYPE:
                attr_list[i].value.s32 = pkt_byt;
                break;
            case SAI_POLICER_ATTR_MODE:
                switch (pol_cfg.mode)
                {
                    case bcmPolicerModeSrTcm:
                        attr_list[i].value.s32 = SAI_POLICER_MODE_SR_TCM;
                        break;
                    case bcmPolicerModeTrTcm:
                        attr_list[i].value.s32 = SAI_POLICER_MODE_TR_TCM;
                        break;
                    case bcmPolicerModeCommitted:
                        attr_list[i].value.s32 = SAI_POLICER_MODE_STORM_CONTROL;
                        break;
                    default: break;
                }
                break;
            case SAI_POLICER_ATTR_COLOR_SOURCE:
                if (pol_cfg.flags & BCM_POLICER_COLOR_BLIND)
                {
                    attr_list[i].value.s32 = SAI_POLICER_COLOR_SOURCE_BLIND;
                }
                else
                {
                    attr_list[i].value.s32 = SAI_POLICER_COLOR_SOURCE_AWARE;
                }
                break;
            case SAI_POLICER_ATTR_CBS:
                attr_list[i].value.u64 = (SAI_METER_TYPE_PACKETS == pkt_byt) ? 
                                         pol_cfg.ckbits_burst : 
                                         pol_cfg.ckbits_burst/8;
                break;
            case SAI_POLICER_ATTR_CIR:
                attr_list[i].value.u64 = (SAI_METER_TYPE_PACKETS == pkt_byt) ? 
                                         pol_cfg.ckbits_sec : 
                                         pol_cfg.ckbits_sec/8;
                break;
            case SAI_POLICER_ATTR_PBS:
                attr_list[i].value.u64 = (SAI_METER_TYPE_PACKETS == pkt_byt) ? 
                                         pol_cfg.pkbits_burst : 
                                         pol_cfg.pkbits_burst/8;
                break;
            case SAI_POLICER_ATTR_PIR:
                attr_list[i].value.u64 = (SAI_METER_TYPE_PACKETS == pkt_byt) ? 
                                         pol_cfg.pkbits_sec : 
                                         pol_cfg.pkbits_sec/8;
                break;
            case SAI_POLICER_ATTR_GREEN_PACKET_ACTION:
                if (TRUE == policer_actions_found)
                {
                    attr_list[i].value.s32 = data.policer_action->gpa;
                }
                else
                {
                    attr_list[i].value.s32 = -1;
                }
                break;
            case SAI_POLICER_ATTR_YELLOW_PACKET_ACTION:
                if (TRUE == policer_actions_found)
                {
                    attr_list[i].value.s32 = data.policer_action->ypa;
                }
                else
                {
                    attr_list[i].value.s32 = -1;
                }
                break;
            case SAI_POLICER_ATTR_RED_PACKET_ACTION:
                if (TRUE == policer_actions_found)
                {
                    attr_list[i].value.s32 = data.policer_action->rpa;
                }
                else
                {
                    attr_list[i].value.s32 = -1;
                }
                break;
            default:
                BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR,
                                       "Unknown policer attribute %d passed\n",
                                       attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_INFO,
                                   "Error processing policer attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_POLICER);
    return rv;
}

/**
 * @brief  Get Policer Statistics
 *
 * @param[in] policer_id - policer id
 * @param[in] counter_ids - array of counter ids
 * @param[in] number_of_counters - number of counters in the array
 * @param[out] counters - array of resulting counter values.
 *
 * @return SAI_STATUS_SUCCESS on success
 *         Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_policer_stats(_In_ sai_object_id_t policer_id,
                           _In_ uint32_t number_of_counters,
                           _In_ const sai_policer_stat_t *counter_ids,
                           _Out_ uint64_t* counters)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_POLICER);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_POLICER);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_policer()
{
    sai_status_t rv;
    _brcm_sai_data_t data;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_policer_action_t *action;

    /* Get policer count from global data */
    rv = _brcm_sai_global_data_get(_BRCM_SAI_POLICER_COUNT, &data);
    BRCM_SAI_RV_CHK(SAI_API_POLICER, "policer count get", rv);
    /* Load policers */
    if (!data.u32)
    {
        return SAI_STATUS_SUCCESS;
    }
    else
    {
        rv = _brcm_sai_list_init(_BRCM_SAI_LIST_POLICER_ACTION,
                                 0, data.u32, NULL);
        BRCM_SAI_RV_CHK(SAI_API_POLICER, "list init policers", rv);
    }
    /* Traverse policers to find which of them had action refs */
    ldata.policer_action = NULL;    
    while(SAI_STATUS_SUCCESS ==
          _brcm_sai_list_traverse(_BRCM_SAI_LIST_POLICER_ACTION, &ldata, &ldata))
    {
        action = ldata.policer_action;
        if (action->ref_count)
        {
            /* Load action refs */
            rv = _brcm_sai_list_init(_BRCM_SAI_LIST_POLICER_OID_MAP, action->pol_id,
                                     action->ref_count, (void**)&action->oids);
            BRCM_SAI_RV_CHK(SAI_API_POLICER, "list init policer ref", rv);
        }
    }
    
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_policer()
{
    sai_status_t rv;
    _brcm_sai_data_t data;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_policer_action_t *action;
    _brcm_sai_policer_oid_map_t *oids;
    
    /* Get policer count from global data */
    rv = _brcm_sai_global_data_get(_BRCM_SAI_POLICER_COUNT, &data);
    BRCM_SAI_RV_CHK(SAI_API_POLICER, "policer count get", rv);
    /* Traverse policers */
    ldata.policer_action = NULL;    
    while(SAI_STATUS_SUCCESS ==
          _brcm_sai_list_traverse(_BRCM_SAI_LIST_POLICER_ACTION, &ldata, &ldata))
    {
        action = ldata.policer_action;
        oids = action->oids;
        if (oids)
        {
            /* Free all action refs and mark the policers which have action refs */
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_POLICER_OID_MAP,
                                     action->pol_id, action->ref_count, oids);
            BRCM_SAI_RV_CHK(SAI_API_POLICER, "list free policer action refs", rv);
            action->oids = (_brcm_sai_policer_oid_map_t*)(uint64_t)action->pol_id;
        }
    }
    if (data.u32)
    {
        /* Free all policers */
        rv = _brcm_sai_list_free(_BRCM_SAI_LIST_POLICER_ACTION, 0, data.u32, NULL);
        BRCM_SAI_RV_CHK(SAI_API_POLICER, "list free policer actions", rv);
    }
    
    return SAI_STATUS_SUCCESS;
}

int
_brcm_sai_storm_info_get(sai_object_id_t policer_id, bool *pkt_mode, int *cir,
                         int *cbs)
{
    int rv;
    bcm_policer_config_t pol_cfg;
    bcm_policer_t pol_id = BRCM_SAI_GET_OBJ_VAL(bcm_policer_t,
                                                    policer_id);

    rv = bcm_policer_get(0, pol_id, &pol_cfg);
    BRCM_SAI_API_CHK(SAI_API_POLICER, "policer get", rv);
    
    *pkt_mode = (BCM_POLICER_MODE_BYTES & pol_cfg.flags) ? FALSE : TRUE;
    *cir = pol_cfg.ckbits_sec;
    *cbs = pol_cfg.ckbits_burst;
    return rv;
}

sai_status_t
_brcm_sai_policer_actions_get(sai_object_id_t policer_id,
                              sai_packet_action_t *gpa,
                              sai_packet_action_t *ypa,
                              sai_packet_action_t *rpa)
{
    sai_status_t rv;
    _brcm_sai_list_data_t data;
    _brcm_sai_list_key_t list_key;
    _brcm_sai_policer_action_t *action;

    list_key.pol_id = BRCM_SAI_GET_OBJ_VAL(bcm_policer_t, policer_id);
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_POLICER_ACTION, NULL, &list_key,
                            &data);
    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_DEBUG, "No policer action found for: %d.\n",
                             list_key.pol_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    action = data.policer_action;
    *gpa = action->gpa;
    *ypa = action->ypa;
    *rpa = action->rpa;

    return SAI_STATUS_SUCCESS;
}

int
_brcm_sai_policer_action_ref_attach(sai_object_id_t policer_id,
                                    sai_object_id_t oid)
{
    sai_status_t rv;
    _brcm_sai_list_key_t list_key;
    _brcm_sai_list_data_t data, base;
    _brcm_sai_policer_action_t *action;

    list_key.pol_id = BRCM_SAI_GET_OBJ_VAL(bcm_policer_t, policer_id);
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_POLICER_ACTION, NULL, &list_key,
                            &data);
    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_DEBUG, "No policer action found for: %d.\n",
                             list_key.pol_id);
        return 1;
    }
    action = data.policer_action;
    action->ref_count++;

    base.oid_map = &action->oids;
    list_key.obj_id = oid;
    rv = _brcm_sai_list_add(_BRCM_SAI_LIST_POLICER_OID_MAP, &base, &list_key,
                            NULL);
    BRCM_SAI_RV_CHK(SAI_API_POLICER, "policer acl map list add", rv);

    return 0;
}

void
_brcm_sai_policer_action_ref_detach(sai_object_id_t oid)
{
    _brcm_sai_policer_oid_map_t *prev, *oids;
    _brcm_sai_policer_action_t *action;
    _brcm_sai_list_data_t data;

    data.policer_action = NULL;    
    while(SAI_STATUS_SUCCESS ==
          _brcm_sai_list_traverse(_BRCM_SAI_LIST_POLICER_ACTION, &data, &data))
    {
        action = data.policer_action;
        oids = action->oids;
        /* FIXME: The following could also be moved into _brcm_sai_list_del()
         * as type _BRCM_SAI_LIST_POLICER_OID_MAP.
         * With some complications of decrementing ref_counts.
         */
        if (!IS_NULL(oids))
        {
            do
            {
                if (oids->oid == oid)
                {
                    /* Node found - delete and update ref count */
                    if (action->ref_count)
                    {
                        action->ref_count--;
                    }
                    /* first node */
                    if (oids == action->oids)
                    {
                        action->oids = oids->next;
                    }
                    else
                    {
                        prev->next = oids->next;
                    }
                    _brcm_sai_dm_free(oids);
                    return;
                }
                prev = oids;
                oids = oids->next;
            } while (!IS_NULL(oids));
        }
    }
}

STATIC sai_status_t
_brcm_sai_policer_action_delete(bcm_policer_t pol_id)
{
    sai_status_t rv;
    _brcm_sai_list_key_t list_key;

    list_key.pol_id = pol_id;
    rv = _brcm_sai_list_del(_BRCM_SAI_LIST_POLICER_ACTION, NULL, &list_key);
    if (SAI_STATUS_ERROR(rv) && (SAI_STATUS_ITEM_NOT_FOUND != rv))
    {
        BRCM_SAI_LOG_POLICER(SAI_LOG_LEVEL_ERROR, "Error in policer action list delete.");
        return rv;
    }
    
    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_policer_api_t policer_apis = {
    brcm_sai_create_policer,
    brcm_sai_remove_policer,
    brcm_sai_set_policer_attribute,
    brcm_sai_get_policer_attribute,
    brcm_sai_get_policer_stats,
    NULL
};
