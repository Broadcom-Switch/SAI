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
#                           Scheduler group functions                          #
################################################################################
*/
/**
 * @brief  Create Scheduler group
 *
 * @param[out] scheduler_group_id Scheudler group id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list array of attributes
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_scheduler_group(_Out_ sai_object_id_t  *scheduler_group_id,
                                _In_ sai_object_id_t switch_id,
                                _In_ uint32_t attr_count,
                                _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER_GROUP);

    return rv;
}

/**
 * @brief  Remove Scheduler group
 *
 * @param[in] scheduler_group_id Scheudler group id
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_scheduler_group(_In_ sai_object_id_t scheduler_group_id)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER_GROUP);

    return rv;
}

/**
 * @brief  Set Scheduler group Attribute
 *
 * @param[in] scheduler_group_id Scheudler group id
 * @param[in] attr attribute to set
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_scheduler_group_attribute(_In_ sai_object_id_t scheduler_group_id,
                                       _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;
    sai_uint8_t level;
    bcm_port_t port;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (BRCM_SAI_CHK_OBJ_MISMATCH(scheduler_group_id, SAI_OBJECT_TYPE_SCHEDULER_GROUP))
    {
        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                 "Invalid object type 0x%16lx passed\n",
                                 scheduler_group_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    port = BRCM_SAI_GET_OBJ_MAP(scheduler_group_id);
    /* 3 bits for the level, 5 bits for the node index */
    level = BRCM_SAI_GET_OBJ_SUB_TYPE(scheduler_group_id);

    if (DEV_IS_THX() || DEV_IS_TD3())
    {
        if ((level & 0x7) == _BRCM_SAI_L1_SCHEDULER_TYPE)
        {
            BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                     "Configuration of scheduler group level %d is not supported.\n",
                                     level);
            return SAI_STATUS_NOT_SUPPORTED;
        }
    }

    switch(attr->id)
    {
        case SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID:
        {
            int id, mode;
            bcm_cos_queue_t cosq;
            _brcm_sai_table_data_t data;
            bcm_gport_t gport, parent_gport;
            _brcm_sai_qos_scheduler_t scheduler;
            _brcm_sai_scheduler_group_t sched_group;

            id = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
            if (0 == id)
            {
                int _id;
                
                /* FIXME: In the future apply the default config */
                DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
                sched_group.port = port;
                sched_group.level = (level & 0x7);
                sched_group.index = (level >> 3);
                data.sched_group = &sched_group;
                /* Lookup current id */
                rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
                {
                    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry lookup.", rv);
                }
                _id = BRCM_SAI_GET_OBJ_VAL(int, sched_group.scheduler_oid);
                if (_id)
                {
                    sched_group.scheduler_oid = attr->value.oid;
                    rv = _brcm_sai_scheduler_get(_id, &scheduler);
                    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "sched prof data get", rv);
                    /* Detach non default id */
                    if (SAI_STATUS_SUCCESS !=
                        _brcm_sai_scheduler_detach_object(_id, &scheduler, scheduler_group_id, FALSE))
                    {
                        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                                 "Unable to detach queue object %lx from scheduler %lx.\n",
                                                 scheduler_group_id, attr->value.oid);
                        return SAI_STATUS_FAILURE;
                    }
                    /* Update table */
                    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry add.", rv);
                }
                break;
            }
            rv = _brcm_sai_scheduler_get(id, &scheduler);
            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "sched prof data get", rv);
            /* Attach queue object to scheduler list. */
            if (SAI_STATUS_SUCCESS !=
                    _brcm_sai_scheduler_attach_object(id, &scheduler, scheduler_group_id))
            {
                BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                         "Unable to attach queue object %lx to scheduler %lx.\n",
                                         scheduler_group_id, attr->value.oid);
                return SAI_STATUS_FAILURE;
            }
            mode = _brcm_sai_scheduler_mode_get(&scheduler);
            gport = BRCM_SAI_GET_OBJ_VAL(bcm_gport_t, scheduler_group_id);
            rv = bcm_cosq_gport_attach_get(0, gport, &parent_gport, &cosq);
            BRCM_SAI_API_CHK(SAI_API_SCHEDULER_GROUP, "cosq gport attach get", rv);
            if (DEV_IS_THX() || DEV_IS_TD3())
            {
                parent_gport = _brcm_sai_gport_get(parent_gport);
                BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_DEBUG,
                                         "Parent port is not GPORT so converting it to GPORT. PGPORT = 0x%x\n",
                                         parent_gport);
            }
            if (_BRCM_SAI_SCHEDULER_ATTR_INVALID != mode)
            {
                rv = bcm_cosq_gport_sched_set(0, parent_gport, cosq, mode,
                                                  scheduler.weight);
                if (BCM_E_UNAVAIL == rv)
                {
                    BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_DEBUG,
                                             "Trying to set schedule mode on port:%d, "
                                             "level:%d queue:%d "
                                             "and OPENNSL returned UNAVAIL.\n"
                                             "Check that all SP queues are consecutive offsets "
                                             "on this scheduler node.\n",
                                             port, level, cosq);
                }
                BRCM_SAI_API_CHK(SAI_API_SCHEDULER_GROUP, "cosq gport sched set", rv);
            }

            rv = _brcm_sai_cosq_bandwidth_set(parent_gport, cosq, &scheduler);
            BRCM_SAI_API_CHK(SAI_API_SCHEDULER_GROUP, "cosq gport bandwidth set", rv);

            if (scheduler.minimum_burst)
            {
                rv = _brcm_sai_cosq_config(parent_gport, cosq, 10,
                                           (uint32) scheduler.minimum_burst);
                BRCM_SAI_API_CHK(SAI_API_SCHEDULER_GROUP, "cosq port control set ", rv);
            }

            if (scheduler.maximum_burst)
            {
                rv = _brcm_sai_cosq_config(parent_gport, cosq, 11,
                                           (uint32) scheduler.maximum_burst);
                BRCM_SAI_API_CHK(SAI_API_SCHEDULER_GROUP, "cosq port control set ", rv);
            }
            DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
            sched_group.port = port;
            sched_group.level = (level & 0x7);
            sched_group.index = (level >> 3);
            sched_group.scheduler_oid = attr->value.oid;
            data.sched_group = &sched_group;
            rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_SCHED_GRP, &data);
            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry add.", rv);
            break;
        }
        default:
            BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                     "Unknown scheduler group attribute %d passed\n",
                                     attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER_GROUP);

    return rv;
}

/**
 * @brief  Get Scheduler Group attribute
 *
 * @param[in] scheduler_group_id - scheduler group id
 * @param[in] attr_count - number of attributes
 * @param[inout] attr_list - array of attributes
 *
 * @return SAI_STATUS_SUCCESS on success
 *        Failure status code on error
 */

STATIC sai_status_t
brcm_sai_get_scheduler_group_attribute(_In_ sai_object_id_t scheduler_group_id,
                                       _In_ uint32_t attr_count,
                                       _Inout_ sai_attribute_t *attr_list)
{
    int i;
    bcm_port_t port;
    sai_uint8_t level, idx /* this is the nodes index */;
    _brcm_sai_table_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_scheduler_group_t sched_group;
            

    BRCM_SAI_FUNCTION_ENTER(SAI_API_SCHEDULER_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(scheduler_group_id, SAI_OBJECT_TYPE_SCHEDULER_GROUP))
    {
        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                 "Invalid object type 0x%16lx passed\n",
                                 scheduler_group_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    port = BRCM_SAI_GET_OBJ_MAP(scheduler_group_id);
    /* 3 bits for the level, 5 bits for the node index */
    level = BRCM_SAI_GET_OBJ_SUB_TYPE(scheduler_group_id);
    idx = level >> 3;
    level = level & 0x7;
    if (((DEV_IS_THX() || DEV_IS_TD3()) 
         && level > _BRCM_SAI_L0_SCHEDULER_TYPE) ||
        level > _BRCM_SAI_L1_SCHEDULER_TYPE)
    {
        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                 "Invalid scheduler group level %d.\n",
                                 level);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_SCHEDULER_GROUP_ATTR_PORT_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, port);
            case SAI_SCHEDULER_GROUP_ATTR_LEVEL:
                attr_list[i].value.u8 = level;
                break;
            case SAI_SCHEDULER_GROUP_ATTR_MAX_CHILDS:
                /* Note: Same as child count for a RO scheduler */
                /* Note: This logic is device dependent */
            case SAI_SCHEDULER_GROUP_ATTR_CHILD_COUNT:
                /* Note: This logic is device dependent */
                attr_list[i].value.u32 = 2;
                if (DEV_IS_THX() || DEV_IS_TD3())
                {
                    if (_BRCM_SAI_PORT_SCHEDULER_TYPE == level)
                    {
                        if (_BRCM_SAI_IS_CPU_PORT(port))
                        {
                            attr_list[i].value.u32 = NUM_TH_CPU_L0_NODES;
                        }
                        else
                        {
                            attr_list[i].value.u32 = NUM_TH_L0_NODES;
                        }
                    }
                    else if (_BRCM_SAI_L0_SCHEDULER_TYPE == level)
                    {
                        if (_BRCM_SAI_IS_CPU_PORT(port))
                        {
                            attr_list[i].value.u32 = NUM_TH_CPU_MC_QUEUES;
                        }
                    }
                    else
                    {
                        /* TH does not support configurable L1 nodes */
                        rv = SAI_STATUS_NOT_SUPPORTED;
                    }
                }
                else
                {
                    if (_BRCM_SAI_PORT_SCHEDULER_TYPE == level && _BRCM_SAI_IS_CPU_PORT(port))
                    {
                        attr_list[i].value.u32 = NUM_TD2_CPU_L0_NODES;
                    }
                    else if ((_BRCM_SAI_IS_CPU_PORT(port) && _BRCM_SAI_L1_SCHEDULER_TYPE == level) ||
                             (!_BRCM_SAI_IS_CPU_PORT(port) && _BRCM_SAI_L0_SCHEDULER_TYPE == level &&
                              0 == idx))
                    {
                        attr_list[i].value.u32 = 8;
                    }
                }
                break;
            case SAI_SCHEDULER_GROUP_ATTR_CHILD_LIST:
            {
                bcm_gport_t gport;
                bool queue = FALSE;
                _brcm_sai_indexed_data_t idata;
                int _idx[2], q, n, type, limit, offset = 0, count = 2;
                int num_queues = _brcm_sai_get_num_queues();

                /* This logic is device dependent */
                if (DEV_IS_THX() || DEV_IS_TD3())
                {
                    if (_BRCM_SAI_PORT_SCHEDULER_TYPE == level)
                    {
                        count = _brcm_sai_get_num_l0_nodes(port);
                    }
                    else if (_BRCM_SAI_L0_SCHEDULER_TYPE == level)
                    {
                        if (_BRCM_SAI_IS_CPU_PORT(port))
                        {
                            count = NUM_TH_CPU_MC_QUEUES;
                        }
                        queue = TRUE;
                    }
                    else
                    {
                        /* TH does not support configurable L1 nodes */
                        rv = SAI_STATUS_NOT_SUPPORTED;
                        break;
                    }
                    /* Bump value to indicate child level in the encoding */
                    level++;                
                }
                else
                {
                    if (level == _BRCM_SAI_PORT_SCHEDULER_TYPE)
                    {
                        count = _brcm_sai_get_num_l0_nodes(port);
                    }
                    else if ((_BRCM_SAI_IS_CPU_PORT(port) && _BRCM_SAI_L1_SCHEDULER_TYPE == level) ||
                             (!_BRCM_SAI_IS_CPU_PORT(port) && _BRCM_SAI_L0_SCHEDULER_TYPE == level &&
                              0 == idx))
                    {
                        count = 8;
                    }
                    if (_BRCM_SAI_L1_SCHEDULER_TYPE == level)
                    {
                        queue = TRUE;
                    }
                    /* Bump value to indicate child level in the encoding */
                    level++;
                    /* Fetch gports and prepare objects */
                    if (0 != port && _BRCM_SAI_L1_SCHEDULER_TYPE == level &&
                        0 != idx)
                    {
                        offset = 8;
                    }
                }
                /* Fetch gports and prepare objects */
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < count)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < count ? 
                        BRCM_SAI_ATTR_LIST_OBJ_COUNT(i)-1 : count-1;
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = count;
                for (n=0; n<=limit; n++)
                {
                    q = (0 == port) ? 
                      (((DEV_IS_TD3()) || (DEV_IS_THX())) ? n+1 : idx*NUM_TD2_CPU_QUEUES+n+1) : 
                      (idx+n*num_queues+1);
                    if (queue)
                    {
                        type = (_BRCM_SAI_IS_CPU_PORT(port) ? SAI_QUEUE_TYPE_MULTICAST :
                                (q <= num_queues) ? SAI_QUEUE_TYPE_UNICAST : 
                                                    SAI_QUEUE_TYPE_MULTICAST);
                        BRCM_SAI_ATTR_LIST_OBJ_LIST(i, n) = 
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_QUEUE,
                                                        type, port, q);
                        _idx[0] = port;
                        _idx[1] = q-1;
                        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_QID,
                                                        _idx, &idata);
                        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "port qid data get", rv);
                        idata.port_qid.parent_sched = scheduler_group_id;
                        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_QID, 
                                                        _idx, &idata);
                        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "port qid data set", rv);
                    }
                    else
                    {
                        gport = _brcm_sai_switch_port_queue_get(port, offset+n,
                                                                level);
                        if (-1 == gport)
                        {
                            BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                            rv = SAI_STATUS_FAILURE;
                        }
                        /* Subtype: 3 LSB - level, 5 MSB - node index */
                        BRCM_SAI_ATTR_LIST_OBJ_LIST(i, n) =
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SCHEDULER_GROUP,
                                                        (level | ((offset+n)<<3)), port, gport);
                        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_DEBUG, "Schedular group child[%d] "
                                                 "port:%d, level:%d, obj:0x%8x\n",
                                                 n, port, level, gport);
                        DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
                        sched_group.port = port;
                        sched_group.level = level;
                        sched_group.index = offset+n;
                        data.sched_group = &sched_group;
                        /* Lookup current id */
                        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
                        {
                            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry lookup.", rv);
                        }
                        sched_group.parent_oid = scheduler_group_id;
                        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry add.", rv);
                    }
                }
                break;
            }
            case SAI_SCHEDULER_GROUP_ATTR_PARENT_NODE:
                DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
                sched_group.port = port;
                sched_group.level = level;
                sched_group.index = idx;
                data.sched_group = &sched_group;
                rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
                {
                    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry lookup.", rv);
                }
                else if (SAI_STATUS_SUCCESS == rv && 0 == sched_group.scheduler_oid)
                {
                    BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                             "First retreive child list from port or scheduler module.\n");
                    rv = SAI_STATUS_UNINITIALIZED;
                    break;
                }
                BRCM_SAI_ATTR_LIST_OBJ(i) = sched_group.parent_oid;
                break;
            case SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID:
                DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
                sched_group.port = port;
                sched_group.level = level;
                sched_group.index = idx;
                data.sched_group = &sched_group;
                rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                if (SAI_STATUS_ITEM_NOT_FOUND == rv || 
                    (SAI_STATUS_SUCCESS == rv && 0 == sched_group.scheduler_oid))
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_SCHEDULER, 0); 
                    rv = SAI_STATUS_SUCCESS;
                    break;
                }
                else
                {
                    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry lookup.", rv);
                }
                BRCM_SAI_ATTR_LIST_OBJ(i) = sched_group.scheduler_oid;
                break;
            default:
                BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                         "Unknown scheduler group attribute %d passed\n",
                                         attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_INFO,
                                     "Error processing scheduler group attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_SCHEDULER_GROUP);

    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
sai_status_t 
_brcm_sai_alloc_sched_group_info()
{
    sai_status_t rv;

    rv = _brcm_sai_db_table_create(_BRCM_SAI_TABLE_SCHED_GRP,
                                   _BRCM_SAI_MAX_PORTS*TOTAL_SCHED_NODES_PER_PORT);
    BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "scheduler group table create", rv);
    return rv;
}

int
_brcm_sai_get_scheduler_max(int type)
{
    /* Note: convert to switch case in the future to support more devices */
    if (DEV_IS_TH3())
    {
        switch(type)
        {
            case _CPU_L0_NODES:
                return NUM_TH3_CPU_L0_NODES;
            case _L0_NODES:
                return NUM_TH3_L0_NODES;
            default:
                return 0;
        }
    }
    else if (DEV_IS_TH() || DEV_IS_TH2() || DEV_IS_TD3())
    {
        switch(type)
        {
            case _CPU_L0_NODES:
                return NUM_TH_CPU_L0_NODES;
            case _L0_NODES:
                return NUM_TH_L0_NODES;
            default:
                return 0;
        }
    }
    else if (DEV_IS_TD2())/* TD2 */
    {
        switch(type)
        {
            case _CPU_L0_NODES:
                return NUM_TD2_CPU_L0_NODES;
            case _L0_NODES:
                return NUM_TD2_L0_NODES;
            default:
                return 0;
        }
    }
    else if (DEV_IS_HX4())/* Helix4 */
    {
        switch(type)
        {
            case _CPU_L0_NODES:
                return NUM_HX4_CPU_L0_NODES;
            case _L0_NODES:
                return NUM_HX4_L0_NODES;
            default:
                return 0;
        }
    }
    else
    {
        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_INFO,
                                 "Unsupported device ID.\n");
        return 0;
    }
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_scheduler_group_api_t scheduler_group_apis = {
    brcm_sai_create_scheduler_group,
    brcm_sai_remove_scheduler_group,
    brcm_sai_set_scheduler_group_attribute,
    brcm_sai_get_scheduler_group_attribute
};
