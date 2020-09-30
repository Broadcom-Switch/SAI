/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

static uint32_t _num_vlan_bytes = 0;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC inline sai_status_t
_brcm_sai_vlan_bmp_set(sai_vlan_id_t vlan_id);
STATIC inline sai_status_t
_brcm_sai_vlan_bmp_clear(sai_vlan_id_t vlan_id);
STATIC sai_status_t
_brcm_sai_vlan_bmp_init(_In_ sai_vlan_id_t vlan_id);

/*
################################################################################
#                                 Vlan functions                               #
################################################################################
*/
/*  
 * Casting SAI vlan type to OPENNSL vlan type. Using this so that
 * later if someone decides to change the size of vlan type in SAI,
 * all we need to do is change this here.
 */
#define VLAN_CAST(__vlan_id) (bcm_vlan_t)((__vlan_id))
#define VLAN_ID_CHECK(__vid) \
    ((_BRCM_SAI_VR_MAX_VID >= (__vid)) && (0 < (__vid)))

/*
* Routine Description:
*    Create a VLAN
*
* Arguments:
 * @param[out] vlan_id VLAN ID
 * @param[in] switch_id Switch id
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_vlan(_Out_ sai_object_id_t *vlan_id,
                     _In_ sai_object_id_t switch_id,
                     _In_ uint32_t attr_count,
                     _In_ const sai_attribute_t *attr_list)
{
    int unit = 0, rv = 0, i;
    sai_vlan_id_t vid = -1;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(vlan_id);
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_VLAN_ATTR_VLAN_ID:
            {
                vid = attr_list[i].value.u16;
                break;
            }
            default:
                BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                                 "Unknown VLAN attribute %d passed\n", attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_INFO,
                             "Error processing VLAN attributes\n");
            return rv;
        }
    }
    if (FALSE == VLAN_ID_CHECK(vid))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "VLAN create failed ID %d\n",
                          (int)vid);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    rv =  bcm_vlan_create(unit, VLAN_CAST(vid));
    BRCM_SAI_API_CHK(SAI_API_VLAN, "vlan create", rv);
    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Create vid: %d\n", vid);

    /* Add vlan to internal list of vlan bitmap */
    rv = _brcm_sai_vlan_bmp_set(vid);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp set", rv);

    *vlan_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VLAN, vid);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description:
*    Remove a VLAN
*
* Arguments:
*    [in] vlan_id - VLAN id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_vlan(_In_ sai_object_id_t vlan_id)
{
    int rv;
    int unit = 0;
    sai_vlan_id_t vid;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;
    
    if (BRCM_SAI_CHK_OBJ_MISMATCH(vlan_id, SAI_OBJECT_TYPE_VLAN))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%16lx passed\n",
                          vlan_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    vid = BRCM_SAI_GET_OBJ_VAL(sai_vlan_id_t, vlan_id);
    rv = bcm_vlan_destroy(unit, VLAN_CAST(vid));
    if (BCM_FAILURE(rv))
    {
        if (BCM_E_BADID == rv)
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Cannot remove default VLAN %d\n",
                              (int)vid);
        }
        else if (BCM_E_NOT_FOUND == rv)
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Cannot remove VLAN %d, NOT FOUND\n",
                              (int)vid);
        }
        else
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Error %s removing VLAN %d\n",
                              bcm_errmsg(rv), (int)vid);
        }
    }
    
    rv = _brcm_sai_vlan_bmp_clear(vid);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp clear", rv);
    
    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Remove vid: %d\n", vid);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return BRCM_RV_BCM_TO_SAI(rv);
}


/*
* Routine Description:
*    Set VLAN attribute Value
*
* Arguments:
*    [in] vlan_id - VLAN id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_vlan_attribute(_In_ sai_object_id_t vlan_id,
                            _In_ const sai_attribute_t *attr)
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;
    
    switch(attr->id)
    {
        case SAI_VLAN_ATTR_INGRESS_ACL:
            rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_PTR_OBJ(), INGRESS,
                                        vlan_id);
            BRCM_SAI_RV_CHK(SAI_API_VLAN, "ACL obj VLAN bind", rv);
            break;
        case SAI_VLAN_ATTR_EGRESS_ACL:
            rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_PTR_OBJ(), EGRESS,
                                        vlan_id);
            BRCM_SAI_RV_CHK(SAI_API_VLAN, "ACL obj VLAN bind", rv);
            break;
        default:
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, 
                             "Unknown vlan attribute %d passed\n", attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
    }
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/*
* Routine Description:
*    Get VLAN attribute Value
*
* Arguments:
*    [in] vlan_id - VLAN id
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_vlan_attribute(_In_ sai_object_id_t vlan_id,
                            _In_ uint32_t attr_count,
                            _Inout_ sai_attribute_t *attr_list)
{
    int i;
    sai_vlan_id_t vid;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(vlan_id, SAI_OBJECT_TYPE_VLAN))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%16lx passed\n",
                          vlan_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    vid = BRCM_SAI_GET_OBJ_VAL(sai_vlan_id_t, vlan_id);
    for (i=0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_VLAN_ATTR_VLAN_ID:
                attr_list[i].value.u16 = vid;
                break;
            case SAI_VLAN_ATTR_MEMBER_LIST:
            {
                int limit, index = 0;
                _brcm_sai_table_data_t tdata;
                _brcm_sai_vlan_membr_list_t *ptr;
                _brcm_sai_vlan_membr_info_t vlan_members;

                DATA_CLEAR(vlan_members, _brcm_sai_vlan_membr_info_t);
                vlan_members.vid = vid;
                tdata.vlan_membrs = &vlan_members;
                rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
                if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
                {
                    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "error in vlan member table lookup.\n");
                    return rv;
                }
                rv = SAI_STATUS_SUCCESS;
                if (attr_list[i].value.objlist.count < vlan_members.membrs_count)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                if (vlan_members.membrs_count)
                {
                    limit = (attr_list[i].value.objlist.count < vlan_members.membrs_count) ?
                            attr_list[i].value.objlist.count-1 : vlan_members.membrs_count-1;
                    attr_list[i].value.objlist.count = vlan_members.membrs_count;
                    ptr = vlan_members.membrs;
                    while (ptr)
                    {
                        if (index > limit)
                        {
                            break;
                        }
                        if (SAI_OBJECT_TYPE_PORT == ptr->membr.type)
                        {
                            attr_list[i].value.objlist.list[index] =
                                BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_VLAN_MEMBER,
                                                            SAI_OBJECT_TYPE_PORT, ptr->membr.val, vid);
                        }
                        else
                        {
                            attr_list[i].value.objlist.list[index] =
                                BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_VLAN_MEMBER,
                                                            SAI_OBJECT_TYPE_LAG, ptr->membr.val, vid);
                        }
                        index++;                       
                        ptr = ptr->next;
                    }
                }
                else
                {
                    attr_list[i].value.objlist.count = 0;
                }
                break;
            }
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_BUFFER_OVERFLOW != rv)
        {
            BRCM_SAI_RV_CHK(SAI_API_VLAN, "Error processsing vlan attributes", rv);
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/*
    \brief Create VLAN Member
    \param[out] vlan_member_id VLAN member ID
    \param[in] attr_count number of attributes
    \param[in] attr_list array of attributes
    \return Success: SAI_STATUS_SUCCESS
            Failure: failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_vlan_member(_Out_ sai_object_id_t* vlan_member_id,
                            _In_ sai_object_id_t switch_id,
                            _In_ uint32_t attr_count,
                            _In_ const sai_attribute_t *attr_list)
{
    bool e;
    uint8_t type;
    bcm_pbmp_t pbm, upbm;
    sai_object_id_t oid = 0;
    sai_vlan_id_t vlan_id = -1;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_vlan_membr_list_t *ptr;
    _brcm_sai_vlan_membr_list_t *membr;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_vlan_membr_info_t vlan_members;
    int i, port_tid, utag = SAI_VLAN_TAGGING_MODE_UNTAGGED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(vlan_member_id);

    for (i=0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_VLAN_MEMBER_ATTR_VLAN_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_VLAN))
                {
                    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                                      "Invalid object type 0x%16lx passed\n",
                                      BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                vlan_id = BRCM_SAI_ATTR_LIST_OBJ_VAL(sai_uint16_t, i);
                break;
            case SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_BRIDGE_PORT))
                {
                    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                                      "Invalid object type 0x%16lx passed\n",
                                      BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                oid = BRCM_SAI_ATTR_LIST_OBJ(i);
                port_tid = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_port_t, i);
                break;
            case SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE:
                utag = attr_list[i].value.s32;
                break;
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
    }
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "Error processsing vlan attributes", rv);
    if (-1 == vlan_id || 0 == oid)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Vlan or bridge port not provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    /* Check if VLAN exists */
    rv = _brcm_sai_vlan_exists(vlan_id, &e);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan exists", rv);
    if (FALSE == e)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Invalid vlan id %u\n", (int)vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* Check if bridge port is valid */
    rv = _brcm_sai_bridge_port_valid(oid, &e);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "bridge port valid", rv);
    if (FALSE == e)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Invalid bridge port 0x%lx\n", oid);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    DATA_CLEAR(vlan_members, _brcm_sai_vlan_membr_info_t);
    vlan_members.vid = vlan_id;
    tdata.vlan_membrs = &vlan_members;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "error in vlan member table lookup.\n");
        return rv;
    }

    /* Check to see if the member is already exist, if so, return with success */
    ptr = tdata.vlan_membrs->membrs;
    while (ptr)
    {
        if (port_tid == ptr->membr.val)
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Adding vlan member %d to vlan %d already exists.\n",
                              port_tid, vlan_id);
            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        }
        ptr = ptr->next;
    }

    BCM_PBMP_CLEAR(pbm);
    BCM_PBMP_CLEAR(upbm);
    type = BRCM_SAI_GET_OBJ_MAP(oid);
    if (SAI_OBJECT_TYPE_PORT == type)
    {
        if (SAI_VLAN_TAGGING_MODE_UNTAGGED == utag)
        {
            BCM_PBMP_PORT_ADD(upbm, port_tid);
        }
        BCM_PBMP_PORT_ADD(pbm, port_tid);
    }
    else /* LAG */
    {
        bcm_trunk_member_t *members;
        bcm_trunk_info_t trunk_info;
        int p, port, max, actual = 0;
        _brcm_sai_indexed_data_t data;
        bcm_trunk_chip_info_t trunk_chip_info;
        
        type = SAI_OBJECT_TYPE_LAG;
        /* Ensure LAG is not already part of an internal RIF/vlan */
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &port_tid, &data);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "lag rif data get", rv);
        if (0 != data.lag_info.rif_obj)
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "LAG is part of a RIF.\n");
            return SAI_STATUS_INVALID_PARAMETER;
        }
        rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
        BRCM_SAI_API_CHK(SAI_API_VLAN, "trunk chip info get", rv);
        max = trunk_chip_info.trunk_group_count;
        members = ALLOC(sizeof(bcm_trunk_member_t) * max);
        if (IS_NULL(members))
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_CRITICAL,
                              "Error allocating memory for lag members.\n");
            return SAI_STATUS_NO_MEMORY;
        }
        rv = bcm_trunk_get(0, port_tid, &trunk_info, max, members, &actual);
        BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "trunk get", rv, members);
        /* Add port(s) to vlan */
        for (p=0; p<actual; p++)
        {
            rv = bcm_port_local_get(0, members[p].gport, &port);
            BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "port local get",
                                  rv, members);
            BCM_PBMP_PORT_ADD(pbm, port);
            if (SAI_VLAN_TAGGING_MODE_UNTAGGED == utag)
            {
                BCM_PBMP_PORT_ADD(upbm, port);
            }
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG,
                              "Adding trunk %d port id %d to vid %d\n",
                              port_tid, port, vlan_id);
        }
        CHECK_FREE(members);
        rv = _brcm_sai_bridge_lag_port_vlan_add(oid, vlan_id, utag);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "bridge lag port vlan add", rv);
    }
    rv = bcm_vlan_port_add(0, VLAN_CAST(vlan_id), pbm, upbm);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Error %s adding port(s) to vlan id %u\n",
                          bcm_errmsg(rv), (int)vlan_id);
    }
    membr = ALLOC_CLEAR(1, sizeof(_brcm_sai_vlan_membr_list_t));
    if (IS_NULL(membr))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                          sizeof(_brcm_sai_vlan_membr_list_t));
        return SAI_STATUS_NO_MEMORY;
    }
    membr->membr.type = type;
    membr->membr.val = port_tid;
    if (IS_NULL(vlan_members.membrs))
    {
        vlan_members.membrs = membr;
    }
    else
    {
        vlan_members.membrs_end->next = membr;
    }
    vlan_members.membrs_end = membr;
    vlan_members.membrs_count++;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan members info db table entry add", rv);
    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Added member type %d val %d "
                      "to vlan %d", type, port_tid, vlan_id);
    /* Check if this vlan is used as a vlan-rif and has a bcast_ip,
     * If so then add this member (port/lag) to it.
     */
    i = vlan_id;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                    &i, &data);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan rif table get", rv);
    if (0 != data.vlan_rif.rif_obj && data.vlan_rif.bcast_ip)
    {
        rv = _brcm_sai_nbr_bcast_member_add(&data.vlan_rif.nbr,
                 BRCM_SAI_GET_OBJ_VAL(bcm_if_t, data.vlan_rif.rif_obj),
                 type, port_tid);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "nbr bcast member add", rv);
    }
    *vlan_member_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_VLAN_MEMBER, 
                                                  type, port_tid, vlan_id);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/*
    \brief Remove VLAN Member
    \param[in] vlan_member_id VLAN member ID
    \return Success: SAI_STATUS_SUCCESS
            Failure: failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_vlan_member(_In_ sai_object_id_t vlan_member_id)
{
    uint8_t type;
    bcm_pbmp_t pbm;
    sai_status_t rv;
    bool found = FALSE;
    sai_vlan_id_t vlan_id;
    sai_object_id_t bp_oid; 
    int port_tid, actual = 0;
    _brcm_sai_indexed_data_t data;
    int idx;
    bool match = FALSE;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_vlan_membr_info_t vlan_members;
    _brcm_sai_vlan_membr_list_t *ptr, *prev;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(vlan_member_id, SAI_OBJECT_TYPE_VLAN_MEMBER))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%16lx passed\n",
                          vlan_member_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    port_tid = BRCM_SAI_GET_OBJ_MAP(vlan_member_id);
    type = ((BRCM_SAI_GET_OBJ_SUB_TYPE(vlan_member_id)) & 0xf);
    bp_oid = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                         SAI_BRIDGE_PORT_TYPE_PORT,
                                         type, port_tid);
    rv = _brcm_sai_bridge_port_valid(bp_oid, &found);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "bridge port valid", rv);
    if (FALSE == found)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Invalid bridge port 0x%lx\n", bp_oid);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    vlan_id = BRCM_SAI_GET_OBJ_VAL(sai_vlan_id_t, vlan_member_id);
    BCM_PBMP_CLEAR(pbm);
    if (SAI_OBJECT_TYPE_PORT == type)
    {
        BCM_PBMP_PORT_ADD(pbm, port_tid);
    }
    else /* LAG */
    {
        int p, port, max;
        bcm_trunk_member_t *members;
        bcm_trunk_info_t trunk_info;
        bcm_trunk_chip_info_t trunk_chip_info;
        
        rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
        BRCM_SAI_API_CHK(SAI_API_VLAN, "trunk chip info get", rv);
        max = trunk_chip_info.trunk_group_count;
        members = ALLOC(sizeof(bcm_trunk_member_t) * max);
        if (IS_NULL(members))
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_CRITICAL,
                              "Error allocating memory for lag members.\n");
            return SAI_STATUS_NO_MEMORY;
        }
        rv = bcm_trunk_get(0, port_tid, &trunk_info, max, members, &actual);
        BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "trunk get", rv, members);
        /* Add port(s) to vlan */
        for (p=0; p<actual; p++)
        {
            rv = bcm_port_local_get(0, members[p].gport, &port);
            BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "port local get",
                                  rv, members);
            BCM_PBMP_PORT_ADD(pbm, port);
        }
        CHECK_FREE(members);
    }

    /* remove from vlan member table */
    DATA_CLEAR(vlan_members, _brcm_sai_vlan_membr_info_t);
    vlan_members.vid = vlan_id;
    tdata.vlan_membrs = &vlan_members;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan member table lookup", rv);
    if (0 == vlan_members.membrs_count)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Zero associated egress objs found for this vlan %d.",
                          vlan_id);
        return SAI_STATUS_FAILURE;
    }
    ptr = vlan_members.membrs;
    if (1 == vlan_members.membrs_count)
    {
        if (type != vlan_members.membrs->membr.type || 
            port_tid != vlan_members.membrs->membr.val)
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "One and only vlan member "
                              "type: %d, val %d did not match incoming %d %d "
                              "for vlan %d", vlan_members.membrs->membr.type, 
                              vlan_members.membrs->membr.val,
                              type, port_tid, vlan_id);
            return SAI_STATUS_FAILURE;
        }
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Removing member type %d val %d "
                          "from vlan %d", type, port_tid, vlan_id);
        vlan_members.membrs_end = vlan_members.membrs = NULL;
        match = TRUE;
    }
    else
    {
        while (ptr)
        {
            if (type == ptr->membr.type &&
                port_tid == ptr->membr.val)
            {
                if (ptr == vlan_members.membrs)
                {
                    vlan_members.membrs = ptr->next;
                }
                else
                {
                    prev->next = ptr->next;
                }
                if (IS_NULL(ptr->next))
                {
                    vlan_members.membrs_end = prev;
                }
                BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Removing member type %d val %d "
                                  "from vlan %d", type, port_tid, vlan_id);
                match = TRUE;
                break;
            }
            prev = ptr;
            ptr = ptr->next;
        }
    }
    if (FALSE == match)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "vlan member type value (%d %d) "
                          "did not match for this vlan %d.", type, port_tid, vlan_id);
        return SAI_STATUS_FAILURE;
    }
    rv = bcm_vlan_port_remove(0, VLAN_CAST(vlan_id), pbm);
    if (BCM_FAILURE(rv))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Error %s removing port(s) from vlan id %u\n",
                          bcm_errmsg(rv), (int)vlan_id);
    }
    FREE_CLEAR(ptr);
    vlan_members.membrs_count--;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan members info db table entry add", rv);
    if (SAI_OBJECT_TYPE_LAG == type)
    {
        rv = _brcm_sai_bridge_lag_port_vlan_remove(bp_oid, vlan_id);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "bridge lag port vlan remove", rv);
    }
    /* Check if this vlan is used as a vlan-rif and has a bcast_ip,
     * If so then remove this member (port/lag) from it.
     */
    idx = vlan_id;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan rif table get", rv);
    if (0 != data.vlan_rif.rif_obj && data.vlan_rif.bcast_ip)
    {
        rv = _brcm_sai_nbr_bcast_member_remove(&data.vlan_rif.nbr, type, 
                                               port_tid);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "nbr bcast member remove", rv);
    }
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/*
    \brief Set VLAN Member Attribute
    \param[in] vlan_member_id VLAN member ID
    \param[in] attr attribute structure containing ID and value
    \return Success: SAI_STATUS_SUCCESS
            Failure: failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_vlan_member_attribute(_In_ sai_object_id_t vlan_member_id,
                                   _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;

    //FIXME: handle utag set for lag or port member

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/*
    \brief Get VLAN Member Attribute
    \param[in] vlan_member_id VLAN member ID
    \param[in] attr_count number of attributes
    \param[in,out] attr_list list of attribute structures containing ID and value
    \return Success: SAI_STATUS_SUCCESS
            Failure: failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_vlan_member_attribute(_In_ sai_object_id_t vlan_member_id,
                                   _In_ const uint32_t attr_count,
                                   _Inout_ sai_attribute_t *attr_list)
{
    uint8_t type;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, utag = SAI_VLAN_TAGGING_MODE_TAGGED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(vlan_member_id, SAI_OBJECT_TYPE_VLAN_MEMBER);
    if (BRCM_SAI_CHK_OBJ_MISMATCH(vlan_member_id, SAI_OBJECT_TYPE_VLAN_MEMBER))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%16lx passed\n",
                          vlan_member_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    for (i=0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_VLAN_MEMBER_ATTR_VLAN_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VLAN,
                                        BRCM_SAI_GET_OBJ_VAL(sai_vlan_id_t, vlan_member_id));
                break;
            case SAI_VLAN_MEMBER_ATTR_BRIDGE_PORT_ID:
                type = BRCM_SAI_GET_OBJ_SUB_TYPE(vlan_member_id);
                BRCM_SAI_ATTR_LIST_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT, 
                                                SAI_BRIDGE_PORT_TYPE_PORT,
                                                type, BRCM_SAI_GET_OBJ_MAP(vlan_member_id));
                break;
            case SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE:
            {
                uint8_t type;
                int vid, port;
                bcm_pbmp_t pbm;
                bcm_pbmp_t upbm;
                
                type = ((BRCM_SAI_GET_OBJ_SUB_TYPE(vlan_member_id)) & 0xf);
                vid = BRCM_SAI_GET_OBJ_VAL(sai_vlan_id_t, vlan_member_id);
                if (SAI_OBJECT_TYPE_PORT == type)
                {
                    port = BRCM_SAI_GET_OBJ_MAP(vlan_member_id);
                    rv  = bcm_vlan_port_get(0, vid, &pbm, &upbm);
                    BRCM_SAI_API_CHK(SAI_API_VLAN, "vlan port get", rv);
                    if (BCM_PBMP_MEMBER(upbm, port))
                    {
                        utag = SAI_VLAN_TAGGING_MODE_UNTAGGED;
                    }
                }
                else
                {
                    //FIXME: handle lag member
                    return SAI_STATUS_NOT_IMPLEMENTED;
                }
                attr_list[i].value.s32 = utag;
                break;
            }
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
    }
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "Error processsing vlan attributes", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/*
* Routine Description:
*   Get vlan statistics counters.
*
* Arguments:
*    [in] vlan_id - VLAN id
*    [in] counter_ids - specifies the array of counter ids
*    [in] number_of_counters - number of counters in the array
*    [out] counters - array of resulting counter values.
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_vlan_stats(_In_ sai_object_id_t vlan_id,
                        _In_ uint32_t number_of_counters,
                        _In_ const sai_vlan_stat_t *counter_ids,
                        _Out_ uint64_t* counters)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/**
 * Routine Description:
 *   @brief Clear vlan statistics counters.
 *
 * Arguments:
 *    @param[in] vlan_id - vlan id
 *    @param[in] counter_ids - specifies the array of counter ids
 *    @param[in] number_of_counters - number of counters in the array
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_clear_vlan_stats(_In_ sai_object_id_t vlan_id,
                          _In_ uint32_t number_of_counters,
                          _In_ const sai_vlan_stat_t *counter_ids)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_VLAN);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_VLAN);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
#ifdef VLAN_DEBUG
/*
* Routine Description:
*   Internal API to print vlan bitmap
*
* Arguments: None
* Return Values: None

*/
STATIC void _brcm_log_vlan_bmp()
{
    uint32_t i,j=0,pos=0;

    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Vlans - ");
    for (i=0; i<_num_vlan_bytes; i++)
    {
        if (vlan_bmp[i])
        {
            for (j=0; j<BYTE_SIZE; j++)
            {
                if (vlan_bmp[i] & (1 << j))
                {
                    pos = (j+1) + (i*BYTE_SIZE);
                    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "%d,", pos);
                }
            }
        }
    }
    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "\n");
    return;
}
#endif

sai_status_t 
_brcm_sai_alloc_vlan()
{
    sai_status_t rv;
    
    _num_vlan_bytes = BRCM_SAI_NUM_BYTES(_BRCM_SAI_VR_MAX_VID);
    rv =  _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_VLAN_BMP,
                                      _num_vlan_bytes);
    BRCM_SAI_RV_LVL_CHK(SAI_API_VLAN, SAI_LOG_LEVEL_CRITICAL, "vlan bmp data get", rv);
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_db_table_create(_BRCM_SAI_TABLE_VLAN_MEMBRS, _BRCM_SAI_VR_MAX_VID))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_CRITICAL,
                          "creating vlan member info table !!\n");
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_db_table_node_list_init(_BRCM_SAI_TABLE_VLAN_MEMBRS,
                                           _BRCM_SAI_LIST_VLAN_MEMBRS);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "creating vlan members lists", rv);

    return rv;
}

sai_status_t
_brcm_sai_free_vlan()
{
    sai_status_t rv;

    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_VLAN_MEMBRS,
                                           _BRCM_SAI_LIST_VLAN_MEMBRS);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "freeing vlan members lists", rv);

    return rv;
}

STATIC inline sai_status_t
_brcm_sai_vlan_bmp_set(sai_vlan_id_t vlan_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    
    if (VLAN_ID_CHECK((int)vlan_id))
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_BMP,
                                        0, &data);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp data get", rv);
        BRCM_SAI_SET_BIT(data.vlan_bmp, (int)vlan_id);
    }
    return SAI_STATUS_SUCCESS;
}

STATIC inline sai_status_t
_brcm_sai_vlan_bmp_clear(sai_vlan_id_t vlan_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    if (VLAN_ID_CHECK(vlan_id))
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_BMP,
                                        0, &data);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp data get", rv);
        BRCM_SAI_CLEAR_BIT(data.vlan_bmp, (int)vlan_id);
    }
    return SAI_STATUS_SUCCESS;
}

inline sai_status_t
_brcm_sai_vlan_exists(_In_ sai_vlan_id_t vlan_id, bool *e)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    if (VLAN_ID_CHECK(vlan_id))
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_BMP,
                                        0, &data);
        BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp data get", rv);
        *e = BRCM_SAI_IS_SET(data.vlan_bmp, vlan_id);
    }
    else
    {
        *e = FALSE;
    }
    
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vlan_bmp_init(_In_ sai_vlan_id_t vlan_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_BMP,
                                    0, &data);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp data get", rv);
    rv = _brcm_sai_vlan_bmp_set(vlan_id);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp set", rv);
    return SAI_STATUS_SUCCESS;
}

/*
  This API will be called when creating a port routing interface.
** NOT RE-ENTRANT **
 */
sai_status_t
_brcm_sai_get_max_unused_vlan_id(bcm_vlan_t *vid)
{
    uint32_t pos=0;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_BMP,
                                    0, &data);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan bmp data get", rv);
    BRCM_SAI_MAX_UNSET(data.vlan_bmp, pos, _BRCM_SAI_VR_MAX_VID);

    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG,
                      "Returning unset vlan %d\n", pos);
    
    *vid = VLAN_CAST(pos);
    return SAI_STATUS_SUCCESS;
}

/*
 * This API will be called when removing a LAG interface.
 ** NOT RE-ENTRANT **
 */
sai_status_t
_brcm_sai_get_vlanobj_from_vlan(bcm_vlan_t vid, sai_object_id_t *vlan_obj)
{
    *vlan_obj = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VLAN, vid);
    return SAI_STATUS_SUCCESS;
}

/*
* Routine Description: Internal API to init vlan module. Also adds cpu
*   + xe ports to Vlan 1
*
* Arguments: None
* Return Values: Returns error on failure
*                SAI_STATUS_SUCCESS on success.
*/
sai_status_t
_brcm_sai_vlan_init()
{
    bcm_vlan_t vid;
    bcm_pbmp_t pbmp;
    int p, unit = 0;
    sai_status_t rv;

    _brcm_sai_table_data_t tdata;
    _brcm_sai_vlan_membr_list_t *membr;
    _brcm_sai_vlan_membr_info_t vlan_members;

    /* Init the BRCM SAI vlan bitmap and set default vlan */
    rv = bcm_vlan_default_get(0, &vid);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Error getting default vid\n");
        return BRCM_RV_BCM_TO_SAI(rv);
    }
    rv = _brcm_sai_vlan_bmp_init(vid);
    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "vlan bmp init", rv);

    /* After switch init, go ahead and add all ports to default vlan 1*/
    _brcm_sai_switch_pbmp_fp_all_get(&pbmp);

    /* 1 is default VLAN during startup */
    rv = bcm_vlan_port_add(unit, 1, pbmp, pbmp);
    if (BCM_FAILURE(rv)) {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Failed to add ports to default VLAN. Error %s\n",
                          bcm_errmsg(rv));
        return BRCM_RV_BCM_TO_SAI(rv);
    }

    /* Add vlan member ports to default vlan */
    DATA_CLEAR(vlan_members, _brcm_sai_vlan_membr_info_t);
    BCM_PBMP_ITER(pbmp, p)
    {
        membr = ALLOC_CLEAR(1, sizeof(_brcm_sai_vlan_membr_list_t));
        if (IS_NULL(membr))
        {
            BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                              sizeof(_brcm_sai_vlan_membr_list_t));
            return SAI_STATUS_NO_MEMORY;
        }

        membr->membr.type = SAI_OBJECT_TYPE_PORT;
        membr->membr.val = p;
        if (IS_NULL(vlan_members.membrs))
        {
            vlan_members.membrs = membr;
        }
        else
        {
            vlan_members.membrs_end->next = membr;
        }

        vlan_members.membrs_end = membr;
        vlan_members.membrs_count++;
    }
    /* Add to DB after the list is prepared. */
    vlan_members.vid = 1;
    tdata.vlan_membrs = &vlan_members;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan members info db table entry add", rv);
    BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_DEBUG, "Added member type SAI_OBJECT_TYPE_PORT val %d "
                      "to vlan 1", p);

    /* Add CPU port as well */
    BCM_PBMP_CLEAR(pbmp);
    BCM_PBMP_PORT_ADD(pbmp, 0);
    rv = bcm_vlan_port_add(unit, 1, pbmp, pbmp);
    if (BCM_FAILURE(rv)) {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Failed to add CPU port to default VLAN. Error %s\n",
                          bcm_errmsg(rv));
        return BRCM_RV_BCM_TO_SAI(rv);
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_vlan_obj_members_get(int vid, int *count, _brcm_sai_vlan_membr_list_t **list)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_vlan_membr_info_t vlan_members;

    DATA_CLEAR(vlan_members, _brcm_sai_vlan_membr_info_t);
    vlan_members.vid = vid;
    tdata.vlan_membrs = &vlan_members;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "error in vlan member table lookup.\n");
        return rv;
    }
    *count = vlan_members.membrs_count;
    if (0 == vlan_members.membrs_count)
    {
        return SAI_STATUS_SUCCESS;
    }
    *list = vlan_members.membrs;

    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_vlan_api_t vlan_apis = {
    brcm_sai_create_vlan,
    brcm_sai_remove_vlan,
    brcm_sai_set_vlan_attribute,
    brcm_sai_get_vlan_attribute,
    brcm_sai_create_vlan_member,
    brcm_sai_remove_vlan_member,
    brcm_sai_set_vlan_member_attribute,
    brcm_sai_get_vlan_member_attribute,
    NULL,
    NULL,
    brcm_sai_get_vlan_stats,
    NULL, /* get_vlan_stats_ext */
    brcm_sai_clear_vlan_stats
};

