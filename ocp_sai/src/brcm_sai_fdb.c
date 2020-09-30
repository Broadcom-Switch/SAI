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
sai_fdb_event_notification_fn _fdb_event = NULL;

/*
################################################################################
#                               Event handlers                                 #
################################################################################
*/
void
_brcm_sai_fdb_event_cb(int unit, bcm_l2_addr_t *l2addr, int operation,
                       void *userdata)
{
    uint32_t attr_count = 0;
    sai_attribute_t attr[2];
    sai_fdb_event_notification_data_t notify;

    if (l2addr)
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_DEBUG, "FDB event: %d for mac " MAC_FMT_STR"\n",
                         operation, MAC_PRINT_STR(l2addr->mac));
    }
    if ((BCM_L2_CALLBACK_LEARN_EVENT == operation)  ||
        (BCM_L2_CALLBACK_ADD == operation))
    {
        notify.event_type = SAI_FDB_EVENT_LEARNED;
        if (SAI_STATUS_SUCCESS !=
            _brcm_sai_global_data_bump(_BRCM_SAI_FDB_COUNT, INC))
        {
            BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR,
                             "Error incrementing fdb count global data\n");
        }
    }
    else if ((BCM_L2_CALLBACK_AGE_EVENT == operation) ||
             (BCM_L2_CALLBACK_DELETE == operation))
    {
        notify.event_type = SAI_FDB_EVENT_AGED;
        if (SAI_STATUS_SUCCESS !=
            _brcm_sai_global_data_bump(_BRCM_SAI_FDB_COUNT, DEC))
        {
            BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR,
                             "Error decrementing fdb count global data\n");
        }
    }
    else
    {
        return;
    }
    attr[attr_count].id = SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID;
    if (l2addr->flags & BCM_L2_TRUNK_MEMBER)
    {
        attr[attr_count++].value.oid = 
            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                        SAI_BRIDGE_PORT_TYPE_PORT,
                                        SAI_OBJECT_TYPE_LAG, l2addr->tgid);
    }
    else
    {
        attr[attr_count++].value.oid = 
            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                        SAI_BRIDGE_PORT_TYPE_PORT,
                                        SAI_OBJECT_TYPE_PORT, l2addr->port);
    }

    /* Nbr and NH update processing */
    _brcm_sai_nbr_mac_db_update(SAI_FDB_EVENT_LEARNED == notify.event_type ? 
                                TRUE : FALSE, l2addr);

    if (IS_NULL(_fdb_event))
    {
        return;
    }
    notify.fdb_entry.switch_id = _brcm_sai_switch_id_get(unit);
    sal_memcpy(notify.fdb_entry.mac_address, l2addr->mac, sizeof(sai_mac_t));
    notify.fdb_entry.bv_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VLAN, l2addr->vid);
    notify.attr_count = attr_count;
    notify.attr = attr;
    _fdb_event(1, &notify);
}


/*
################################################################################
#                               FDB functions                                  #
################################################################################
*/
/*
* Routine Description:
*    Create FDB entry
*
* Arguments:
*    [in] fdb_entry - fdb entry
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_fdb_entry(_In_ const sai_fdb_entry_t* fdb_entry,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list)
{
    int i;
    sai_status_t rv;
    bcm_l2_addr_t l2addr;
    bool drop = FALSE, have_port_id = FALSE;
    sai_vlan_id_t vlan_id = 0;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_FDB);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(fdb_entry);

    if (BRCM_SAI_MAC_IS_ZERO(fdb_entry->mac_address))
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_INFO, "Null mac address passed.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (BRCM_SAI_MAC_IS_BCAST(fdb_entry->mac_address))
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_INFO, "BCAST mac address not supported.\n");
        return SAI_STATUS_NOT_SUPPORTED;
    }
    if (BRCM_SAI_MAC_IS_MCAST(fdb_entry->mac_address))
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_INFO, "MCAST mac address not supported.\n");
        return SAI_STATUS_NOT_SUPPORTED;
    }
    if (fdb_entry->bv_id)
    {
        if (BRCM_SAI_CHK_OBJ_MISMATCH(fdb_entry->bv_id, SAI_OBJECT_TYPE_VLAN))
        {
            BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR,
                             "Invalid object type 0x%16lx passed\n",
                             fdb_entry->bv_id);
            return SAI_STATUS_INVALID_OBJECT_TYPE;
        }
    }
    vlan_id = BRCM_SAI_GET_OBJ_VAL(sai_vlan_id_t, fdb_entry->bv_id);
    bcm_l2_addr_t_init(&l2addr, fdb_entry->mac_address, vlan_id);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_FDB_ENTRY_ATTR_TYPE:
                if (SAI_FDB_ENTRY_TYPE_STATIC == attr_list[i].value.s32)
                {
                    l2addr.flags |= BCM_L2_STATIC;
                }
                break;
            case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
                if (SAI_OBJECT_TYPE_BRIDGE_PORT == BRCM_SAI_ATTR_LIST_OBJ_TYPE(i))
                {
                    if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_ATTR_LIST_OBJ_MAP(i))
                    {
                        l2addr.port = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_DEBUG, "L2 port: %d\n", l2addr.port);
                    }
                    else /* LAG */
                    {
                        l2addr.flags |= BCM_L2_TRUNK_MEMBER;
                        l2addr.tgid = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_DEBUG, "L2 LAG: %d\n", l2addr.tgid);
                    }
                }
                else
                {
                    BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_CRITICAL,
                                     "Invalid object type %d passed\n",
                                     BRCM_SAI_ATTR_LIST_OBJ_TYPE(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                have_port_id = TRUE;
                break;
            case SAI_FDB_ENTRY_ATTR_PACKET_ACTION:
                if (SAI_PACKET_ACTION_DROP == attr_list[i].value.s32)
                {
                    drop = TRUE;
                    l2addr.flags |= BCM_L2_DISCARD_DST;
                }
                else if (SAI_PACKET_ACTION_FORWARD != attr_list[i].value.s32)
                {
                    BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_CRITICAL,
                                     "Bad pkt action attribute passed\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            case SAI_FDB_ENTRY_ATTR_CUSTOM_RANGE_START:
                l2addr.flags |= BCM_L2_L3LOOKUP;
                break;
            default: 
                BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_INFO,
                                 "Un-supported attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }    
    if (FALSE == have_port_id && FALSE == drop)
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_CRITICAL, "Missing L2 info.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    rv = bcm_l2_addr_add(0, &l2addr);
    if(rv != BCM_E_EXISTS)
    {
        BRCM_SAI_API_CHK(SAI_API_FDB, "Create FDB", rv);
        if (!DEV_IS_TD3() && !DEV_IS_TH3() && !DEV_IS_HX4())
        {
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_FDB_COUNT, INC);
            BRCM_SAI_RV_CHK(SAI_API_FDB, "fdb global count inc", rv);
        }
        _brcm_sai_nbr_mac_db_update(TRUE, &l2addr);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_FDB);
    return BRCM_RV_BCM_TO_SAI(rv);
}

/*
* Routine Description:
*    Remove FDB entry
*
* Arguments:
*    [in] fdb_entry - fdb entry
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_fdb_entry(_In_ const sai_fdb_entry_t* fdb_entry)
{
    sai_status_t rv;
    bcm_mac_t mac;
    bcm_vlan_t vid;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_FDB);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(fdb_entry))
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_CRITICAL, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (fdb_entry->bv_id)
    {
        if (BRCM_SAI_CHK_OBJ_MISMATCH(fdb_entry->bv_id, SAI_OBJECT_TYPE_VLAN))
        {
            BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR,
                             "Invalid object type 0x%16lx passed\n",
                             fdb_entry->bv_id);
            return SAI_STATUS_INVALID_OBJECT_TYPE;
        }
    }
    sal_memcpy(mac, fdb_entry->mac_address, sizeof(bcm_mac_t));
    vid = BRCM_SAI_GET_OBJ_VAL(bcm_vlan_t, fdb_entry->bv_id);
    rv = bcm_l2_addr_delete(0, mac, vid);
    BRCM_SAI_API_CHK(SAI_API_FDB, "Remove FDB", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_FDB);
    return BRCM_RV_BCM_TO_SAI(rv);
}

/*
* Routine Description:
*    Set fdb entry attribute value
*
* Arguments:
*    [in] fdb_entry - fdb entry
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_fdb_entry_attribute(_In_ const sai_fdb_entry_t* fdb_entry,
                                 _In_ const sai_attribute_t *attr)
{
    sai_status_t rv;
    bcm_l2_addr_t l2addr;
    sai_vlan_id_t vlan_id;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_FDB);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(fdb_entry) || IS_NULL(attr))
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_CRITICAL, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    vlan_id = BRCM_SAI_GET_OBJ_VAL(sai_vlan_id_t, fdb_entry->bv_id);
    bcm_l2_addr_t_init(&l2addr, fdb_entry->mac_address, vlan_id);
    rv = bcm_l2_addr_get(0, (sai_uint8_t *)fdb_entry->mac_address, vlan_id, &l2addr);
    BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr get", rv);
    switch (attr->id)
    {
        case SAI_FDB_ENTRY_ATTR_TYPE:
            if (SAI_FDB_ENTRY_TYPE_STATIC == attr->value.s32)
            {
                l2addr.flags |= BCM_L2_STATIC;
            }
            else
            {
                l2addr.flags &= BCM_L2_STATIC;
            }
            rv = bcm_l2_addr_add(0, &l2addr);
            BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr add", rv);
            break;
        case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
            if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_GET_OBJ_MAP(attr->value.oid))
            {
                l2addr.flags &= ~BCM_L2_TRUNK_MEMBER;
                l2addr.port = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
                BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_DEBUG, "L2 port: %d\n", l2addr.port);
            }
            else /* LAG */
            {
                l2addr.flags |= BCM_L2_TRUNK_MEMBER;
                l2addr.tgid = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
                BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_DEBUG, "L2 LAG: %d\n", l2addr.tgid);
            }
            rv = bcm_l2_addr_add(0, &l2addr);
            BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr add", rv);
            /* Trigger neighbor updates. */
            _brcm_sai_nbr_mac_db_update(FALSE, &l2addr);
            _brcm_sai_nbr_mac_db_update(TRUE, &l2addr);
            break;
        default: 
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_FDB);
    return rv;
}

/*
* Routine Description:
*    Get fdb entry attribute value
*
* Arguments:
*    [in] fdb_entry - fdb entry
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_fdb_entry_attribute(_In_ const sai_fdb_entry_t* fdb_entry,
                                 _In_ uint32_t attr_count,
                                 _Inout_ sai_attribute_t *attr_list)
{
    int i;
    sai_status_t rv;
    bcm_l2_addr_t l2addr;
    bcm_mac_t mac;
    bcm_vlan_t vid;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_FDB);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;

    if (IS_NULL(fdb_entry))
    {
        BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_CRITICAL, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (fdb_entry->bv_id)
    {
        if (BRCM_SAI_CHK_OBJ_MISMATCH(fdb_entry->bv_id, SAI_OBJECT_TYPE_VLAN))
        {
            BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR,
                             "Invalid object type 0x%16lx passed\n",
                             fdb_entry->bv_id);
            return SAI_STATUS_INVALID_OBJECT_TYPE;
        }
    }
    sal_memcpy(mac, fdb_entry->mac_address, sizeof(bcm_mac_t));
    vid = BRCM_SAI_GET_OBJ_VAL(bcm_vlan_t, fdb_entry->bv_id);
    sal_memset(&l2addr, 0, sizeof(bcm_l2_addr_t));
    rv = bcm_l2_addr_get(0, mac, vid, &l2addr);
    BRCM_SAI_API_CHK(SAI_API_FDB, "FDB attrib get", rv);

    for (i=0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_FDB_ENTRY_ATTR_TYPE:
                if (l2addr.flags & BCM_L2_STATIC)
                {
                    attr_list[i].value.s32 = SAI_FDB_ENTRY_TYPE_STATIC;
                }
                else
                {
                    attr_list[i].value.s32 = SAI_FDB_ENTRY_TYPE_DYNAMIC;
                }
                break;
            case SAI_FDB_ENTRY_ATTR_BRIDGE_PORT_ID:
                if (l2addr.flags & BCM_L2_TRUNK_MEMBER)
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) =
                        BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                                    SAI_BRIDGE_PORT_TYPE_PORT,
                                                    SAI_OBJECT_TYPE_LAG, l2addr.tgid);
                }
                else
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) =
                        BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                                    SAI_BRIDGE_PORT_TYPE_PORT,
                                                    SAI_OBJECT_TYPE_PORT, l2addr.port);
                }
                break;
            default:
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_FDB);
    return BRCM_RV_BCM_TO_SAI(rv);
}


/*
* Routine Description:
*    Remove all FDB entries by attribute set in sai_fdb_flush_attr
*
* Arguments:
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_flush_fdb_entries(_In_ sai_object_id_t switch_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    sai_object_id_t oid = 0;
    sai_vlan_id_t vlan_id = 0;
    int i, port_tid;
    bool e;
    uint8_t type;
    uint32_t flags = BCM_L2_DELETE_STATIC;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_FDB);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case  SAI_FDB_FLUSH_ATTR_BRIDGE_PORT_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_BRIDGE_PORT))
                {
                    BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR,
                                     "Invalid Bridge Port object type 0x%16lx passed\n",
                                     BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                oid = BRCM_SAI_ATTR_LIST_OBJ(i);
                port_tid = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_port_t, i);
                break;
            case SAI_FDB_FLUSH_ATTR_BV_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_VLAN))
                {
                    BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR,
                                     "Invalid VLAN object type 0x%16lx passed\n",
                                     BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                vlan_id = BRCM_SAI_ATTR_LIST_OBJ_VAL(sai_vlan_id_t, i);
                break;
            case SAI_FDB_FLUSH_ATTR_ENTRY_TYPE:
                if (SAI_FDB_FLUSH_ENTRY_TYPE_STATIC == attr_list[i].value.s32)
                {
                    return SAI_STATUS_NOT_SUPPORTED;
                }
                flags = 0;
                break;
            default:
                BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_DEBUG,
                                 "Unsupported attribute %d passed\n",
                                 attr_list[i].id);
                break;
        }
    }
    if ((0 == vlan_id) && (0 == oid))
    {
        bcm_pbmp_t pbmp;
        int p;

        /* Clear entire table */
        _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
        BCM_PBMP_ITER(pbmp, p)
        {
            rv = bcm_l2_addr_delete_by_port(0, 0, p, flags);
            BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr delete by port", rv);
        }
    }
    else if ((0 != vlan_id) && (0 != oid))
    {
        rv = _brcm_sai_bridge_port_valid(oid, &e);
        BRCM_SAI_RV_CHK(SAI_API_FDB, "bridge port valid", rv);
        if (FALSE == e)
        {
            BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR, "Invalid bridge port 0x%lx\n", oid);
            return SAI_STATUS_INVALID_PARAMETER;
        }
        type = BRCM_SAI_GET_OBJ_MAP(oid);
        if (SAI_OBJECT_TYPE_PORT == type)
        {
            rv = bcm_l2_addr_delete_by_vlan_port(0, vlan_id, 0, port_tid, flags);
            BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr delete by vlan port", rv);
        }
        else
        {
            rv = bcm_l2_addr_delete_by_vlan_trunk(0, vlan_id, port_tid, flags);
            BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr delete by vlan trunk", rv);
        }
    }
    else if (0 != oid)
    {
        rv = _brcm_sai_bridge_port_valid(oid, &e);
        BRCM_SAI_RV_CHK(SAI_API_FDB, "bridge port valid", rv);
        if (FALSE == e)
        {
            BRCM_SAI_LOG_FDB(SAI_LOG_LEVEL_ERROR, "Invalid bridge port 0x%lx\n", oid);
            return SAI_STATUS_INVALID_PARAMETER;
        }
        type = BRCM_SAI_GET_OBJ_MAP(oid);
        if (SAI_OBJECT_TYPE_PORT == type)
        {
            rv = bcm_l2_addr_delete_by_port(0, 0, port_tid, flags);
            BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr delete by port", rv);
        }
        else
        {
            rv = bcm_l2_addr_delete_by_trunk(0, port_tid, flags);
            BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr delete by trunk", rv);
        }
    }
    else
    {
        rv = bcm_l2_addr_delete_by_vlan(0, vlan_id, flags);
        BRCM_SAI_API_CHK(SAI_API_FDB, "l2 addr delete by vlan", rv);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_FDB);
    return rv;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_fdb_api_t fdb_apis = {
    brcm_sai_create_fdb_entry,
    brcm_sai_remove_fdb_entry,
    brcm_sai_set_fdb_entry_attribute,
    brcm_sai_get_fdb_entry_attribute,
    brcm_sai_flush_fdb_entries
};
