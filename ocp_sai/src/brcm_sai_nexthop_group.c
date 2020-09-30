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
#                           Next hop group functions                           #
################################################################################
*/

/*
* Routine Description:
*    Create next hop group
*
* Arguments:
*    [out] next_hop_group_id - next hop group id
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_next_hop_group(_Out_ sai_object_id_t* next_hop_group_id,
                               _In_ sai_object_id_t switch_id,
                               _In_ uint32_t attr_count,
                               _In_ const sai_attribute_t *attr_list)
{
    int i;
    bcm_if_t if_t;
    sai_status_t rv;
    _brcm_sai_table_data_t data;
    bcm_l3_egress_ecmp_t ecmp_object;
    _brcm_sai_ecmp_table_t ecmp_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(next_hop_group_id);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_NEXT_HOP_GROUP_ATTR_TYPE:
                if (SAI_NEXT_HOP_GROUP_TYPE_ECMP != attr_list[i].value.u32)
                {
                    return SAI_STATUS_INVALID_ATTR_VALUE_0+i;
                }
                break;
            default:
                BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_INFO,
                                 "Unknown nexthop group attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    bcm_l3_egress_ecmp_t_init(&ecmp_object);

    rv = _brcm_sai_drop_if_get(&if_t);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, 
                    "getting system drop intf global data", rv);
    rv = bcm_l3_egress_ecmp_create(0, &ecmp_object, 1, &if_t);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp nh group create", rv);

    /* Add NHG to table */
    DATA_CLEAR(ecmp_table, _brcm_sai_ecmp_table_t);
    ecmp_table.intf = ecmp_object.ecmp_intf;
    data.ecmp_table = &ecmp_table;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ECMP, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp DB table entry add.", rv);
    *next_hop_group_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_NEXT_HOP_GROUP,
                                             ecmp_object.ecmp_intf);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);
    return rv;
}

/*
* Routine Description:
*    Remove next hop group
*
* Arguments:
*    [in] next_hop_group_id - next hop group id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_next_hop_group(_In_ sai_object_id_t next_hop_group_id)
{
    sai_status_t rv;
    _brcm_sai_table_data_t data;
    bcm_l3_egress_ecmp_t ecmp_object;
    _brcm_sai_ecmp_table_t ecmp_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(next_hop_group_id,
                                  SAI_OBJECT_TYPE_NEXT_HOP_GROUP))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         next_hop_group_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    bcm_l3_egress_ecmp_t_init(&ecmp_object);
    ecmp_object.ecmp_intf = BRCM_SAI_GET_OBJ_VAL(bcm_if_t,
                                                 next_hop_group_id);
    /* Lookup NHG table */
    DATA_CLEAR(ecmp_table, _brcm_sai_ecmp_table_t);
    ecmp_table.intf = ecmp_object.ecmp_intf;
    data.ecmp_table = &ecmp_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ECMP, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp table entry lookup.", rv);
    if (ecmp_table.nh_count)
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                         "Can't remove NHG %d with ref count %d.\n", 
                         ecmp_table.intf, ecmp_table.nh_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    rv = bcm_l3_egress_ecmp_destroy(0, &ecmp_object);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp nh group destroy", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);
    return rv;
}

/*
* Routine Description:
*    Set Next Hop Group attribute
*
* Arguments:
*    [in] next_hop_group_id - next_hop_group_id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_next_hop_group_attribute(_In_ sai_object_id_t next_hop_group_id,
                                      _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);

    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);

    return rv;
}

/*
* Routine Description:
*    Get Next Hop Group attribute
*
* Arguments:
*    [in] next_hop_group_id - next_hop_group_id
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_next_hop_group_attribute(_In_ sai_object_id_t next_hop_group_id,
                                      _In_ uint32_t attr_count,
                                      _Inout_ sai_attribute_t *attr_list)
{
    int i;
    _brcm_sai_table_data_t data;
    _brcm_sai_ecmp_table_t ecmp_table;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(next_hop_group_id, SAI_OBJECT_TYPE_NEXT_HOP_GROUP);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_NEXT_HOP_GROUP_ATTR_TYPE:
                attr_list[i].value.u32 = SAI_NEXT_HOP_GROUP_TYPE_ECMP;
                break;
            case SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_COUNT:
                /* Lookup NHG table */
                DATA_CLEAR(ecmp_table, _brcm_sai_ecmp_table_t);
                ecmp_table.intf = BRCM_SAI_GET_OBJ_VAL(bcm_if_t,
                                                       next_hop_group_id);
                data.ecmp_table = &ecmp_table;
                rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ECMP, &data);
                BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp table entry lookup.", rv);
                attr_list[i].value.u32 = ecmp_table.nh_count;
                break;
            case SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_MEMBER_LIST:
            {
                int limit, c = 0;
                _brcm_sai_nh_list_t *nhs;

                /* Lookup NHG table */
                DATA_CLEAR(ecmp_table, _brcm_sai_ecmp_table_t);
                ecmp_table.intf = BRCM_SAI_GET_OBJ_VAL(bcm_if_t,
                                                       next_hop_group_id);
                data.ecmp_table = &ecmp_table;
                rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ECMP, &data);
                BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp table entry lookup.", rv);
                limit = ecmp_table.nh_count;
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < limit)
                {
                    limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i);
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                if (limit)
                {
                    nhs = ecmp_table.nh_list;
                    while (nhs)
                    {
                        BRCM_SAI_ATTR_LIST_OBJ_LIST(i, c) =
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, 0, 
                                                        nhs->nhid, ecmp_table.intf);
                        nhs = nhs->next;
                        c++;
                        if (c >= limit)
                        {
                            break;
                        }
                    }
                }
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = ecmp_table.nh_count;
                if (!rv)
                {
                    /* a bit of sanity chk */
                    BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_INFO, "NHG members: %d:%d.\n",
                                     ecmp_table.nh_count, c);
                }
                break;
            }
            default:
                BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_INFO,
                                 "Unknown nexthop group attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP,
                        "Error processsing NHG attributes", rv);
    }    

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);
    return rv;
}

/*
* Routine Description:
*    Add next hop to a group
*
* Arguments:
*    [in] next_hop_group_id - next hop group id
*    [in] next_hop_count - number of next hops
*    [in] nexthops - array of next hops
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_next_hop_group_member(_Out_ sai_object_id_t* next_hop_group_member_id,
                                      _In_ sai_object_id_t switch_id,
                                      _In_ uint32_t attr_count,
                                      _In_ const sai_attribute_t *attr_list)
{
    bool new_drop = FALSE;
    int i, idx = -1, count, max;
    _brcm_sai_nh_list_t *nh_node;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t data;
    bcm_l3_egress_ecmp_t ecmp_object;
    _brcm_sai_ecmp_table_t ecmp_table;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_if_t one_intf, drop_if_id, nh, *intfs = NULL, ecmp_intf = -1;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(next_hop_group_member_id);
    
    for (i=0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_NEXT_HOP_GROUP))
                {
                    BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                                     "Invalid object type 0x%16lx passed\n",
                                     BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                ecmp_intf = BRCM_SAI_GET_OBJ_VAL(bcm_if_t,
                                                 BRCM_SAI_ATTR_LIST_OBJ(i));
                break;
            case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_NEXT_HOP))
                {
                    BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                                     "Invalid object type 0x%16lx passed\n",
                                     BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                idx = BRCM_SAI_GET_OBJ_VAL(bcm_if_t,
                                           BRCM_SAI_ATTR_LIST_OBJ(i));
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                                (int *) &idx, &data);
                BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "nh info data get", rv);
                nh = data.nh_info.if_id;
                break;
            case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT:
                break;
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP,
                        "Error processsing NHG member attributes", rv);
    }
    
    if (-1 == idx || -1 == ecmp_intf)
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "No NHG or NH.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    
    rv = _brcm_sai_l3_config_get(1, &max);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "get max ecmp", rv);
    intfs = ALLOC(max * sizeof(bcm_if_t));
    if (IS_NULL(intfs))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "Error with alloc %d\n",
                         max);
        return SAI_STATUS_NO_MEMORY;
    }
    bcm_l3_egress_ecmp_t_init(&ecmp_object);
    ecmp_object.ecmp_intf = ecmp_intf;
    rv = bcm_l3_egress_ecmp_get(0, &ecmp_object, max, (bcm_if_t*)intfs, &count);
    BRCM_SAI_API_CHK_FREE(SAI_API_NEXT_HOP_GROUP, "ecmp nh group get", rv, intfs);
    
    /* Note: There will always be atleast 1 existing member in the SDK ecmp group.
     *       If there is just 1 existing member then:
     *          If the member is not a drop intf and the new intf is also not a
     *              drop intf then simply add the new intf.
     *          If the member is not a drop intf and the new intf is a drop intf
     *              then no SDK ecmp op.
     *          If the member is a drop intf and the new intf is not a drop intf
     *              then remove the drop intf, add the new intf.
     *          If the member is a drop intf and the new intf is also a drop intf
     *              then no SDK ecmp op.
     *       If there are more than 1 existing members then:
     *          If the new intf is not a drop intf then simply add it.
     *          If the new intf is a drop intf then no SDK ecmp op.
     *
     *       Plus always save state and exit.
     */
     
    rv = _brcm_sai_drop_if_get(&drop_if_id);
    BRCM_SAI_RV_CHK_FREE(SAI_API_NEXT_HOP_GROUP, 
                         "getting system drop intf global data", rv, intfs);
    if (drop_if_id == nh) /* NH currently unknown */
    {
        new_drop = TRUE;
    }
    if (1 == count)
    {
        one_intf = intfs[0];
        if (drop_if_id == one_intf && FALSE == new_drop)
        {
            rv = bcm_l3_egress_ecmp_delete(0, &ecmp_object, one_intf);
            BRCM_SAI_API_CHK_FREE(SAI_API_NEXT_HOP_GROUP,
                                  "ecmp nh group del drop", rv, intfs);
        }
    }
    CHECK_FREE(intfs);
    if (FALSE == new_drop)
    {
        rv = _brcm_l3_egress_ecmp_add(&ecmp_object, nh);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp nh group add", rv);
    }

    /* Lookup NHG table */
    DATA_CLEAR(ecmp_table, _brcm_sai_ecmp_table_t);
    ecmp_table.intf = ecmp_object.ecmp_intf;
    tdata.ecmp_table = &ecmp_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ECMP, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp table entry lookup.", rv);
        
    /* Alloc a new NH node and add it to the end of the list and update, save table entry */
    nh_node = ALLOC_CLEAR(1, sizeof(_brcm_sai_nh_list_t));
    if (IS_NULL(nh_node))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                         sizeof(_brcm_sai_nh_list_t));
        return SAI_STATUS_NO_MEMORY;
    }
    nh_node->nhid = idx;
    if (ecmp_table.nh_list)
    {
        ecmp_table.end->next = nh_node;
    }
    else
    {
        ecmp_table.nh_list = nh_node;
    }
    ecmp_table.end = nh_node;
    ecmp_table.nh_count++;
        
    /* Add NHG to NH table ecmp list end */
    rv = _brcm_sai_nh_table_ecmp_list_ecmp_add(idx, ecmp_table.intf);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "nh table ecmp list ecmp add.", rv);

    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ECMP, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp DB table entry add.", rv);

    /* Inrcement count */
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_global_data_bump(_BRCM_SAI_ECMP_NH_COUNT, INC))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                         "Error incrementing nhg member count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    *next_hop_group_member_id = 
        BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER, 0,
                                    idx, ecmp_object.ecmp_intf);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);
    return rv;
}

/*
* Routine Description:
*    Remove next hop from a group
*
* Arguments:
*    [in] next_hop_group_member_id - next hop group id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_next_hop_group_member(_In_ sai_object_id_t next_hop_group_member_id)
{
    sai_status_t rv;
    int idx = -1, count, max;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t data;
    bcm_l3_egress_ecmp_t ecmp_object;
    _brcm_sai_ecmp_table_t ecmp_table;
    bcm_if_t drop_if_id, nh, *intfs = NULL, ecmp_intf = -1;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(next_hop_group_member_id,
                                  SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         next_hop_group_member_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    
    ecmp_intf = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, next_hop_group_member_id);
    idx = BRCM_SAI_GET_OBJ_MAP(next_hop_group_member_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                    (int *) &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "nh info data get", rv);
    nh = data.nh_info.if_id;
    
    rv = _brcm_sai_drop_if_get(&drop_if_id);
    BRCM_SAI_RV_CHK_FREE(SAI_API_NEXT_HOP_GROUP, 
                         "getting system drop intf global data", rv, intfs);
    if (drop_if_id != nh && _BRCM_SAI_NH_UNKOWN_NBR_TYPE != data.nh_info.type_state)
    {
        rv = _brcm_sai_l3_config_get(1, &max);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "get max ecmp", rv);
        intfs = ALLOC(max * sizeof(bcm_if_t));
        if (IS_NULL(intfs))
        {
            BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "Error with alloc %d\n",
                             max);
            return SAI_STATUS_NO_MEMORY;
        }
        bcm_l3_egress_ecmp_t_init(&ecmp_object);
        ecmp_object.ecmp_intf = ecmp_intf;
        rv = bcm_l3_egress_ecmp_get(0, &ecmp_object, max, (bcm_if_t*)intfs, &count);
        BRCM_SAI_API_CHK_FREE(SAI_API_NEXT_HOP_GROUP, "ecmp nh group get", rv, intfs);

        if (1 == count && drop_if_id != intfs[0])
        {
            /* Add drop intf */
            rv = _brcm_l3_egress_ecmp_add(&ecmp_object, drop_if_id);
            BRCM_SAI_RV_CHK_FREE(SAI_API_NEXT_HOP_GROUP,
                                 "ecmp nh group add", rv, intfs);
        }
        if (1 != count || drop_if_id != intfs[0])
        {
            rv = bcm_l3_egress_ecmp_delete(0, &ecmp_object, nh);
            if (rv != BCM_E_NONE && rv != BCM_E_NOT_FOUND)
            {
                BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                                 "Error in ecmp nh group del for nhg: %d nh: %d\n",
                                 ecmp_intf, nh);
                return rv;
            }
            else if (BCM_E_NOT_FOUND == rv)
            {
                BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_DEBUG,
                                 "nh %d not currently active in nhg: %d\n",
                                 nh, ecmp_intf);
                rv = BCM_E_NONE;
            }
        }
        CHECK_FREE(intfs);
    }
    /* Lookup NHG table and delete NH from list. Update counts. Save state */
    DATA_CLEAR(ecmp_table, _brcm_sai_ecmp_table_t);
    ecmp_table.intf = ecmp_intf;
    tdata.ecmp_table = &ecmp_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ECMP, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp table entry lookup.", rv);
    if (0 == ecmp_table.nh_count)
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "NHG list empty.\n");
        return SAI_STATUS_FAILURE;
    }
    if (1 == ecmp_table.nh_count)
    {
        if (idx != ecmp_table.end->nhid)
        {
            BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                             "One and only NH in the NHG list, did not match.\n");
            return SAI_STATUS_FAILURE;
        }
        else
        {
            FREE(ecmp_table.nh_list);
            ecmp_table.end = ecmp_table.nh_list = NULL;
        }
    }
    else
    {
        bool found = FALSE;
        _brcm_sai_nh_list_t *ptr, *prev;
        
        ptr = ecmp_table.nh_list;
        do 
        {
            if (idx == ptr->nhid)
            {
                found = TRUE;
                if (ptr == ecmp_table.nh_list)
                {
                    ecmp_table.nh_list = ptr->next;
                }
                else
                {
                    prev->next = ptr->next;
                }
                if (IS_NULL(ptr->next))
                {
                    ecmp_table.end = prev;
                }
                FREE(ptr);
                ptr = NULL;
                break;
            }
            prev = ptr;
            ptr = ptr->next;
        } while (ptr);
        if (FALSE == found)
        {
            BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "NH not found in the NHG list.\n");
            return SAI_STATUS_FAILURE;
        }
    }
    ecmp_table.nh_count--;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ECMP, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp DB table entry add.", rv);
    
    /* Lookup NH table and delete NHG from the list. Update counts. Save state */
    rv = _brcm_sai_nh_table_ecmp_list_ecmp_del(idx, ecmp_intf);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "nh table ecmp list ecmp del.", rv);

    /* Decrement count */
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_global_data_bump(_BRCM_SAI_ECMP_NH_COUNT, DEC))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                         "Error decrementing nhg nh count global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);

    return rv;
}

/**
 * @brief Get Next Hop Group attribute
 *
 * @param[in] next_hop_group_member_id Next hop group member ID
 * @param[in] attr_count Number of attributes
 * @param[inout] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_next_hop_group_member_attribute(_In_ sai_object_id_t next_hop_group_member_id,
                                             _In_ uint32_t attr_count,
                                             _Inout_ sai_attribute_t *attr_list)
{
    int i, gid, nhid;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(next_hop_group_member_id,
                                      SAI_OBJECT_TYPE_NEXT_HOP_GROUP_MEMBER);
    for (i=0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID:
                gid = BRCM_SAI_GET_OBJ_VAL(int, next_hop_group_member_id);
                BRCM_SAI_ATTR_LIST_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, gid);
                break;
            case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID:
                nhid = BRCM_SAI_GET_OBJ_MAP(next_hop_group_member_id);
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_NEXT_HOP, nhid);
                break;
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP,
                        "Error processsing NHG member attributes", rv);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);

    return rv;
}

/*
################################################################################
#                               Internal functions                             #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_nhg_info()
{
    int max;
    sai_status_t rv;
    _brcm_sai_data_t gdata;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_ECMP_NH_COUNT, &gdata))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                         "Error getting nhg nh count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    rv = bcm_l3_route_max_ecmp_get(0, &max);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP_GROUP, "route max ecmp get", rv);
    max = _brcm_sai_get_max_ecmp_members()/max;
    
    rv = _brcm_sai_db_table_create(_BRCM_SAI_TABLE_ECMP, max);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "creating ecmp nhg table.", rv);
    /* Now traverse the NHG table and create NH lists */
    rv = _brcm_sai_db_table_node_list_init(_BRCM_SAI_TABLE_ECMP,
                                           _BRCM_SAI_LIST_ECMP_NH);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "creating nhg table node nh list.", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);
    return rv;
}

sai_status_t
_brcm_sai_free_nhg_info()
{
    sai_status_t rv;
    _brcm_sai_data_t gdata;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_ECMP_NH_COUNT, &gdata))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR,
                         "Error getting nhg nh count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    /* Traverse the NHG table and free NH lists */
    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_ECMP,
                                           _BRCM_SAI_LIST_ECMP_NH);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "freeing nhg table node nh list.", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);
    return rv;
}

sai_status_t
_brcm_sai_nhg_nh_update(bool add, int nhid, bcm_if_t if_id)
{
    int max, count;
    sai_status_t rv;
    _brcm_sai_nh_ecmp_t *ecmp;
    bcm_if_t drop_if_id, *intfs;
    bcm_l3_egress_ecmp_t ecmp_object;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP_GROUP);

    /* Lock the shared data */
    _brcm_sai_nh_table_ecmp_lock(nhid);
    
    /* Get count and list of NHGs for this NH to update */
    rv = _brcm_sai_nh_table_ecmp_get(nhid, &count, &ecmp);
    if (SAI_STATUS_ERROR(rv))
    {
        _brcm_sai_nh_table_ecmp_unlock(nhid);
    }
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "nh table ecmp info get.", rv);
    if (0 == count)
    {
        goto _nhg_update_end;
    }
    /* cache out reusable data */
    rv = _brcm_sai_drop_if_get(&drop_if_id);
    if (SAI_STATUS_ERROR(rv))
    {
        _brcm_sai_nh_table_ecmp_unlock(nhid);
    }
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, 
                    "getting system drop intf global data", rv);
    rv = _brcm_sai_l3_config_get(1, &max);
    if (SAI_STATUS_ERROR(rv))
    {
        _brcm_sai_nh_table_ecmp_unlock(nhid);
    }
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP_GROUP, "get max ecmp", rv);
    intfs = ALLOC(max * sizeof(bcm_if_t));
    if (IS_NULL(intfs))
    {
        BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "Error with alloc %d\n",
                         max);
        _brcm_sai_nh_table_ecmp_unlock(nhid);
        return SAI_STATUS_NO_MEMORY;
    }
    
    /* Traverse and update all NHGs */
    do {
        /* check count */
        bcm_l3_egress_ecmp_t_init(&ecmp_object);
        ecmp_object.ecmp_intf = ecmp->intf;
        rv = _brcm_l3_egress_ecmp_get(&ecmp_object, max, intfs, 
                                      &count);
        if (SAI_STATUS_ERROR(rv))
        {
            _brcm_sai_nh_table_ecmp_unlock(nhid);
        }
        BRCM_SAI_RV_CHK_FREE(SAI_API_NEXT_HOP_GROUP, "l3 egress ecmp get", rv,
                             intfs);
        if (add)
        {
            /* if add op: if count is 1 and if its drop, then remove drop. +add new */
            if ((1 == count) && (drop_if_id == intfs[0]))
            {
                rv = bcm_l3_egress_ecmp_delete(0, &ecmp_object, drop_if_id);
                if (BCM_FAILURE(rv))
                {
                    _brcm_sai_nh_table_ecmp_unlock(nhid);
                }
                BRCM_SAI_API_CHK_FREE(SAI_API_NEXT_HOP_GROUP,
                                      "l3 egress ecmp del", rv, intfs);
            }
            rv = _brcm_l3_egress_ecmp_add(&ecmp_object, if_id);
            if (SAI_STATUS_ERROR(rv))
            {
                _brcm_sai_nh_table_ecmp_unlock(nhid);
            }
            BRCM_SAI_RV_CHK_FREE(SAI_API_NEXT_HOP_GROUP,
                                 "l3 egress ecmp add", rv, intfs);
        }
        else
        {
            /* if del op: if count is 1, add drop. +del old */
            if (1 == count)
            {
                rv = bcm_l3_egress_ecmp_add(0, &ecmp_object, drop_if_id);
                if (BCM_FAILURE(rv))
                {
                    _brcm_sai_nh_table_ecmp_unlock(nhid);
                }
                BRCM_SAI_API_CHK_FREE(SAI_API_NEXT_HOP_GROUP, 
                                      "l3 egress ecmp add", rv, intfs);
            }
            rv = bcm_l3_egress_ecmp_delete(0, &ecmp_object, if_id);
            if (BCM_FAILURE(rv))
            {
                _brcm_sai_nh_table_ecmp_unlock(nhid);
            }
            BRCM_SAI_API_CHK_FREE(SAI_API_NEXT_HOP_GROUP,
                                  "l3 egress ecmp del", rv, intfs);
        }
        ecmp = ecmp->next;
    } while (ecmp);
    
_nhg_update_end:
    /* Un-lock the shared data */
    _brcm_sai_nh_table_ecmp_unlock(nhid);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP_GROUP);
    return SAI_STATUS_SUCCESS;
}

/*
*  Next Hop methods table retrieved with sai_api_query()
*/
const sai_next_hop_group_api_t next_hop_grp_apis = {
    brcm_sai_create_next_hop_group,
    brcm_sai_remove_next_hop_group,
    brcm_sai_set_next_hop_group_attribute,
    brcm_sai_get_next_hop_group_attribute,
    brcm_sai_create_next_hop_group_member,
    brcm_sai_remove_next_hop_group_member,
    NULL,
    brcm_sai_get_next_hop_group_member_attribute,
    NULL,
    NULL
};
