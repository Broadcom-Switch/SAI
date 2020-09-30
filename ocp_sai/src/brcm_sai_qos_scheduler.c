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
#                                  Scheduler functions                         #
################################################################################
*/

/**
 * @brief  Create Scheduler Profile
 *
 * @param[out] scheduler_id Scheduler id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list array of attributes
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_scheduler(_Out_ sai_object_id_t *scheduler_id,
                          _In_ sai_object_id_t switch_id,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list)
{
    int i, s;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_qos_scheduler_t *scheduler = NULL;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(scheduler_id);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_SAI_SCHEDULER_COUNT, &data))
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR,
                               "Error getting scheduler count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    if (_BRCM_SAI_MAX_SCHEDULER_PROFILES - 1 < data.u32)
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, "Max scheduler profiles reached.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_SCHED_PROF, 1,
                                              _BRCM_SAI_MAX_SCHEDULER_PROFILES,
                                              &s);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data index reserve", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &s, &idata);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data get", rv);
    idata.scheduler_prof.algorithm = _BRCM_SAI_SCHEDULER_ATTR_INVALID;
    idata.scheduler_prof.shaper_type = SAI_METER_TYPE_BYTES;
    scheduler = &idata.scheduler_prof;

    for (i = 0; i < attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_SCHEDULER_ATTR_SCHEDULING_TYPE:
                if ((SAI_SCHEDULING_TYPE_STRICT != attr_list[i].value.s32) &&
                    (SAI_SCHEDULING_TYPE_WRR != attr_list[i].value.s32) &&
                    (SAI_SCHEDULING_TYPE_DWRR != attr_list[i].value.s32))
                {
                    BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                           "Invalid Scheduler Mode: %d.\n", 
                                           attr_list[i].value.s32);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                scheduler->algorithm = attr_list[i].value.s32;
                break;
            case SAI_SCHEDULER_ATTR_SCHEDULING_WEIGHT:
                if ((_BRCM_SAI_SCHEDULER_WEIGHT_MIN > attr_list[i].value.u8) ||
                    (_BRCM_SAI_SCHEDULER_WEIGHT_MAX < attr_list[i].value.u8))
                {
                    BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                           "Scheduler weight %d outside of "
                                           "valid range 1 - 100.\n", 
                                           attr_list[i].value.u8);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                scheduler->weight = attr_list[i].value.u8;
                break;
            case SAI_SCHEDULER_ATTR_MIN_BANDWIDTH_RATE:
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                                   attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                           "Scheduler minimum bandwidth %lu > max allowed.\n", 
                                           attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                scheduler->minimum_bandwidth = 
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    attr_list[i].value.u64 : attr_list[i].value.u64 * 8 / 1000;
                break;
            case SAI_SCHEDULER_ATTR_MIN_BANDWIDTH_BURST_RATE:
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                                   attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                           "Scheduler minimum burst %lu > max allowed.\n", 
                                           attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                scheduler->minimum_burst = 
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    attr_list[i].value.u64 : attr_list[i].value.u64 * 8 / 1000;
                break;
            case SAI_SCHEDULER_ATTR_MAX_BANDWIDTH_RATE:
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                                   attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                           "Scheduler maximum bandwidth %lu > max allowed.\n", 
                                           attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                scheduler->maximum_bandwidth = 
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    attr_list[i].value.u64 : attr_list[i].value.u64 * 8 / 1000;
                break;
            case SAI_SCHEDULER_ATTR_MAX_BANDWIDTH_BURST_RATE:
                if (SAI_STATUS_FAILURE == 
                        _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                                   attr_list[i].value.u64))
                {
                    BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                           "Scheduler maximum burst %lu > max allowed.\n", 
                                           attr_list[i].value.u64);
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                scheduler->maximum_burst = 
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    attr_list[i].value.u64 : attr_list[i].value.u64 * 8 / 1000;
                break;
            default:
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_INFO,
                                       "Unknown qos schedular attribute %d passed\n",
                                       attr_list[i].id);
                break;
        }
    }   
    if (((SAI_SCHEDULING_TYPE_WRR == scheduler->algorithm) ||
         (SAI_SCHEDULING_TYPE_DWRR == scheduler->algorithm)) &&
         (0 == scheduler->weight))
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                               "Weighted Scheduler algorithm must have non-zero "
                               "weight assigned.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    idata.scheduler_prof.idx = s;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &s, &idata);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data set", rv);
    *scheduler_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_SCHEDULER, s);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_SAI_SCHEDULER_COUNT, INC))
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR,
                               "Error incrementing scheduler count global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER);

    return rv;
}

/**
 * @brief  Remove Scheduler profile
 *
 * @param[in] scheduler_id Scheduler id
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_scheduler(_In_ sai_object_id_t scheduler_id)
{
    sai_status_t rv;
    int scheduler_index;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_qos_scheduler_t *scheduler;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    
    scheduler_index = BRCM_SAI_GET_OBJ_VAL(int, scheduler_id);
    if (_BRCM_SAI_MAX_SCHEDULER_PROFILES < scheduler_index)
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, "Invalid scheduler id.\n");
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &scheduler_index, &data);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data get", rv);
    
    scheduler = &data.scheduler_prof;
    if (scheduler->ref_count)
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, "Scheduler still in use.\n");
        return SAI_STATUS_OBJECT_IN_USE;
    }
    while (NULL != scheduler->object_list)
    {
        if (SAI_STATUS_SUCCESS != 
            _brcm_sai_scheduler_detach_object(scheduler_index, scheduler,
                                              scheduler->object_list->object_id, TRUE))
        {
            return SAI_STATUS_FAILURE;
        }
    }
    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_SCHED_PROF,
                                      scheduler_index);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data free", rv);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_SAI_SCHEDULER_COUNT, DEC))
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR,
                               "Error decrementing scheduler count global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER);
    return rv;
}

/**
 * @brief  Set Scheduler Attribute
 *
 * @param[in] scheduler_id Scheduler id
 * @param[in] attr attribute to set
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_scheduler_attribute(_In_ sai_object_id_t scheduler_id,
                                 _In_ const sai_attribute_t *attr)
{
    int id;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_qos_scheduler_t *scheduler;
    _brcm_sai_scheduler_object_t *current_object;
    sai_object_id_t object_type;
    sai_attribute_t sai_attr;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    id = BRCM_SAI_GET_OBJ_VAL(int, scheduler_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &id, &data);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data get", rv);
    scheduler = &data.scheduler_prof;
    switch (attr->id)
    {
        case SAI_SCHEDULER_ATTR_SCHEDULING_TYPE:
                scheduler->algorithm = attr->value.s32;
            break;
        case SAI_SCHEDULER_ATTR_SCHEDULING_WEIGHT:
            if ((attr->value.u8 < _BRCM_SAI_SCHEDULER_WEIGHT_MIN) ||
                (attr->value.u8 > _BRCM_SAI_SCHEDULER_WEIGHT_MAX))
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Scheduler weight %d outside of valid "
                                       "range 1 - 100.\n", 
                                       attr->value.u8);
                return SAI_STATUS_INVALID_PARAMETER;
            }
            scheduler->weight = attr->value.u8;
            break;
        case SAI_SCHEDULER_ATTR_MIN_BANDWIDTH_RATE:
            if (SAI_STATUS_FAILURE == 
                    _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                               attr->value.u64))
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Scheduler minimum bandwidth %lu > max allowed.\n", 
                                       attr->value.u64);
                return SAI_STATUS_INVALID_PARAMETER;
            }
            scheduler->minimum_bandwidth = 
                (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                attr->value.u64 : attr->value.u64 * 8 / 1000;
            break;
        case SAI_SCHEDULER_ATTR_MIN_BANDWIDTH_BURST_RATE:
            if (SAI_STATUS_FAILURE == 
                    _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                               attr->value.u64))
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Scheduler minimum burst %lu > max allowed.\n", 
                                       attr->value.u64);
                return SAI_STATUS_INVALID_PARAMETER;
            }
            scheduler->minimum_burst = 
                (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                attr->value.u64 : attr->value.u64 * 8 / 1000;
            break;
        case SAI_SCHEDULER_ATTR_MAX_BANDWIDTH_RATE:
            if (SAI_STATUS_FAILURE == 
                    _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                               attr->value.u64))
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Scheduler maximum bandwidth %lu > max allowed.\n", 
                                       attr->value.u64);
                return SAI_STATUS_INVALID_PARAMETER;
            }
            scheduler->maximum_bandwidth = 
                (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                attr->value.u64 : attr->value.u64 * 8 / 1000;
            break;
        case SAI_SCHEDULER_ATTR_MAX_BANDWIDTH_BURST_RATE:
            if (SAI_STATUS_FAILURE == 
                    _brcm_sai_32bit_size_check(scheduler->shaper_type, 
                                               attr->value.u64))
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Scheduler maximum burst %lu > max allowed.\n", 
                                       attr->value.u64);
                return SAI_STATUS_INVALID_PARAMETER;
            }
            scheduler->maximum_burst = 
                (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                attr->value.u64 : attr->value.u64 * 8 / 1000;
            break;
        default:
            BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                   "Unknown qos scheduler attribute %d passed\n",
                                   attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
            return rv;
    }

    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &id, &data);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data set", rv);

    current_object = scheduler->object_list;
    sai_attr.value.oid = scheduler_id;
    while (NULL != current_object)
    {
        object_type = BRCM_SAI_GET_OBJ_TYPE(current_object->object_id);
        if (SAI_OBJECT_TYPE_PORT == object_type)
        {
            sai_attr.id = SAI_PORT_ATTR_QOS_SCHEDULER_PROFILE_ID;
            rv = port_apis.set_port_attribute(current_object->object_id, &sai_attr);
            if (SAI_STATUS_SUCCESS != rv)
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Unable to set port attribute %lu for port id %lx, "
                                       "scheduler id %lx\n", attr->value.u64, 
                                       current_object->object_id,
                                       scheduler_id);
                return rv;
            }
        }
        else if (SAI_OBJECT_TYPE_QUEUE == object_type)
        {
            sai_attr.id = SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID;
            rv = qos_apis.set_queue_attribute(current_object->object_id, &sai_attr);
            if (SAI_STATUS_SUCCESS != rv)
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Unable to set queue attribute %lu for queue id %lx, "
                                       "scheduler id %lx\n", attr->value.u64, 
                                       current_object->object_id,
                                       scheduler_id);
                return rv;
            }
        }
        else if (SAI_OBJECT_TYPE_SCHEDULER_GROUP == object_type)
        {
            sai_attr.id = SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID;
            rv = scheduler_group_apis.
                     set_scheduler_group_attribute(current_object->object_id,
                                                   &sai_attr);
            if (SAI_STATUS_SUCCESS != rv)
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                       "Unable to set scheduler group attribute "
                                       "%lu for sched group id %lx, "
                                       "scheduler id %lx\n", attr->value.u64, 
                                       current_object->object_id,
                                       scheduler_id);
                return rv;
            }
        }
        else
        {
            BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR,
                                   "Invalid object type 0x%16lx passed\n",
                                   current_object->object_id);
            return SAI_STATUS_INVALID_OBJECT_TYPE;
        }
        current_object = current_object->next;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER);
    return rv;
}

/**
 * @brief  Get Scheduler attribute
 *
 * @param[in] scheduler_id - scheduler id
 * @param[in] attr_count - number of attributes
 * @param[inout] attr_list - array of attributes
 *
 * @return SAI_STATUS_SUCCESS on success
 *        Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_scheduler_attribute(_In_ sai_object_id_t scheduler_id,
                                 _In_ uint32_t attr_count,
                                 _Inout_ sai_attribute_t *attr_list)
{
    int i, id;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_qos_scheduler_t *scheduler;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;

    id = BRCM_SAI_GET_OBJ_VAL(int, scheduler_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &id, &data);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data get", rv);
    scheduler = &data.scheduler_prof;
    for (i = 0; i < attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_SCHEDULER_ATTR_SCHEDULING_TYPE:
                 attr_list[i].value.s32 = scheduler->algorithm;
                break;
            case SAI_SCHEDULER_ATTR_SCHEDULING_WEIGHT:
                attr_list[i].value.u8 = scheduler->weight;
                break;
            case SAI_SCHEDULER_ATTR_MIN_BANDWIDTH_RATE:
                attr_list[i].value.u64 = 
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    scheduler->minimum_bandwidth : scheduler->minimum_bandwidth * 1000 / 8;
                break;
            case SAI_SCHEDULER_ATTR_MIN_BANDWIDTH_BURST_RATE:
                attr_list[i].value.u64 =
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    scheduler->minimum_burst : scheduler->minimum_burst * 1000 / 8;
                break;
            case SAI_SCHEDULER_ATTR_MAX_BANDWIDTH_RATE:
                attr_list[i].value.u64 =
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    scheduler->maximum_bandwidth : scheduler->maximum_bandwidth * 1000 / 8;
                break;
            case SAI_SCHEDULER_ATTR_MAX_BANDWIDTH_BURST_RATE:
                attr_list[i].value.u64 =
                    (SAI_METER_TYPE_PACKETS == scheduler->shaper_type) ?
                    scheduler->maximum_burst : scheduler->maximum_burst * 1000 / 8;
                break;
            default:
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR,
                                       "Unknown qos scheduler attribute %d passed\n",
                                       attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_INFO,
                                   "Error processing qos scheduler attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER);
    return rv;
}

/*
################################################################################
#                                Internal Scheduler functions                  #
################################################################################
*/
sai_status_t 
_brcm_sai_alloc_sched()
{
    sai_status_t rv;

    if ((rv =  _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_SCHED_PROF,
                                           _BRCM_SAI_MAX_SCHEDULER_PROFILES+1))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_CRITICAL,
                               "Error initializing scheduler profile data !!\n");
        return rv;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t 
_brcm_sai_free_sched()
{
    sai_status_t rv;

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_SCHED_PROF, 
                                      1, _BRCM_SAI_MAX_SCHEDULER_PROFILES+1,
                                      -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_SCHEDULER, SAI_LOG_LEVEL_CRITICAL,
                        "freeing scheduler profiles", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t 
_brcm_sai_alloc_sched_prof_refs()
{
    int s;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
                                              
    for (s = 1; s <= _BRCM_SAI_MAX_SCHEDULER_PROFILES; s++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_SCHED_PROF,
                                        &s, &data);
        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data get", rv);
        if (data.scheduler_prof.valid && data.scheduler_prof.object_list &&
            data.scheduler_prof.ref_count)
        {
            rv = _brcm_sai_list_init(_BRCM_SAI_LIST_SCHED_OBJ_MAP,
                                     s, data.scheduler_prof.ref_count,
                                     (void**)&data.scheduler_prof.object_list);
            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "list init sched prof refs", rv);
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_SCHED_PROF,
                                            &s, &data);
            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data set", rv);
        }
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_sched_prof_refs()
{
    int s;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
                                              
    for (s = 1; s <= _BRCM_SAI_MAX_SCHEDULER_PROFILES; s++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_SCHED_PROF,
                                        &s, &data);
        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data get", rv);
        if (data.scheduler_prof.valid && data.scheduler_prof.object_list)
        {
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_SCHED_OBJ_MAP,
                                     s, data.scheduler_prof.ref_count,
                                     data.scheduler_prof.object_list);
            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "list free sched prof refs", rv);
            data.scheduler_prof.object_list = (_brcm_sai_scheduler_object_t*)(uint64_t)s;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_SCHED_PROF,
                                            &s, &data);
            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data set", rv);
        }
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_scheduler_get(int id, _brcm_sai_qos_scheduler_t *sched)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &id, &data);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data get", rv);
    if (NULL == sched || FALSE == data.scheduler_prof.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    *sched = data.scheduler_prof;

    return SAI_STATUS_SUCCESS;
}

sai_status_t 
_brcm_sai_scheduler_attach_object(int id, _brcm_sai_qos_scheduler_t *scheduler, 
                                  sai_object_id_t object_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_scheduler_object_t *current = scheduler->object_list;
    _brcm_sai_scheduler_object_t *prev = NULL, *new_object;

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
    new_object = ALLOC_CLEAR(1, sizeof(_brcm_sai_scheduler_object_t));
    if (IS_NULL(new_object))
    {
        BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_CRITICAL,
                               "Error allocating memory for scheduler object list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_object->object_id = object_id;
    new_object->next = NULL;
    if (IS_NULL(current))
    {
        /* 1st object */
        current = new_object;
        scheduler->object_list = current;
    }
    else
    {
        current->next = new_object;
    }
    scheduler->ref_count++;
    data.scheduler_prof = *scheduler;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_SCHED_PROF,
                                    &id, &data);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data set", rv);
    
    return SAI_STATUS_SUCCESS;
}

sai_status_t 
_brcm_sai_scheduler_detach_object(int id, _brcm_sai_qos_scheduler_t *scheduler, 
                                  sai_object_id_t object_id, bool apply_default)
{
    sai_attribute_t sai_attr;
    sai_object_id_t object_type;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_scheduler_object_t *current = scheduler->object_list, *prev;

    prev = current;
    /* See if the object is in the list. */
    while (NULL != current)
    {
        if (current->object_id == object_id)
        {
            /* Unlink it */
            if (scheduler->ref_count == 1)
            {
                /* Last item in list */
                scheduler->object_list = NULL;
            }
            else
            {
                /* Is this the first ? */
                if (prev == current)
                {
                    scheduler->object_list = current->next;
                }
                else
                {
                    prev->next = current->next;
                }
            }

            /* Set object attributes back to defaults. */
            sai_attr.value.oid = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_SCHEDULER, 0);

            object_type = BRCM_SAI_GET_OBJ_TYPE(object_id);
            if (SAI_OBJECT_TYPE_PORT == object_type)
            {
                if (TRUE == apply_default)
                {
                    sai_attr.id = SAI_PORT_ATTR_QOS_SCHEDULER_PROFILE_ID;
                    rv = port_apis.set_port_attribute(object_id, &sai_attr);
                    if (SAI_STATUS_SUCCESS != rv)
                    {
                          BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                                 "Unable to set port attributes to default for "
                                                 "port id %lx\n", object_id);
                    }
                }
            }
            else if (SAI_OBJECT_TYPE_QUEUE == object_type)
            {
                if (TRUE == apply_default)
                {
                    sai_attr.id = SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID;
                    rv = qos_apis.set_queue_attribute(object_id, &sai_attr);
                    if (SAI_STATUS_SUCCESS != rv)
                    {
                          BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, 
                                                 "Unable to set queue attributes to default for "
                                                 "queue id %lx\n", object_id);
                    }
                }
            }
            else if (SAI_OBJECT_TYPE_SCHEDULER_GROUP == object_type)
            {
                /* FIXME: Nothing to be done for now */
                if (TRUE == apply_default)
                {
                }
            }
            else
            {
                BRCM_SAI_LOG_SCHEDULER(SAI_LOG_LEVEL_ERROR, "Invalid object type found %lx\n",
                                       object_id);
                return SAI_STATUS_INVALID_OBJECT_TYPE;
            }

            scheduler->ref_count--;
            CHECK_FREE(current);
            data.scheduler_prof = *scheduler;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_SCHED_PROF,
                                            &id, &data);
            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER, "sched prof data set", rv);

            return rv;
        }
        prev = current;
        current = current->next;
    }

    return SAI_STATUS_ITEM_NOT_FOUND;
}

int
_brcm_sai_scheduler_mode_get(_brcm_sai_qos_scheduler_t *scheduler)
{
    int mode = _BRCM_SAI_SCHEDULER_ATTR_INVALID;
    
    if (SAI_SCHEDULING_TYPE_STRICT == scheduler->algorithm)
    {
        mode = BCM_COSQ_STRICT;
    }
    else if (SAI_SCHEDULING_TYPE_WRR == scheduler->algorithm)
    {
        mode = BCM_COSQ_WEIGHTED_ROUND_ROBIN;
    }
    else if (SAI_SCHEDULING_TYPE_DWRR == scheduler->algorithm)
    {
        mode = BCM_COSQ_DEFICIT_ROUND_ROBIN;
    }
    return mode;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_scheduler_api_t qos_scheduler_apis = {
    brcm_sai_create_scheduler,
    brcm_sai_remove_scheduler,
    brcm_sai_set_scheduler_attribute,
    brcm_sai_get_scheduler_attribute
};
