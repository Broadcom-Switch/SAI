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
#                              Bridge functions                                #
################################################################################
*/

/**
 * @brief Create bridge port
 *
 * @param[out] bridge_port_id Bridge port ID
 * @param[in] switch_id Switch object id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_bridge_port(_Out_ sai_object_id_t* bridge_port_id,
                            _In_ sai_object_id_t switch_id,
                            _In_ uint32_t attr_count,
                            _In_ const sai_attribute_t *attr_list)
{
    bool state = FALSE;
    _brcm_sai_indexed_data_t idata;
    int i, val, type = -1, port_type = 0;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int flags = BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(bridge_port_id);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_BRIDGE_PORT_ATTR_TYPE:
                type = attr_list[i].value.u32;
                if (SAI_BRIDGE_PORT_TYPE_PORT != type)
                {
                    BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                                        "Only bridge port type port supported currently.\n");
                    rv = SAI_STATUS_NOT_IMPLEMENTED;
                }
                break;
            case SAI_BRIDGE_PORT_ATTR_PORT_ID:
                port_type = BRCM_SAI_ATTR_LIST_OBJ_TYPE(i);
                val = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                break;
            case SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES:
            case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION:
                rv = SAI_STATUS_NOT_IMPLEMENTED;
                break;
            case SAI_BRIDGE_PORT_ATTR_ADMIN_STATE:
                state = attr_list[i].value.booldata;
                break;
            case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE:
                switch (attr_list[i].value.u32)
                {
                    case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DROP:
                        flags = 0;
                        break;
                    case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE:
                        flags = BCM_PORT_LEARN_FWD;
                        break;
                    case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW:
                        flags = BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD;
                        break;
                    case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_TRAP:
                        flags = BCM_PORT_LEARN_CPU;
                        break;
                    case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_LOG:
                        flags = BCM_PORT_LEARN_FWD | BCM_PORT_LEARN_CPU;
                        break;
                    default:
                        return SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                break;
            default:
                BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                                    "Unknown bridge port attribute %d passed\n", attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                             "Error processing bridge attributes\n");
            return rv;
        }
    }
    if (-1 == type || 0 == port_type)
    {
        BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                            "Unknown bridge port type or unsupported port type\n");
        rv = SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    else
    {
        /* Note: currently only supporting bridge port type SAI_BRIDGE_PORT_TYPE_PORT */
        if (SAI_OBJECT_TYPE_PORT == port_type)
        {
            rv = bcm_port_learn_set(0, val, TRUE == state ? flags : 0);
            BRCM_SAI_API_CHK(SAI_API_BRIDGE, "port learn set", rv);
            rv = bcm_port_control_set(0, val, bcmPortControlL2Move, TRUE == state ?
                                      flags : 0);
            BRCM_SAI_API_CHK(SAI_API_BRIDGE, "port control set", rv);
        }
        else
        {
            int port, max, actual;
            bcm_trunk_member_t *members;
            bcm_trunk_info_t trunk_info;
            bcm_trunk_chip_info_t trunk_chip_info;

            rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
            BRCM_SAI_API_CHK(SAI_API_VLAN, "trunk chip info get", rv);
            max = trunk_chip_info.trunk_group_count;
            members = ALLOC(sizeof(bcm_trunk_member_t) * max);
            if (IS_NULL(members))
            {
                BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_CRITICAL,
                                    "Error allocating memory for lag members.\n");
                return SAI_STATUS_NO_MEMORY;
            }
            rv = bcm_trunk_get(0, val, &trunk_info, max, members, &actual);
            BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "trunk get", rv, members);
            for (i=0; i<actual; i++)
            {
                rv = bcm_port_local_get(0, members[i].gport, &port);
                BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "port local get", rv,
                                      members);
                rv = bcm_port_learn_set(0, port, TRUE == state ? flags : 0);
                BRCM_SAI_API_CHK_FREE(SAI_API_BRIDGE, "port learn set", rv,
                                      members);
                rv = bcm_port_control_set(0, port, bcmPortControlL2Move,
                                          TRUE == state ? flags : 0);
                BRCM_SAI_API_CHK_FREE(SAI_API_BRIDGE, "port control set", rv,
                                      members);
            }
            CHECK_FREE(members);
        }
        if (SAI_OBJECT_TYPE_PORT == port_type)
        {
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                            &val, &idata);
            BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
            idata.port_info.idx = val;
            idata.port_info.bdg_port = _BP_CREATED;
            idata.port_info.learn_flags = flags;
            idata.port_info.bdg_port_admin_state = state;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                            &val, &idata);
            BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data set", rv);
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_BRIDGE_PORTS, INC);
            BRCM_SAI_RV_CHK(SAI_API_BRIDGE,
                            "incrementing global bridge port count global data", rv);
        }
        *bridge_port_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                                      type, port_type, val);

        if (SAI_OBJECT_TYPE_LAG == port_type)
        {
            _brcm_sai_list_data_t ldata;
            _brcm_sai_bridge_lag_port_t lag_port;

            lag_port.oid = *bridge_port_id;
            lag_port.bridge_port_state = state;
            lag_port.learn_flags = flags;
            lag_port.vid_count = 0;
            lag_port.vid_list = NULL;
            lag_port.next = NULL;
            ldata.bdg_lag_ports = &lag_port;
            rv = _brcm_sai_list_add(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL,
                                    NULL, &ldata);
            BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list add", rv);
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_BRIDGE_LAG_PORTS, INC);
            BRCM_SAI_RV_CHK(SAI_API_BRIDGE,
                            "incrementing global bridge lag port count global data", rv);
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Remove bridge port
 *
 * @param[in] bridge_port_id Bridge port ID
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_bridge_port(_In_ sai_object_id_t bridge_port_id)
{
    int act, idx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t idata;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(bridge_port_id, SAI_OBJECT_TYPE_BRIDGE_PORT))
    {
        BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                            "Invalid object type 0x%16lx passed\n",
                            bridge_port_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_GET_OBJ_MAP(bridge_port_id))
    {
        idx = BRCM_SAI_GET_OBJ_VAL(int, bridge_port_id);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                        &idx, &idata);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
        idata.port_info.idx = idx;
        act = idata.port_info.bdg_port;
        idata.port_info.bdg_port = _BP_DELETED;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                         &idx, &idata);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
        if (_BP_CREATED == act)
        {
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_BRIDGE_PORTS, DEC);
            BRCM_SAI_RV_CHK(SAI_API_BRIDGE,
                        "decrementing global bridge port count global data", rv);
        }
    }
    else
    {
        _brcm_sai_list_key_t list_key;

        list_key.obj_id = bridge_port_id;
        rv = _brcm_sai_list_del(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list del", rv);
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_BRIDGE_LAG_PORTS, DEC);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE,
                        "incrementing global bridge lag port count global data", rv);
    }
    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                     "Removed bridge port, subtype: %d, type: %d, val: %d\n",
                     BRCM_SAI_GET_OBJ_SUB_TYPE(bridge_port_id),
                     BRCM_SAI_GET_OBJ_MAP(bridge_port_id),
                     BRCM_SAI_GET_OBJ_VAL(int, bridge_port_id));

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Set attribute for bridge port
 *
 * @param[in] bridge_port_id Bridge port ID
 * @param[in] attr attribute to set
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_bridge_port_attribute(_In_ sai_object_id_t bridge_port_id,
                                   _In_ const sai_attribute_t *attr)
{
#define _SET_BRIDGE_PORT "Set bridge port"
    int sub_type, port_tid = -1;
    _brcm_sai_indexed_data_t idata;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_SET_OBJ_ATTRIB_PARAM_CHK(bridge_port_id, SAI_OBJECT_TYPE_BRIDGE_PORT);

    if (SAI_OBJECT_TYPE_BRIDGE_PORT == BRCM_SAI_GET_OBJ_TYPE(bridge_port_id))
    {
        port_tid = BRCM_SAI_GET_OBJ_VAL(int, bridge_port_id);
        sub_type = BRCM_SAI_GET_OBJ_MAP(bridge_port_id);
        if (SAI_OBJECT_TYPE_PORT == sub_type)
        {
            BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_DEBUG, "port: %d\n", port_tid);
        }
        else
        {
            BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_DEBUG, "lag: %d\n", port_tid);
        }
    }
    else
    {
        BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                            "Invalid object type %d passed\n",
                            BRCM_SAI_GET_OBJ_TYPE(bridge_port_id));
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    if (-1 == port_tid)
    {
        BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                            "No port/lag type/value found for bridge port object\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    switch(attr->id)
    {
        case SAI_BRIDGE_PORT_ATTR_ADMIN_STATE:
        {
            /* Note: currently only supporting bridge port type SAI_BRIDGE_PORT_TYPE_PORT */
            if (SAI_OBJECT_TYPE_PORT == sub_type)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                                &port_tid, &idata);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
                rv = bcm_port_learn_set(0, port_tid, TRUE == attr->value.booldata ?
                                        idata.port_info.learn_flags : 0);
                BRCM_SAI_API_CHK(SAI_API_BRIDGE, "port learn set", rv);
                rv = bcm_port_control_set(0, port_tid, bcmPortControlL2Move,
                                          TRUE == attr->value.booldata ?
                                          idata.port_info.learn_flags : 0);
                BRCM_SAI_API_CHK(SAI_API_BRIDGE, "port control set", rv);
                idata.port_info.bdg_port_admin_state = attr->value.booldata;
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                                &port_tid, &idata);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data set", rv);
            }
            else
            {
                bool state;
                int i, port, max, actual;
                bcm_trunk_member_t *members;
                bcm_trunk_info_t trunk_info;
                _brcm_sai_list_data_t ldata;
                _brcm_sai_list_key_t list_key;
                _brcm_sai_bridge_lag_port_t lag_port;
                bcm_trunk_chip_info_t trunk_chip_info;

                rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
                BRCM_SAI_API_CHK(SAI_API_VLAN, "trunk chip info get", rv);
                max = trunk_chip_info.trunk_group_count;
                members = ALLOC(sizeof(bcm_trunk_member_t) * max);
                if (IS_NULL(members))
                {
                    BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_CRITICAL,
                                        "Error allocating memory for lag members.\n");
                    return SAI_STATUS_NO_MEMORY;
                }
                rv = bcm_trunk_get(0, port_tid, &trunk_info, max, members, &actual);
                BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "trunk get", rv, members);
                state = attr->value.booldata;
                list_key.obj_id = bridge_port_id;
                rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key,
                                        &ldata);
                BRCM_SAI_API_CHK_FREE(SAI_API_BRIDGE, "bridge lag ports list get", rv,
                                      members);
                for (i=0; i<actual; i++)
                {
                    rv = bcm_port_local_get(0, members[i].gport, &port);
                    BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "port local get", rv,
                                          members);
                    rv = bcm_port_learn_set(0, port, TRUE == state ?
                                            ldata.bdg_lag_ports->learn_flags : 0);
                    BRCM_SAI_API_CHK(SAI_API_BRIDGE, "port learn set", rv);
                    rv = bcm_port_control_set(0, port, bcmPortControlL2Move,
                                              TRUE == state ?
                                              ldata.bdg_lag_ports->learn_flags : 0);
                    BRCM_SAI_API_CHK(SAI_API_BRIDGE, "port control set", rv);
                }
                CHECK_FREE(members);
                lag_port.oid = bridge_port_id;
                lag_port.bridge_port_state = state;
                lag_port.vid_count = ldata.bdg_lag_ports->vid_count;
                lag_port.vid_list = ldata.bdg_lag_ports->vid_list;
                lag_port.learn_flags = ldata.bdg_lag_ports->learn_flags;
                lag_port.next = NULL;
                ldata.bdg_lag_ports = &lag_port;
                rv = _brcm_sai_list_del(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list del", rv);
                rv = _brcm_sai_list_add(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL,
                                        NULL, &ldata);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list add", rv);
            }
            break;
        }
        case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE:
        {
            uint32 flags = 0;

            switch (attr->value.u32)
            {
                case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DROP:
                    break;
                case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE:
                    flags = BCM_PORT_LEARN_FWD;
                    break;
                case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW:
                    flags = BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD;
                    break;
                case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_TRAP:
                    flags = BCM_PORT_LEARN_CPU;
                    break;
                case SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_LOG:
                    flags = BCM_PORT_LEARN_FWD | BCM_PORT_LEARN_CPU;
                    break;
                default:
                    return SAI_STATUS_INVALID_ATTR_VALUE_0;
            }
            /* Note: currently only supporting bridge port type SAI_BRIDGE_PORT_TYPE_PORT */
            if (SAI_OBJECT_TYPE_PORT == sub_type)
            {
                rv = bcm_port_learn_set(0, port_tid, flags);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_BRIDGE, _SET_BRIDGE_PORT, port_tid, rv,
                                          attr->id);
                rv = bcm_port_control_set(0, port_tid, bcmPortControlL2Move, flags);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_BRIDGE, _SET_BRIDGE_PORT, port_tid, rv,
                                          attr->id);
            }
            else
            {
                int i, port, max, actual;
                bcm_trunk_member_t *members;
                bcm_trunk_info_t trunk_info;
                _brcm_sai_list_data_t ldata;
                _brcm_sai_list_key_t list_key;
                _brcm_sai_bridge_lag_port_t lag_port;
                bcm_trunk_chip_info_t trunk_chip_info;

                rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
                BRCM_SAI_API_CHK(SAI_API_VLAN, "trunk chip info get", rv);
                max = trunk_chip_info.trunk_group_count;
                members = ALLOC(sizeof(bcm_trunk_member_t) * max);
                if (IS_NULL(members))
                {
                    BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_CRITICAL,
                                        "Error allocating memory for lag members.\n");
                    return SAI_STATUS_NO_MEMORY;
                }
                rv = bcm_trunk_get(0, port_tid, &trunk_info, max, members, &actual);
                BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "trunk get", rv, members);
                for (i=0; i<actual; i++)
                {
                    rv = bcm_port_local_get(0, members[i].gport, &port);
                    BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "port local get", rv,
                                          members);
                    rv = bcm_port_learn_set(0, port, flags);
                    BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "port learn set", rv,
                                          members);
                    rv = bcm_port_control_set(0, port, bcmPortControlL2Move, flags);
                    BRCM_SAI_API_CHK_FREE(SAI_API_BRIDGE, "port control set", rv,
                                          members);
                }
                CHECK_FREE(members);
                list_key.obj_id = bridge_port_id;
                rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key,
                                        &ldata);
                BRCM_SAI_API_CHK_FREE(SAI_API_BRIDGE, "port enable set", rv,
                                      members);
                lag_port.oid = bridge_port_id;
                lag_port.bridge_port_state = ldata.bdg_lag_ports->bridge_port_state;
                lag_port.learn_flags = flags;
                lag_port.next = NULL;
                ldata.bdg_lag_ports = &lag_port;
                rv = _brcm_sai_list_del(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list del", rv);
                rv = _brcm_sai_list_add(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL,
                                        NULL, &ldata);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list add", rv);
            }
            break;
        }
        default:
            BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                              "Unknown bridge port attribute %d passed\n", attr->id);
            rv = SAI_STATUS_NOT_IMPLEMENTED;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Get attributes of bridge port
 *
 * @param[in] bridge_port_id Bridge port ID
 * @param[in] attr_count number of attributes
 * @param[inout] attr_list array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_bridge_port_attribute(_In_ sai_object_id_t bridge_port_id,
                                   _In_ uint32_t attr_count,
                                   _Inout_ sai_attribute_t *attr_list)
{
#define _GET_BRIDGE_PORT "Get bridge port"
    int i;
    int sub_type, port_tid = -1;
    _brcm_sai_indexed_data_t idata;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(bridge_port_id, SAI_OBJECT_TYPE_BRIDGE_PORT);

    if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_GET_OBJ_MAP(bridge_port_id))
    {
        port_tid = BRCM_SAI_GET_OBJ_VAL(int, bridge_port_id);
        sub_type = BRCM_SAI_GET_OBJ_MAP(bridge_port_id);
        if (SAI_OBJECT_TYPE_PORT == sub_type)
        {
            BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_DEBUG, "port: %d\n", port_tid);
        }
        else
        {
            BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_DEBUG, "lag: %d\n", port_tid);
        }
        if (-1 == port_tid)
        {
            BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                                "No port/lag type/value found for bridge port object\n");
            return SAI_STATUS_INVALID_PARAMETER;
        }
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_BRIDGE_PORT_ATTR_TYPE:
                attr_list[i].value.u32 = BRCM_SAI_GET_OBJ_SUB_TYPE(bridge_port_id);
                break;
            case SAI_BRIDGE_PORT_ATTR_PORT_ID:
                if (SAI_OBJECT_TYPE_LAG == sub_type)
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG,
                                                    BRCM_SAI_GET_OBJ_VAL(int, bridge_port_id));
                }
                else if (SAI_OBJECT_TYPE_PORT == sub_type)
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT,
                                                     BRCM_SAI_GET_OBJ_VAL(int, bridge_port_id));
                }
                else
                {
                    BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                                        "No port/lag type found for bridge object\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            case SAI_BRIDGE_PORT_ATTR_ADMIN_STATE:
                if (SAI_OBJECT_TYPE_PORT == sub_type)
                {
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                                    &port_tid, &idata);
                    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
                    attr_list[i].value.booldata = idata.port_info.bdg_port_admin_state;
                }
                else
                {
                    _brcm_sai_list_data_t ldata;
                    _brcm_sai_list_key_t list_key;

                    list_key.obj_id = bridge_port_id;
                    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key,
                                            &ldata);
                    BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_BRIDGE, _GET_BRIDGE_PORT, port_tid, rv,
                                              attr_list[i].id);
                    attr_list[i].value.booldata = ldata.bdg_lag_ports->bridge_port_state;
                }
                break;
            case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE:
            {
                uint32 flags;

                /* Note: currently only supporting bridge port type SAI_BRIDGE_PORT_TYPE_PORT */
                if (SAI_OBJECT_TYPE_PORT == sub_type)
                {
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                                    &port_tid, &idata);
                    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
                    flags = idata.port_info.learn_flags;
                }
                else
                {
                    _brcm_sai_list_data_t ldata;
                    _brcm_sai_list_key_t list_key;

                    list_key.obj_id = bridge_port_id;
                    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key,
                                            &ldata);
                    BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_BRIDGE, _GET_BRIDGE_PORT, port_tid, rv,
                                              attr_list[i].id);
                    flags = ldata.bdg_lag_ports->learn_flags;
                }
                if (BCM_PORT_LEARN_FWD == flags)
                {
                    attr_list[i].value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DISABLE;
                }
                else if ((BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD) == flags)
                {
                    attr_list[i].value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_HW;
                }
                else if (BCM_PORT_LEARN_CPU == flags)
                {
                    attr_list[i].value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_TRAP;
                }
                else if ((BCM_PORT_LEARN_FWD | BCM_PORT_LEARN_CPU) == flags)
                {
                    attr_list[i].value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_CPU_LOG;
                }
                else
                {
                    attr_list[i].value.u32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DROP;
                }
                break;
            }
            default:
                BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                                    "Unknown bridge port attribute %d passed\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Create bridge
 *
 * @param[out] bridge_id Bridge ID
 * @param[in] switch_id Switch object id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_bridge(_Out_ sai_object_id_t* bridge_id,
                       _In_ sai_object_id_t switch_id,
                       _In_ uint32_t attr_count,
                       _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Remove bridge
 *
 * @param[in] bridge_id Bridge ID
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_bridge (_In_ sai_object_id_t bridge_id)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Set attribute for bridge
 *
 * @param[in] bridge_id Bridge ID
 * @param[in] attr attribute to set
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_bridge_attribute(_In_ sai_object_id_t bridge_id,
                              _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Get attributes of bridge
 *
 * @param[in] bridge_id Bridge ID
 * @param[in] attr_count number of attributes
 * @param[inout] attr_list array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_bridge_attribute(_In_ sai_object_id_t bridge_id,
                              _In_ uint32_t attr_count,
                              _Inout_ sai_attribute_t *attr_list)
{
    int i;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(bridge_id, SAI_OBJECT_TYPE_BRIDGE))
    {
        BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%16lx passed\n",
                          bridge_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    if (BRCM_SAI_GET_OBJ_VAL(int, bridge_id))
    {
        BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                            "Invalid object id 0x%16lx passed\n",
                            bridge_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_BRIDGE_ATTR_TYPE:
                attr_list[i].value.u32 = SAI_BRIDGE_TYPE_1Q;
                break;
            case SAI_BRIDGE_ATTR_PORT_LIST:
            {
                bcm_pbmp_t pbmp;
                sai_status_t rv1;
                _brcm_sai_data_t gdata;
                int p, count, limit, index = 0;
                _brcm_sai_indexed_data_t idata;

                if (0 == BRCM_SAI_ATTR_LIST_OBJ_COUNT(i))
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }
                _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
                BCM_PBMP_ITER(pbmp, p)
                {
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                                     &p, &idata);
                    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
                    if (_BP_DELETED != idata.port_info.bdg_port)
                    {
                        index++;
                    }
                }
                rv = _brcm_sai_global_data_get(_BRCM_SAI_BRIDGE_LAG_PORTS, &gdata);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag port count global data get", rv);
                count = index + gdata.u32;
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < count)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                /* Set limit to max index depending on incoming list
                   size */
                limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < count ?
                        BRCM_SAI_ATTR_LIST_OBJ_COUNT(i)-1 : count-1;
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = count;
                index = 0; count = gdata.u32; /* reset for reuse */
                BCM_PBMP_ITER(pbmp, p)
                {
                    if (index > limit)
                    {
                        break;
                    }
                    rv1 = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                                     &p, &idata);
                    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv1);
                    if (_BP_DELETED != idata.port_info.bdg_port)
                    {
                        BRCM_SAI_ATTR_LIST_OBJ_LIST(i, index++) =
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                                        SAI_BRIDGE_PORT_TYPE_PORT,
                                                        SAI_OBJECT_TYPE_PORT, p);
                    }
                }
                if (count)
                {
                    _brcm_sai_list_data_t ldata;
                    _brcm_sai_bridge_lag_port_t *lag_ports;

                    ldata.bdg_lag_ports = NULL;
                    while(SAI_STATUS_SUCCESS ==
                        _brcm_sai_list_traverse(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, &ldata, &ldata))
                    {
                        lag_ports = ldata.bdg_lag_ports;
                        if (index > limit)
                        {
                            break;
                        }
                        BRCM_SAI_ATTR_LIST_OBJ_LIST(i, index++) = lag_ports->oid;
                    }
                }
                break;
            }
            default:
                BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_ERROR,
                                    "Unknown bridge attribute %d passed\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Get bridge statistics counters.
 *
 * @param[in] bridge_id Bridge id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 * @param[out] counters Array of resulting counter values.
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_bridge_stats(_In_ sai_object_id_t bridge_id,
                          _In_ uint32_t number_of_counters,
                          _In_ const sai_bridge_stat_t *counter_ids,
                          _Out_ uint64_t *counters)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Clear bridge statistics counters.
 *
 * @param[in] bridge_id Bridge id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_clear_bridge_stats(_In_ sai_object_id_t bridge_id,
                            _In_ uint32_t number_of_counters,
                            _In_ const sai_bridge_stat_t *counter_ids)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Get bridge port statistics counters.
 *
 * @param[in] bridge_port_id Bridge port id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 * @param[out] counters Array of resulting counter values.
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_bridge_port_stats(_In_ sai_object_id_t bridge_port_id,
                               _In_ uint32_t number_of_counters,
                               _In_ const sai_bridge_port_stat_t *counter_ids,
                               _Out_ uint64_t *counters)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/**
 * @brief Clear bridge port statistics counters.
 *
 * @param[in] bridge_port_id Bridge port id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_clear_bridge_port_stats(_In_ sai_object_id_t bridge_port_id,
                                 _In_ uint32_t number_of_counters,
                                 _In_ const sai_bridge_port_stat_t *counter_ids)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

/*
################################################################################
#                              Internal functions                              #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_bridge_info()
{
    sai_status_t rv;
    _brcm_sai_data_t gdata;
    _brcm_sai_list_data_t bdata;
    int bpid;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);

    rv = _brcm_sai_global_data_get(_BRCM_SAI_BRIDGE_LAG_PORTS, &gdata);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports global data get", rv);

    if (gdata.u32)
    {
        rv = _brcm_sai_list_init(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, 0,
                                 gdata.u32, NULL);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "list init bridge lag ports", rv);
        bdata.bdg_lag_ports = NULL;
        while (SAI_STATUS_SUCCESS ==
                 _brcm_sai_list_traverse(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, &bdata, &bdata))
        {
            if (bdata.bdg_lag_ports->vid_count)
            {
                bpid = BRCM_SAI_GET_OBJ_VAL(int, bdata.bdg_lag_ports->oid) + 1;
                rv = _brcm_sai_list_init(_BRCM_SAI_LIST_LAG_BP_VLAN_LIST, bpid,
                                         bdata.bdg_lag_ports->vid_count,
                                        (void **)&bdata.bdg_lag_ports->vid_list);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "list init lag bridge port vlan list", rv);
            }
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

sai_status_t
_brcm_sai_free_bridge_info()
{
    sai_status_t rv;
    _brcm_sai_data_t gdata;
    _brcm_sai_list_data_t bdata;
    int bpid;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BRIDGE);

    rv = _brcm_sai_global_data_get(_BRCM_SAI_BRIDGE_LAG_PORTS, &gdata);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports global data get", rv);

    if (gdata.u32)
    {
        DATA_CLEAR(bdata,  _brcm_sai_list_data_t);
        /* Free all LAG BP VLAN lists */
        while (SAI_STATUS_SUCCESS ==
                 _brcm_sai_list_traverse(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, &bdata, &bdata))
        {
            if (bdata.bdg_lag_ports->vid_count)
            {
                bpid = BRCM_SAI_GET_OBJ_VAL(int, bdata.bdg_lag_ports->oid) + 1;
                rv = _brcm_sai_list_free(_BRCM_SAI_LIST_LAG_BP_VLAN_LIST, bpid,
                                         bdata.bdg_lag_ports->vid_count,
                                        (void *)bdata.bdg_lag_ports->vid_list);
                BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "list free lag bridge port vlan list", rv);
            }
        }
        rv = _brcm_sai_list_free(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, 0,
                                 gdata.u32, NULL);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "list free bridge lag ports", rv);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BRIDGE);
    return rv;
}

sai_status_t
_brcm_sai_bridge_port_valid(sai_object_id_t bridge_port_id, bool *found)
{
    int idx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t idata;

    if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_GET_OBJ_MAP(bridge_port_id))
    {
        idx = BRCM_SAI_GET_OBJ_VAL(int, bridge_port_id);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                        &idx, &idata);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "port info data get", rv);
        *found = (_BP_DELETED != idata.port_info.bdg_port) ? TRUE : FALSE;
    }
    else
    {
        _brcm_sai_list_key_t list_key;
        _brcm_sai_list_data_t ldata;

        list_key.obj_id = bridge_port_id;
        rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key,
                                &ldata);
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "sai bridge lag port list get.", rv);
        *found = TRUE;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_bridge_lag_port_vlan_add(sai_object_id_t bridge_port_id, int vid, int utag)
{
    sai_status_t rv;
    _brcm_sai_list_data_t ldata, base, vdata;
    _brcm_sai_list_key_t list_key;
    _brcm_sai_bridge_lag_port_t lag_port;
    _brcm_sai_lag_bp_vlan_info_t lag_vlan_info;

    DATA_CLEAR(lag_port,  _brcm_sai_bridge_lag_port_t);
    DATA_CLEAR(lag_vlan_info,  _brcm_sai_lag_bp_vlan_info_t);

    list_key.obj_id = bridge_port_id;
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key,
                            &ldata);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "sai bridge lag port list get", rv);

    lag_port.oid = bridge_port_id;
    lag_port.learn_flags = ldata.bdg_lag_ports->learn_flags;
    lag_port.bridge_port_state = ldata.bdg_lag_ports->bridge_port_state;
    lag_port.vid_count = ldata.bdg_lag_ports->vid_count;
    lag_port.next = NULL;
    /* Add VLAN to the LAG's VLAN list */
    base.vid_list = ldata.bdg_lag_ports->vid_list;
    rv = _brcm_sai_list_del(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list del", rv);
    lag_vlan_info.vid = vid;
    lag_vlan_info.utag = utag;
    lag_vlan_info.next = NULL;
    vdata.vid_list = &lag_vlan_info;
    rv = _brcm_sai_list_add(_BRCM_SAI_LIST_LAG_BP_VLAN_LIST,
                            &base, NULL, &vdata);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag bridge port vlan list add", rv);
    lag_port.vid_list = base.vid_list;
    lag_port.vid_count++;
    /* Update Bridge LAG port */
    ldata.bdg_lag_ports = &lag_port;
    rv = _brcm_sai_list_add(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL,
                            NULL, &ldata);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list add", rv);
    BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_DEBUG,
                        "Added bridge port 0x%16lx to vlan %d with "
                        "utag %d, vlan count %d\n", bridge_port_id, vid,
                        utag, lag_port.vid_count);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_bridge_lag_port_vlan_remove(sai_object_id_t bridge_port_id, int vid)
{
    sai_status_t rv;
    _brcm_sai_list_data_t ldata, base;
    _brcm_sai_list_key_t list_key;
    //_brcm_sai_bridge_lag_port_t lag_port;

    list_key.obj_id = bridge_port_id;
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key,
                            &ldata);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "sai bridge lag port list get", rv);
#if 0
    rv = _brcm_sai_list_del(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list del", rv);
    lag_port.oid = bridge_port_id;
    lag_port.learn_flags = ldata.bdg_lag_ports->learn_flags;
    lag_port.bridge_port_state = ldata.bdg_lag_ports->bridge_port_state;
    lag_port.vid_count = ldata.bdg_lag_ports->vid_count;
    lag_port.next = NULL;
#endif
    /* Remove VLAN from the LAG's VLAN list */
    base.vid_list = ldata.bdg_lag_ports->vid_list;
    list_key.vid = vid;
    rv = _brcm_sai_list_del(_BRCM_SAI_LIST_LAG_BP_VLAN_LIST,
                            &base, &list_key);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag bridge port vlan list delete", rv);
    ldata.bdg_lag_ports->vid_list = base.vid_list;
    //lag_port.vid_list = base.vid_list;
    //lag_port.vid_count--;
    ldata.bdg_lag_ports->vid_count--;
    if (0 == ldata.bdg_lag_ports->vid_count)
    {
        ldata.bdg_lag_ports->vid_list = NULL;
    }
#if 0
    /* Update Bridge LAG port */
    ldata.bdg_lag_ports = &lag_port;
    rv = _brcm_sai_list_add(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL,
                            NULL, &ldata);
    BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "bridge lag ports list add", rv);
#endif
    BRCM_SAI_LOG_BRIDGE(SAI_LOG_LEVEL_DEBUG,
                        "Removed bridge port 0x%16lx from vlan %d, "
                        "current vlan count %d\n", bridge_port_id, vid,
                        ldata.bdg_lag_ports->vid_count);

    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_bridge_api_t bridge_apis = {
    brcm_sai_create_bridge,
    brcm_sai_remove_bridge,
    brcm_sai_set_bridge_attribute,
    brcm_sai_get_bridge_attribute,
    brcm_sai_get_bridge_stats,
    NULL, /* get_bridge_stats_ext_fn */
    brcm_sai_clear_bridge_stats,
    brcm_sai_create_bridge_port,
    brcm_sai_remove_bridge_port,
    brcm_sai_set_bridge_port_attribute,
    brcm_sai_get_bridge_port_attribute,
    brcm_sai_get_bridge_port_stats,
    NULL, /* get_bridge_port_stats_ext_fn */
    brcm_sai_clear_bridge_port_stats
};
