/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

#define _BRCM_SAI_ACL_MAX_STATS 2

/*
* precompiler flag used to disable adding bcmFieldQualifyDstTrunk to QSETs;
* this is a temporary fixup, a permanent solution needs to be developed
*/
#define SAI_FIXUP_DISABLE_DEST_TRUNK_QUALIFIER 1


#define ACL_DEBUG(__fmt, __args...)             \
  COMP_DBG_LEVEL(ACL, DEBUG, __fmt,##__args)

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t 
_brcm_sai_acl_table_entry_attach(int tid, int eid, int uid, bool state,
                                 bool clone, int bind_point_type);
STATIC sai_status_t
_brcm_sai_acl_table_entry_detach(int tid, int eid);
STATIC sai_status_t
_brcm_sai_field_entry_copy(int unit, int table_id, bcm_field_entry_t u_entry,
                           int bind_point_type, int val, bool ingress,
                           bool state);
STATIC sai_status_t
_brcm_sai_field_qualify_ing_lag(bcm_field_entry_t entry,
                                int lag);
STATIC sai_status_t
_brcm_sai_acl_table_group_member_attach(int gid, sai_object_id_t val);

sai_status_t
_brcm_sai_acl_table_group_member_detach(int gid, int val);

STATIC sai_status_t
_brcm_sai_acl_table_bind(sai_object_id_t acl_obj, bool direction,
                         sai_object_id_t bind_obj, int type, bool bp_check);
sai_status_t
_brcm_sai_field_qualify_ing_lag_remove(_brcm_sai_acl_table_t *acl_table,
                                       bcm_pbmp_t *unbind_pbmp,
                                       int lag);

/*
################################################################################
#                                  ACL functions                               #
################################################################################
*/

/*
* Routine Description:
*   Create an ACL table
*
* Arguments:
*   [out] acl_table_id - the the acl table id
*   [in] attr_count - number of attributes
*   [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_acl_table(_Out_ sai_object_id_t* acl_table_id,
                          _In_ sai_object_id_t switch_id,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list)
{
    int32_t *blist;
    sai_status_t rv;
    int b, tid, stage, bcount = 0;
    _brcm_sai_indexed_data_t data;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    
    rv = _brcm_sai_create_acl_table(acl_table_id, attr_count,
                                    attr_list, &stage, &bcount, &blist);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "create table entry", rv);
    
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_ACL_TABLES_COUNT, INC))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Error incrementing acl tables count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    
    tid = BRCM_SAI_GET_OBJ_VAL(int, *acl_table_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    data.acl_table.valid = TRUE;
    data.acl_table.idx = tid;
    data.acl_table.stage = stage;
    if (bcount)
    {
        data.acl_table.bind_types_count = bcount;
        for (b=0; b<bcount; b++)
        {
            data.acl_table.bind_types[b] = blist[b];
        }
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL table created: %d\n", tid);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Delete an ACL table
*
* Arguments:
*   [in] acl_table_id - the acl table id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_delete_acl_table(_In_ sai_object_id_t acl_table_id)
{
    sai_status_t rv;
    bcm_field_group_t group;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_table_id, SAI_OBJECT_TYPE_ACL_TABLE))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    group = BRCM_SAI_GET_OBJ_VAL(bcm_field_group_t, acl_table_id);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &group, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (data.acl_table.ref_count)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Non zero ref count (%d) for acl table.\n",
                         data.acl_table.ref_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    if (data.acl_table.group_count)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Table is still member of %d groups.\n",
                         data.acl_table.group_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
#if 0
    if (data.acl_table.bind_count)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Table is still bound %d times.\n",
                         data.acl_table.bind_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
#endif
    rv = bcm_field_group_destroy(0, group);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field group destroy", rv);
    /*_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_ACL_TABLE, group);*/
    data.acl_table.valid = FALSE;
    /* FIXME: this should be done in an un-bind call. Remove the following
     * once unbind is available
     */
    if (data.acl_table.bind_count)
    {
        _brcm_sai_acl_bind_point_t *next, *bpts = data.acl_table.bind_pts;
        while (bpts)
        {
            if (bpts->type ==  SAI_ACL_BIND_POINT_TYPE_LAG)
            {
                /* for lags we maintain a list of acl binds, we need
                   to delete it */
                rv = _brcm_sai_lag_unbind_acl_obj(bpts->val,
                                                  acl_table_id);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "acl unbind", rv);
            }
            next = bpts->next;
            FREE(bpts);
            bpts = next;
        }
        data.acl_table.bind_pts = NULL;
        data.acl_table.bind_count = 0;
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &group, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
 
    
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_ACL_TABLES_COUNT, DEC))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Error decrementing acl tables count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL table removed: %d\n", group);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Set ACL table attribute
*
* Arguments:
*    [in] acl_table_id - the acl table id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_acl_table_attribute(_In_ sai_object_id_t acl_table_id,
                                 _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Get ACL table attribute
*
* Arguments:
*    [in] acl_table_id - acl table id
*    [in] attr_count - number of attributes
*    [Out] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_acl_table_attribute(_In_ sai_object_id_t acl_table_id,
                                 _In_ uint32_t attr_count,
                                 _Out_ sai_attribute_t *attr_list)
{
    int i, stage = -1;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_field_qset_t qset;
    bcm_field_group_t group;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;

    group = BRCM_SAI_GET_OBJ_VAL(bcm_field_group_t, acl_table_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Get acl table: %d\n", group);
    rv = bcm_field_group_get(0, group, &qset);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field group create", rv);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_TABLE_ATTR_ACL_STAGE:
                if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyStageIngress))
                {
                    stage = attr_list[i].value.u32 = SAI_ACL_STAGE_INGRESS;
                }
                else if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyStageEgress))
                {
                    stage = attr_list[i].value.u32 = SAI_ACL_STAGE_EGRESS;
                }
                else
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Could not get table stage.\n");
                    return SAI_STATUS_FAILURE;
                }
                break;
            default: break;
        }
        if (-1 != stage)
        {
            break;
        }
    }
        
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_TABLE_ATTR_ACL_STAGE:
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_SRC_IPV6:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifySrcIp6) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDstIp6) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_SRC_MAC:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifySrcMac) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DST_MAC:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDstMac) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_SRC_IP:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifySrcIp) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DST_IP:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDstIp) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_IN_PORTS:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUT_PORTS:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "OUT PORTS not supported on this platform.\n");
                return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            case SAI_ACL_TABLE_ATTR_FIELD_IN_PORT:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPort) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUT_PORT:
                if (-1 == stage)
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Could not get stage info.\n");
                    return SAI_STATUS_FAILURE;
                }
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, SAI_ACL_STAGE_INGRESS == stage ?  
                                       bcmFieldQualifyDstPort :
                                       bcmFieldQualifyOutPort) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_ID:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOuterVlanId) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_PRI:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOuterVlanPri) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_CFI:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOuterVlanCfi) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_INNER_VLAN_ID:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInnerVlanId) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_INNER_VLAN_PRI:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInnerVlanPri) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_INNER_VLAN_CFI:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInnerVlanCfi) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_L4_SRC_PORT:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyL4SrcPort) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_L4_DST_PORT:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyL4DstPort) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ETHER_TYPE:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyEtherType) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_IP_PROTOCOL:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyIpProtocol) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DSCP:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDSCP) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ECN:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDSCP) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TTL:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyTtl) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TOS:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyTos) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_IP_FLAGS:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyIpFlags) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TCP_FLAGS:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyTcpControl) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyIpType) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_FRAG:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyIpFlags) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TC:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyIntPriority) ? 
                    TRUE : FALSE;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ACL_RANGE_TYPE:
                attr_list[i].value.booldata = 
                    BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyRangeCheck) ? 
                    TRUE : FALSE;
                break; 
            case SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_ENTRY:
            {
                bcm_field_group_status_t group_status;

                rv = bcm_field_group_status_get(0, group, &group_status);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "field group status get", rv);
                attr_list[i].value.u32 = group_status.entries_free;
                break;
            }
            case SAI_ACL_TABLE_ATTR_AVAILABLE_ACL_COUNTER:
            {
                bcm_field_group_status_t group_status;

                rv = bcm_field_group_status_get(0, group, &group_status);
                BRCM_SAI_API_CHK(SAI_API_SWITCH, "field group status get", rv);
                attr_list[i].value.u32 = group_status.counters_free;
                break;
            }
            default: 
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown acl table attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_INVALID_ATTRIBUTE_0;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Create an ACL entry
*
* Arguments:
*   [out] acl_entry_id - the acl entry id
*   [in] attr_count - number of attributes
*   [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_acl_entry(_Out_ sai_object_id_t *acl_entry_id,
                          _In_ sai_object_id_t switch_id,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list)
{
    int i, tid = 0;
    sai_status_t rv;
    bcm_field_qset_t qset;
    bcm_field_entry_t entry;
    int port = 0, lag = 0, vlan = 0;
    sai_packet_action_t gpa=0, ypa=0, rpa=0;
    bool state = TRUE, ingress = FALSE, pol = FALSE, pol_act = FALSE;
    _brcm_sai_indexed_data_t data, _data = { .acl_tbl_grp.bind_count = 0 };

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(acl_entry_id);
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_ENTRY_ATTR_TABLE_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_ACL_TABLE))
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "ACL table object not passed.\n");
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                tid = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_field_group_t, i);
                rv = bcm_field_group_get(0, tid, &qset);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field group get", rv);
                if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyStageIngress))
                {
                    ingress = TRUE;
                }
                rv = bcm_field_entry_create(0,
                         BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_field_group_t, i),
                         &entry);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry create", rv);
                break;
            default: break;
        }
        if (tid)
        {
            break;
        }
    }
    if (!tid)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "No table id found\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_table.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }

    *acl_entry_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_ACL_ENTRY, 0, tid, entry);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_POLICER:
            {
                rv = _brcm_sai_policer_action_ref_attach(BRCM_SAI_ATTR_ACL_ACTION_OBJ(i),
                                                         *acl_entry_id);
                if (rv > 0)
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Skip policer action set.\n");
                }
                else if (0 != rv)
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                     "Error encountered in Policer action get.\n");
                    return rv;
                }
                else
                {
                    pol_act = TRUE;
                }
                pol = TRUE;
            }
            default: break;
        }
        if (pol && pol_act)
        {
            rv = _brcm_sai_policer_actions_get(BRCM_SAI_ATTR_ACL_ACTION_OBJ(i),
                                               &gpa, &ypa, &rpa);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "policer actions get", rv);
            break;
        }
    }

    rv = _brcm_sai_create_acl_entry(acl_entry_id, attr_count, attr_list, entry,
                                    ingress, gpa, ypa, rpa, &state);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "create acl entry", rv);
    
    rv = _brcm_sai_acl_table_entry_attach(tid, entry, 0, state, FALSE, 0);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "table entry attach", rv);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL entry created: %d\n", entry);
    
    /* Check if table is part of any group and if so then check and traverse it
     * for bind points and add qualifiers 
     */
    if (data.acl_table.group_count)
    {
        int _group_count = 0;

        for (i=0; i<_BRCM_SAI_MAX_ACL_TABLE_GROUPS; i++)
        {
            if (0 == data.acl_table.group[i])
            {
                continue;
            }
            _group_count++;
            
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                            &data.acl_table.group[i], &_data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
            if (_data.acl_tbl_grp.bind_count && _data.acl_tbl_grp.bind_pts)
            {
                bool switch_bp = FALSE, port_bp = FALSE;
                _brcm_sai_acl_bind_point_t *bp = _data.acl_tbl_grp.bind_pts;
            
                while (bp)
                {
                    switch (bp->type)
                    {
                        case SAI_ACL_BIND_POINT_TYPE_PORT:
                            if (ingress)
                            {
                                bcm_pbmp_t pbmp, mpbmp;
                                rv = bcm_field_qualify_InPorts_get(0, entry, &pbmp, &mpbmp);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts get", rv);
                                BCM_PBMP_PORT_ADD(pbmp, bp->val);
                                _brcm_sai_switch_pbmp_fp_all_get(&mpbmp);
                                rv = bcm_field_qualify_InPorts(0, entry, pbmp, mpbmp);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                            }
                            else
                            {
                                if (!port)
                                {
                                    rv = bcm_field_qualify_OutPort(0, entry, bp->val,
                                                                   _BRCM_SAI_MASK_32);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
                                }
                                else
                                {
                                    rv = _brcm_sai_field_entry_copy(0, tid, entry, bp->type,
                                                                    bp->val, ingress, state);
                                    BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                                }
                                port++;
                            }
                            port_bp = TRUE;
                            break;
                        case SAI_ACL_BIND_POINT_TYPE_SWITCH:
                            switch_bp = TRUE;
                            break;
                        case SAI_ACL_BIND_POINT_TYPE_LAG:
                            if (ingress)
                            {
                                rv = _brcm_sai_field_qualify_ing_lag(entry,
                                                                     bp->val);
                                BRCM_SAI_RV_CHK(SAI_API_ACL, "field ing src trunk qual", rv);
                            }
                            else
                            {
                                if (!lag)
                                {
                                    rv = bcm_field_qualify_DstTrunk(0, entry, bp->val, 0xffff);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify dst trunk", rv);
                                }
                                else
                                {
                                    rv = _brcm_sai_field_entry_copy(0, tid, entry, bp->type,
                                                                    bp->val, ingress, state);
                                    BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                                }
                            }
                            lag++;
                            break;
                        case SAI_ACL_BIND_POINT_TYPE_VLAN:
                            if (!vlan)
                            {
                                rv = bcm_field_qualify_OuterVlanId(0, entry, bp->val,
                                                                   _BRCM_SAI_MASK_16);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
                            }
                            else
                            {
                                rv = _brcm_sai_field_entry_copy(0, tid, entry, bp->type,
                                                                bp->val, ingress, state);
                                BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                            }
                            vlan++;
                            break;
                        case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF:
                            return SAI_STATUS_NOT_IMPLEMENTED;
                        default: 
                            return SAI_STATUS_INVALID_OBJECT_TYPE;
                    }
                    bp = bp->next;
                }
                if (ingress && port_bp && switch_bp)
                {
                    bcm_pbmp_t pbmp;

                    _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
                    rv = bcm_field_qualify_InPorts(0, entry, pbmp, pbmp);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                }
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                                 "Group (%d) binding completed for table(%d) : "
                                 "port count %d, lag count %d, vlan count %d\n",
                                 data.acl_table.group[i], tid, port, lag, vlan);
                port = 0, lag = 0; vlan = 0;
            }
            if (_group_count == data.acl_table.group_count)
            {
                break;
            }
        }
    }
    
    /* Then check and traverse the tables bind points, and add qualifiers */
    if (data.acl_table.bind_count && data.acl_table.bind_pts)
    {
        bool switch_bp = FALSE, port_bp = FALSE;
        _brcm_sai_acl_bind_point_t *bp = data.acl_table.bind_pts;
        
        while (bp)
        {
            switch (bp->type)
            {
                case SAI_ACL_BIND_POINT_TYPE_PORT:
                    if (ingress)
                    {
                        bcm_pbmp_t pbmp, mpbmp;
                        rv = bcm_field_qualify_InPorts_get(0, entry, &pbmp, &mpbmp);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts get", rv);
                        BCM_PBMP_PORT_ADD(pbmp, bp->val);
                        _brcm_sai_switch_pbmp_fp_all_get(&mpbmp);
                        rv = bcm_field_qualify_InPorts(0, entry, pbmp, mpbmp);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                    }
                    else
                    {
                        if (!port)
                        {
                            rv = bcm_field_qualify_OutPort(0, entry, bp->val,
                                                           _BRCM_SAI_MASK_32);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
                        }
                        else
                        {
                            rv = _brcm_sai_field_entry_copy(0, tid, entry, bp->type,
                                                            bp->val, ingress, state);
                            BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                        }
                        port++;
                    }
                    port_bp = TRUE;
                    break;
                case SAI_ACL_BIND_POINT_TYPE_SWITCH:
                    switch_bp = TRUE;
                    break;
                case SAI_ACL_BIND_POINT_TYPE_LAG:
                    if (ingress)
                    {
                        rv = _brcm_sai_field_qualify_ing_lag(entry,
                                                             bp->val);
                        BRCM_SAI_RV_CHK(SAI_API_ACL, "field ing src trunk qual", rv);
                    }
                    else
                    {
                        if (!lag)
                        {
                            rv = bcm_field_qualify_DstTrunk(0, entry, bp->val, 0xffff);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify dst trunk", rv);
                        }
                        else
                        {
                            rv = _brcm_sai_field_entry_copy(0, tid, entry, bp->type,
                                                            bp->val, ingress, state);
                            BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                        }
                    }
                    lag++;
                    break;
                case SAI_ACL_BIND_POINT_TYPE_VLAN:
                    if (!vlan)
                    {
                        rv = bcm_field_qualify_OuterVlanId(0, entry, bp->val,
                                                           _BRCM_SAI_MASK_16);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
                    }
                    else
                    {
                        rv = _brcm_sai_field_entry_copy(0, tid, entry, bp->type,
                                                        bp->val, ingress, state);
                        BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                    }
                    vlan++;
                    break;
                case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF:
                    return SAI_STATUS_NOT_IMPLEMENTED;
                default: 
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            bp = bp->next;
        }
        if (ingress && port_bp && switch_bp)
        {
            bcm_pbmp_t pbmp;

            _brcm_sai_switch_pbmp_fp_all_get(&pbmp);
            rv = bcm_field_qualify_InPorts(0, entry, pbmp, pbmp);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
        }
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                         "Table binding completed: lag count %d, vlan count %d\n",
                         lag, vlan);
    }    
    
    rv = bcm_field_entry_install(0, entry);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry install", rv);
    rv = bcm_field_entry_enable_set(0, entry, state ? TRUE : FALSE);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry enable set", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Delete an ACL entry
*
* Arguments:
 *  [in] acl_entry_id - the acl entry id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_delete_acl_entry(_In_ sai_object_id_t acl_entry_id)
{
    int tid, idx;
    sai_status_t rv;
    bool found = FALSE;
    bcm_l3_egress_t egr;
    bcm_l3_intf_t l3intf;
    uint32 param0 = 0, param1;
    bcm_field_entry_t entry;
    _brcm_sai_acl_entry_t *l_entry;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_entry_id, SAI_OBJECT_TYPE_ACL_ENTRY))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    entry = BRCM_SAI_GET_OBJ_VAL(bcm_field_entry_t, acl_entry_id);
    rv = bcm_field_action_get(0, entry, bcmFieldActionChangeL2Fields, 
                              &param0, &param1);
    if (BCM_FAILURE(rv) && (BCM_E_NOT_FOUND != rv))
    {
        BRCM_SAI_API_CHK(SAI_API_ACL,"field action get for SrcMacNew", rv);
    }
    if (param0)
    {
        rv =  bcm_l3_egress_get(0, param0, &egr);
        BRCM_SAI_API_CHK(SAI_API_ACL,"L3 egress get for SrcMacNew", rv);
        rv = bcm_l3_egress_destroy(0, param0);
        BRCM_SAI_API_CHK(SAI_API_ACL,"L3 egress del for SrcMacNew", rv);
        bcm_l3_intf_t_init(&l3intf);
        l3intf.l3a_intf_id = egr.intf;
        rv = bcm_l3_intf_get(0, &l3intf);
        BRCM_SAI_API_CHK(SAI_API_ACL,"L3 intf get for SrcMacNew", rv);
        rv = bcm_l3_intf_delete(0, &l3intf);
        BRCM_SAI_API_CHK(SAI_API_ACL,"L3 intf del for SrcMacNew", rv);
    }
    rv = bcm_field_action_get(0, entry, bcmFieldActionMirrorIngress,
                              &param0, &param1);
    if (BCM_FAILURE(rv) && (BCM_E_NOT_FOUND != rv))
    {
        BRCM_SAI_API_CHK(SAI_API_ACL,"field action get for bcmFieldActionMirrorIngress", rv);
    }
    if (param1 && (BCM_E_NOT_FOUND != rv))
    {
        idx = param1 & 0xff;
        rv = _brcm_sai_mirror_ref_update(0, idx, FALSE);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "mirror session ref sub", rv);
    }
    rv = bcm_field_action_get(0, entry, bcmFieldActionMirrorEgress,
                              &param0, &param1);
    if (BCM_FAILURE(rv) && (BCM_E_NOT_FOUND != rv))
    {
        BRCM_SAI_API_CHK(SAI_API_ACL,"field action get for bcmFieldActionMirrorEgress", rv);
    }
    if (param1 && (BCM_E_NOT_FOUND != rv))
    {
        idx = param1 & 0xff;
        rv = _brcm_sai_mirror_ref_update(0, idx, FALSE);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "mirror session ref sub", rv);
    }
    rv = bcm_field_entry_destroy(0, entry);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry destroy", rv);
    _brcm_sai_policer_action_ref_detach(acl_entry_id);
    tid = BRCM_SAI_GET_OBJ_MAP(acl_entry_id);
    rv = _brcm_sai_acl_table_entry_detach(tid, entry);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "table entry detach", rv);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Deleted user entry: %d\n", entry);
    /* Now remove any local entries for this user entry */
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_table.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (data.acl_table.ref_count && data.acl_table.entries)
    {
        l_entry = data.acl_table.entries;
        do 
        {
            while (l_entry)
            {
                if (l_entry->uid == entry)
                {
                    found = TRUE;
                    break;
                }
                l_entry = l_entry->next;
            }
            if (found)
            {
                rv = bcm_field_action_get(0, entry, bcmFieldActionChangeL2Fields, 
                                          &param0, &param1);
                if (BCM_FAILURE(rv) && (BCM_E_NOT_FOUND != rv))
                {
                    BRCM_SAI_API_CHK(SAI_API_ACL,"field action get for SrcMacNew", rv);
                }
                if (param0)
                {
                    rv = bcm_l3_egress_get(0, param0, &egr);
                    BRCM_SAI_API_CHK(SAI_API_ACL,"L3 egress get for SrcMacNew", rv);
                    rv = bcm_l3_egress_destroy(0, param0);
                    BRCM_SAI_API_CHK(SAI_API_ACL,"L3 egress del for SrcMacNew", rv);
                    bcm_l3_intf_t_init(&l3intf);
                    l3intf.l3a_intf_id = egr.intf;
                    rv = bcm_l3_intf_get(0, &l3intf);
                    BRCM_SAI_API_CHK(SAI_API_ACL,"L3 intf get for SrcMacNew", rv);
                    rv = bcm_l3_intf_delete(0, &l3intf);
                    BRCM_SAI_API_CHK(SAI_API_ACL,"L3 intf del for SrcMacNew", rv);
                }
                rv = bcm_field_entry_destroy(0, l_entry->id);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry destroy", rv);
                rv = _brcm_sai_acl_table_entry_detach(tid, l_entry->id); 
                BRCM_SAI_RV_CHK(SAI_API_ACL, "table entry detach", rv);
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Deleted local entry: %d\n",
                                 l_entry->id);
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                                &tid, &data);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
                l_entry = data.acl_table.entries;
                found = FALSE;
            }
        } while (l_entry);
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL entry removed: %d\n", entry);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Set ACL entry attribute
*
* Arguments:
*    [in] acl_entry_id - the acl entry id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_acl_entry_attribute(_In_ sai_object_id_t acl_entry_id,
                                 _In_ const sai_attribute_t *attr)
{
    int eid, tid;
    sai_status_t rv;
    bool found = FALSE;
    _brcm_sai_acl_entry_t *entry;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_SET_OBJ_ATTRIB_PARAM_CHK(acl_entry_id, SAI_OBJECT_TYPE_ACL_ENTRY);

    eid = BRCM_SAI_GET_OBJ_VAL(bcm_field_entry_t, acl_entry_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Set acl entry: %d\n", eid);
    switch(attr->id)
    {
        case SAI_ACL_ENTRY_ATTR_ADMIN_STATE:
            tid = BRCM_SAI_GET_OBJ_MAP(acl_entry_id);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &tid, &data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
            if (FALSE == data.acl_table.valid)
            {
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            if (data.acl_table.ref_count && data.acl_table.entries)
            {
                entry = data.acl_table.entries;
                while (entry)
                {
                    if (eid == entry->id)
                    {
                        entry->state = attr->value.booldata;
                        found = TRUE;
                        rv = bcm_field_entry_enable_set(0, eid, attr->value.booldata);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "acl entry state set", rv);
                    }
                    else if (eid == entry->uid)
                    {
                        entry->state = attr->value.booldata;
                        rv = bcm_field_entry_enable_set(0, entry->id, attr->value.booldata);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "acl entry state set", rv);
                    }
                    entry = entry->next;
                }
            }
            if (!found)
            {
                rv = SAI_STATUS_ITEM_NOT_FOUND;
            }
            break;
        case SAI_ACL_ENTRY_ATTR_ACTION_COUNTER:
            tid = BRCM_SAI_GET_OBJ_MAP(acl_entry_id);

            rv = bcm_field_entry_stat_attach(0, eid,
                     BRCM_SAI_GET_OBJ_VAL(int, attr->value.aclaction.parameter.oid));
            BRCM_SAI_API_CHK(SAI_API_ACL, "field entry stat attach", rv);
            rv = bcm_field_entry_reinstall(0, eid);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);

            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &tid, &data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
            if (FALSE == data.acl_table.valid)
            {
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            if (data.acl_table.ref_count && data.acl_table.entries)
            {
                entry = data.acl_table.entries;
                while (entry)
                {
                    if (eid == entry->uid)
                    {
                         rv = bcm_field_entry_stat_attach(0, entry->id,
                                  BRCM_SAI_GET_OBJ_VAL(int, attr->value.aclaction.parameter.oid));
                         BRCM_SAI_API_CHK(SAI_API_ACL, "field entry stat attach", rv);
                         rv = bcm_field_entry_reinstall(0, entry->id);
                         BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                    }
                    entry = entry->next;
                }
            }
            break;        
        default:
            return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
    }
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Get ACL entry attribute
*
* Arguments:
*    [in] acl_entry_id - acl entry id
*    [in] attr_count - number of attributes
*    [Out] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_acl_entry_attribute(_In_ sai_object_id_t acl_entry_id,
                                 _In_ uint32_t attr_count,
                                 _Out_ sai_attribute_t *attr_list)
{
    int i, val;
    uint32_t val32, _val32;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_field_entry_t entry;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;

    entry = BRCM_SAI_GET_OBJ_VAL(bcm_field_entry_t, acl_entry_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Get acl entry attributes for entry: %d\n", entry);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_ENTRY_ATTR_TABLE_ID:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_PRIORITY:
                rv = bcm_field_entry_prio_get(0, entry,
                                                  (int *)&attr_list[i].value.u32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "entry priority get", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ADMIN_STATE:
                rv = bcm_field_entry_enable_get(0, entry, &val);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry enable get", rv);
                attr_list[i].value.booldata = val ? TRUE : FALSE;
                break;
            /* Qualifier values */
            case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IPV6:
            {
                bcm_ip6_t ip6;
                bcm_ip6_t ip6_mask;

                rv = bcm_field_qualify_SrcIp6_get(0, entry, &ip6, &ip6_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify SrcIp6", rv);
                memcpy(BRCM_SAI_ATTR_ACL_FLD_IP6(i), ip6,
                       sizeof(bcm_ip6_t));
                memcpy(BRCM_SAI_ATTR_ACL_FLD_MASK_IP6(i), ip6_mask,
                       sizeof(bcm_ip6_t));
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6:
            {
                bcm_ip6_t ip6;
                bcm_ip6_t ip6_mask;

                rv = bcm_field_qualify_DstIp6_get(0, entry, &ip6, &ip6_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify SrcIp6", rv);
                memcpy(BRCM_SAI_ATTR_ACL_FLD_IP6(i), ip6,
                       sizeof(bcm_ip6_t));
                memcpy(BRCM_SAI_ATTR_ACL_FLD_MASK_IP6(i), ip6_mask,
                       sizeof(bcm_ip6_t));
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_SRC_MAC:
            {
                bcm_mac_t mac;
                bcm_mac_t mac_mask;

                rv = bcm_field_qualify_SrcMac_get(0, entry, &mac, &mac_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify SrcMac", rv);
                memcpy(BRCM_SAI_ATTR_ACL_FLD_MAC(i), mac,
                       sizeof(bcm_mac_t));
                memcpy(BRCM_SAI_ATTR_ACL_FLD_MASK_MAC(i), mac_mask,
                       sizeof(bcm_mac_t));
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_DST_MAC:
            {
                bcm_mac_t mac;
                bcm_mac_t mac_mask;

                rv = bcm_field_qualify_DstMac_get(0, entry, &mac, &mac_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstMac", rv);
                memcpy(BRCM_SAI_ATTR_ACL_FLD_MAC(i), mac,
                       sizeof(bcm_mac_t));
                memcpy(BRCM_SAI_ATTR_ACL_FLD_MASK_MAC(i), mac_mask,
                       sizeof(bcm_mac_t));
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP:
                rv = bcm_field_qualify_SrcIp_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_IP4(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify SrcIp", rv);
                BRCM_SAI_ATTR_ACL_FLD_IP4(i) = ntohl(BRCM_SAI_ATTR_ACL_FLD_IP4(i));
                BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i) = ntohl(BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i));
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_DST_IP:
                rv = bcm_field_qualify_DstIp_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_IP4(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstIp", rv);
                BRCM_SAI_ATTR_ACL_FLD_IP4(i) = ntohl(BRCM_SAI_ATTR_ACL_FLD_IP4(i));
                BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i) = ntohl(BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i));
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_IN_PORTS:
            {
                int limit, p, index = 0;
                bcm_pbmp_t pbmp, mpbmp;
                
                if (0 == BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i))
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                }
                rv = bcm_field_qualify_InPorts_get(0, entry, &pbmp, &mpbmp);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                BCM_PBMP_COUNT(pbmp, p);
                if (BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i) < p)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                /* Set limit to max index depending on incoming list
                   size */
                limit = BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i) < p ? 
                        BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i)-1 : p-1;
                BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i) = p;
                BCM_PBMP_ITER(pbmp, p)
                {
                    if (index > limit)
                    {
                        break;
                    }
                    BRCM_SAI_ATTR_ACL_FLD_OBJ_LIST(i, index++) =
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, p);
                }
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORTS:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "OUT PORTS not supported on this platform.\n");
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT:
            {
                bcm_port_t data;
                bcm_port_t mask;

                rv = bcm_field_qualify_InPort_get(0, entry, &data, &mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPort", rv);
                BRCM_SAI_ATTR_ACL_FLD_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, data);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_ID:
                rv = bcm_field_qualify_OuterVlanId_get(0, entry, 
                         &BRCM_SAI_ATTR_ACL_FLD_16(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_PRI:
                rv = bcm_field_qualify_OuterVlanPri_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanPri", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_CFI:
                rv = bcm_field_qualify_OuterVlanCfi_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanCfi", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_INNER_VLAN_ID:
                rv = bcm_field_qualify_InnerVlanId_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_16(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InnerVlanId", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_INNER_VLAN_PRI:
                rv = bcm_field_qualify_InnerVlanPri_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InnerVlanPri", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_INNER_VLAN_CFI:
                rv = bcm_field_qualify_InnerVlanCfi_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InnerVlanCfi", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_L4_SRC_PORT:
            {
                bcm_l4_port_t port;
                bcm_l4_port_t mask;
                
                rv = bcm_field_qualify_L4SrcPort_get(0, entry, &port, &mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify L4SrcPort", rv);
                BRCM_SAI_ATTR_ACL_FLD_16(i) = port;
                BRCM_SAI_ATTR_ACL_FLD_MASK_16(i) = mask;
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_L4_DST_PORT:
            {
                bcm_l4_port_t port;
                bcm_l4_port_t mask;

                rv = bcm_field_qualify_L4DstPort_get(0, entry, 
                                                     &port, &mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify L4DstPort", rv);
                BRCM_SAI_ATTR_ACL_FLD_16(i) = port;
                BRCM_SAI_ATTR_ACL_FLD_MASK_16(i) = mask;
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_ETHER_TYPE:
                rv = bcm_field_qualify_EtherType_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_16(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_IP_PROTOCOL:
                rv = bcm_field_qualify_IpProtocol_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpProtocol", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_DSCP:
            {
                uint8 val, mask;

                rv = bcm_field_qualify_DSCP_get(0, entry, &val, &mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DSCP", rv);
                BRCM_SAI_ATTR_ACL_FLD_8(i) = val >> 2;
                BRCM_SAI_ATTR_ACL_FLD_MASK_8(i) = mask >> 2;
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_ECN:
            {
                uint8 val, mask;

                rv = bcm_field_qualify_DSCP_get(0, entry, &val, &mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DSCP", rv);
                BRCM_SAI_ATTR_ACL_FLD_8(i) = val & 0x3;
                BRCM_SAI_ATTR_ACL_FLD_MASK_8(i) = mask & 0x3;
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_TTL:
                rv = bcm_field_qualify_Ttl_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify Ttl", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_TOS:
                rv = bcm_field_qualify_Tos_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify Tos", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_IP_FLAGS:
                rv = bcm_field_qualify_IpFlags_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpFlags", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_TCP_FLAGS:
                rv = bcm_field_qualify_TcpControl_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify TcpControl", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_TYPE:
            {
                bcm_field_IpType_t type;
                
                rv = bcm_field_qualify_IpType_get(0, entry, &type);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpType", rv);
                BRCM_SAI_ATTR_ACL_FLD_32(i) = BRCM_IPTYPE_BCM_TO_SAI(type);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_FRAG:
            {
                bcm_field_IpFrag_t frag_info;

                rv = bcm_field_qualify_IpFrag_get(0, entry, &frag_info);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpFrag", rv);
                BRCM_SAI_ATTR_ACL_FLD_32(i) = BRCM_IPFRAG_BCM_TO_SAI(frag_info);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_TC:
                rv = bcm_field_qualify_IntPriority_get(0, entry,
                         &BRCM_SAI_ATTR_ACL_FLD_8(i),
                         &BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IntPriority", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_ACL_RANGE_TYPE:
            {
                sai_status_t rv1;
                bcm_field_range_t *range = NULL;
                int r, limit, count, *invert = NULL;
                
                if (0 == BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i))
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                }
                rv = bcm_field_qualify_RangeCheck_get(0, entry, 0, NULL, NULL,
                                                      &count);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify RangeCheck", rv);
                if (count > BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i))
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                limit = BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i) < count ? 
                        BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i) : count;
                BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i) = count;
                invert = ALLOC(sizeof(int) * limit);
                if (IS_NULL(invert))
                {
                    break;
                }
                range = ALLOC(sizeof(bcm_field_range_t) * limit);
                if (IS_NULL(range))
                {
                    FREE(invert);
                    break;
                }
                rv1 = bcm_field_qualify_RangeCheck_get(0, entry, limit, range, invert,
                                                       &count);
                BRCM_SAI_API_CHK_FREE2(SAI_API_ACL, "field qualify RangeCheck", 
                                       rv1, invert, range);
                for (r=0; r<limit; r++)
                {
                    BRCM_SAI_ATTR_ACL_FLD_OBJ_LIST(i, r) =
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_RANGE, range[r]);
                }
                break;
            }
            
            /* Actions */
            case SAI_ACL_ENTRY_ATTR_ACTION_REDIRECT:
                rv = bcm_field_action_get(0, entry, bcmFieldActionRedirectPort,
                                          &val32, &_val32);
                if (BCM_SUCCESS(rv))
                {
                    BRCM_SAI_ATTR_ACL_ACTION_OBJ(i) = 
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, _val32);
                    break;
                }
                else if (BCM_E_NOT_FOUND != rv)
                {
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                    break;
                }
                rv = bcm_field_action_get(0, entry, bcmFieldActionRedirectTrunk,
                                          &val32, &_val32);
                if (BCM_SUCCESS(rv))
                {
                    BRCM_SAI_ATTR_ACL_ACTION_OBJ(i) = 
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG, _val32);
                    break;
                }
                else if (BCM_E_NOT_FOUND != rv)
                {
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                    break;
                }
                rv = bcm_field_action_get(0, entry, bcmFieldActionRedirectEgrNextHop,
                                          &val32, &_val32);
                if (BCM_SUCCESS(rv))
                {
                    if (val32 >= 200000)
                    {
                        BRCM_SAI_ATTR_ACL_ACTION_OBJ(i) =
                            BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_NEXT_HOP_GROUP, val32);
                        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Creating NHG object with id: %d\n", val32);
                    }
                    else
                    {
                        /* Will need to search for this egress object and get the nhid 
                         * to re-construct the NH object.
                         */
                         int n, max;
                         bool found = FALSE;
                         _brcm_sai_indexed_data_t data;

                         BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                                          "Creating NH object with id: %d\n", val32);
                         max = _brcm_sai_max_nh_count_get();
                         for (n=1; n<max; n++)
                         {
                             rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                                             &n, &data);
                             BRCM_SAI_RV_CHK(SAI_API_ACL, "nh info data get", rv);
                             if (data.nh_info.idx && 
                                 (SAI_NEXT_HOP_TYPE_IP == data.nh_info.type_state ||
                                  _BRCM_SAI_NH_UNKOWN_NBR_TYPE == data.nh_info.type_state) &&
                                 (val32 == data.nh_info.if_id))
                             {
                                 found = TRUE;
                                 break;
                             }
                         }
                         if (found)
                         {
                             BRCM_SAI_ATTR_ACL_ACTION_OBJ(i) =
                                 BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_NEXT_HOP, n);
                             BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                                              "Creating NH object with id: %d\n", n);
                         }
                         else
                         {
                             BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                              "Unable to determine NH object value.\n");
                             return SAI_STATUS_INVALID_PARAMETER;
                         }
                    }
                    break;
                }
                else if (BCM_E_NOT_FOUND != rv)
                {
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                    break;
                }
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION:
            case SAI_ACL_ENTRY_ATTR_ACTION_FLOOD:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_COUNTER:
                rv = bcm_field_entry_stat_get(0, entry, &val);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry stat get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_COUNTER, val);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_INGRESS:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionMirrorIngress, 0, &val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, val);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_EGRESS:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionMirrorEgress, 0, &val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, val);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_POLICER:
            case SAI_ACL_ENTRY_ATTR_ACTION_DECREMENT_TTL:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_TC:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionPrioIntNew, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_VAL_8(i) = val32;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_PACKET_COLOR:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionDropPrecedence, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                switch(val32)
                {
                    case BCM_FIELD_COLOR_GREEN: 
                        BRCM_SAI_ATTR_ACL_ACTION_VAL_32(i) = SAI_PACKET_COLOR_GREEN;
                        break;
                    case BCM_FIELD_COLOR_YELLOW:
                        BRCM_SAI_ATTR_ACL_ACTION_VAL_32(i) = SAI_PACKET_COLOR_YELLOW;
                        break;
                    case BCM_FIELD_COLOR_RED:
                        BRCM_SAI_ATTR_ACL_ACTION_VAL_32(i) = SAI_PACKET_COLOR_RED;
                        break;
                    default:
                        rv = SAI_STATUS_FAILURE;
                }
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_INNER_VLAN_ID:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionInnerVlanNew, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_VAL_16(i) = val32;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_INNER_VLAN_PRI:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionInnerVlanPrioNew, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_VAL_8(i) = val32;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_OUTER_VLAN_ID:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionOuterVlanNew, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_VAL_16(i) = val32;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_OUTER_VLAN_PRI:
                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionOuterVlanPrioNew, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_VAL_8(i) = val32;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_SRC_MAC:
            {
                bcm_l3_egress_t l3egress;
                bcm_l3_intf_t l3intf;

                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionChangeL2Fields, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                rv =  bcm_l3_egress_get(0, val32, &l3egress);
                BRCM_SAI_API_CHK(SAI_API_ACL,"L3 egress get for SrcMacNew", rv);
                l3intf.l3a_intf_id = l3egress.intf;
                rv =  bcm_l3_intf_get(0, &l3intf);
                BRCM_SAI_API_CHK(SAI_API_ACL,"L3 intf get for SrcMacNew", rv);
                memcpy(BRCM_SAI_ATTR_ACL_ACTION_MAC(i), l3intf.l3a_mac_addr,
                       sizeof(sai_mac_t));
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DST_MAC:
            {
                bcm_l3_egress_t l3egress;

                rv = bcm_field_action_get(0, entry,
                         bcmFieldActionChangeL2Fields, &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                rv =  bcm_l3_egress_get(0, val32, &l3egress);
                BRCM_SAI_API_CHK(SAI_API_ACL,"L3 egress get for DstMacNew", rv);
                memcpy(BRCM_SAI_ATTR_ACL_ACTION_MAC(i), l3egress.mac_addr,
                       sizeof(sai_mac_t));
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_SRC_IP:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DST_IP:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_SRC_IPV6:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DST_IPV6:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DSCP:
                rv = bcm_field_action_get(0, entry,
                                              bcmFieldActionDscpNew,
                                              &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_VAL_8(i) = val32;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_ECN:
                rv = bcm_field_action_get(0, entry,
                                              bcmFieldActionEcnNew,
                                              &val32, &_val32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action get", rv);
                BRCM_SAI_ATTR_ACL_ACTION_VAL_8(i) = val32;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_L4_SRC_PORT:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_L4_DST_PORT:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            default:
                rv = SAI_STATUS_INVALID_ATTRIBUTE_0+i;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Create an ACL counter
*
* Arguments:
*   [out] acl_counter_id - the acl counter id
*   [in] attr_count - number of attributes
*   [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_acl_counter(_Out_ sai_object_id_t *acl_counter_id,
                            _In_ sai_object_id_t switch_id,
                            _In_ uint32_t attr_count,
                            _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv;
    int i, stat_id, nstat = 0;
    bcm_field_group_t group = -1;
    bcm_field_stat_t stat_arr[_BRCM_SAI_ACL_MAX_STATS];

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(acl_counter_id);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_COUNTER_ATTR_TABLE_ID:
                group = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_field_group_t, i);
                break;
            case SAI_ACL_COUNTER_ATTR_ENABLE_PACKET_COUNT:
                if (2 > nstat)
                {
                    stat_arr[nstat] = bcmFieldStatPackets;
                    nstat++;
                }
                else
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Unsupported number of stats.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            case SAI_ACL_COUNTER_ATTR_ENABLE_BYTE_COUNT:
                if (2 > nstat)
                {
                    stat_arr[nstat] = bcmFieldStatBytes;
                    nstat++;
                }
                else
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Unsupported number of stats.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            default:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown acl counter attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_ATTR_NOT_SUPPORTED_0+i;
        }
    }
    if ((-1 == group) || (0 == nstat))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "No table id or stat count\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    rv = bcm_field_stat_create(0, group, nstat, stat_arr, &stat_id);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field stat create", rv);
    *acl_counter_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_COUNTER, stat_id);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Delete an ACL counter
*
* Arguments:
 *  [in] acl_counter_id - the acl counter id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_delete_acl_counter(_In_ sai_object_id_t acl_counter_id)
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Del acl counter id %d\n",
                     BRCM_SAI_GET_OBJ_VAL(int, acl_counter_id));
    rv = bcm_field_stat_destroy(0, BRCM_SAI_GET_OBJ_VAL(int, acl_counter_id));
    BRCM_SAI_API_CHK(SAI_API_ACL, "field stat destroy", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Set ACL counter attribute
*
* Arguments:
*    [in] acl_counter_id - the acl counter id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_acl_counter_attribute(_In_ sai_object_id_t acl_counter_id,
                                   _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Get ACL counter attribute
*
* Arguments:
*    [in] acl_counter_id - acl counter id
*    [in] attr_count - number of attributes
*    [Out] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_acl_counter_attribute(_In_ sai_object_id_t acl_counter_id,
                                   _In_ uint32_t attr_count,
                                   _Out_ sai_attribute_t *attr_list)
{
    int stat, i, nstat = 0;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    uint64 value_arr[_BRCM_SAI_ACL_MAX_STATS];
    bcm_field_stat_t stat_arr[_BRCM_SAI_ACL_MAX_STATS];

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_ACL_COUNTER_ATTR_PACKETS:
                stat = bcmFieldStatPackets;
                break;
            case SAI_ACL_COUNTER_ATTR_BYTES:
                stat = bcmFieldStatBytes;
                break;
            default:
                return SAI_STATUS_ATTR_NOT_SUPPORTED_0+i;
        }
        if (_BRCM_SAI_ACL_MAX_STATS > nstat)
        {
            stat_arr[nstat] = stat;
            nstat++;
        }
        else
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Unsupported number of stats.\n");
            return SAI_STATUS_INVALID_PARAMETER;
        }
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Stat id: %d, count: %d\n",
                     BRCM_SAI_GET_OBJ_VAL(int, acl_counter_id), nstat);
    rv = bcm_field_stat_multi_get(0,
             BRCM_SAI_GET_OBJ_VAL(int, acl_counter_id), nstat, stat_arr,
             value_arr);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field stat multi get", rv);
    for (i=0; i<nstat; i++)
    {
        attr_list[i].value.u64 = value_arr[i];
    }
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 *   Routine Description:
 *     @brief Create an ACL Range
 *
 *  Arguments:
 *  @param[out] acl_range_id - the acl range id
 *  @param[in] attr_count - number of attributes
 *  @param[in] attr_list - array of attributes
 *
 *  Return Values:
 *    @return  SAI_STATUS_SUCCESS on success
 *             Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_acl_range(_Out_ sai_object_id_t* acl_range_id,
                          _In_ sai_object_id_t switch_id,
                          _In_ uint32_t attr_count,
                          _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv;
    uint32 flags = 0;
    int i, min = -1, max = -1;
    bcm_field_range_t range;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    for (i=0; i<attr_count; i++)
    {
        if (SAI_ACL_RANGE_ATTR_TYPE == attr_list[i].id)
        {
            switch (attr_list[i].value.u32)
            {
                case SAI_ACL_RANGE_TYPE_L4_SRC_PORT_RANGE:
                    flags = BCM_FIELD_RANGE_SRCPORT;
                    break;
                case SAI_ACL_RANGE_TYPE_L4_DST_PORT_RANGE:
                    flags = BCM_FIELD_RANGE_DSTPORT;
                    break;
                case SAI_ACL_RANGE_TYPE_OUTER_VLAN:
                    flags = BCM_FIELD_RANGE_OUTER_VLAN;
                    break;
                case SAI_ACL_RANGE_TYPE_INNER_VLAN:
                    flags = BCM_FIELD_RANGE_INNER_VLAN;
                    break;
                case SAI_ACL_RANGE_TYPE_PACKET_LENGTH:
                    flags = BCM_FIELD_RANGE_PACKET_LENGTH;
                    break;
                default:
                    return SAI_STATUS_INVALID_ATTR_VALUE_0+i;
            }
        }
        else if (SAI_ACL_RANGE_ATTR_LIMIT == attr_list[i].id)
        {
            min = attr_list[i].value.u32range.min;
            max = attr_list[i].value.u32range.max;
        }
    }
    if (0 == flags || -1 == min || -1 == max)
    {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    rv = bcm_field_range_create(0, &range, flags, min, max);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field range create", rv);
    *acl_range_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_RANGE, range);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 *  Routine Description:
 *    @brief Remove an ACL Range
 *
 *  Arguments:
 *    @param[in] acl_range_id - the acl range id
 *
 *  Return Values:
 *    @return  SAI_STATUS_SUCCESS on success
 *             Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_acl_range(_In_ sai_object_id_t acl_range_id)
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_range_id, SAI_OBJECT_TYPE_ACL_RANGE))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Remove acl range: %d\n",
                     BRCM_SAI_GET_OBJ_VAL(bcm_field_range_t, acl_range_id));
    rv = bcm_field_range_destroy(0, BRCM_SAI_GET_OBJ_VAL(bcm_field_range_t,
                                                             acl_range_id));
    BRCM_SAI_API_CHK(SAI_API_ACL, "field range destroy", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * Routine Description:
 *   @brief Set ACL range attribute
 *
 * Arguments:
 *    @param[in] acl_range_id - the acl range id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return  SAI_STATUS_SUCCESS on success
 *             Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_acl_range_attribute(_In_ sai_object_id_t acl_range_id,
                                 _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * Routine Description:
 *   @brief Get ACL range attribute
 *
 * Arguments:
 *    @param[in] acl_range_id - acl range id
 *    @param[in] attr_count - number of attributes
 *    @param[out] attr_list - array of attributes
 *
 * Return Values:
 *    @return  SAI_STATUS_SUCCESS on success
 *             Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_acl_range_attribute(_In_ sai_object_id_t acl_range_id,
                                 _In_ uint32_t attr_count,
                                 _Out_ sai_attribute_t *attr_list)
{
    uint32 flags;
    sai_status_t rv;
    int i, rngid, min, max;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_range_id, SAI_OBJECT_TYPE_ACL_RANGE))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    rngid = BRCM_SAI_GET_OBJ_VAL(int, acl_range_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Get acl range: %d\n", rngid);
    rv = bcm_field_range_get(0, rngid, &flags, &min, &max);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field range get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_ACL_RANGE_ATTR_TYPE:
            {
                if (flags & BCM_FIELD_RANGE_SRCPORT)
                {
                    attr_list[i].value.u32 = SAI_ACL_RANGE_TYPE_L4_SRC_PORT_RANGE;
                } 
                else if (flags & BCM_FIELD_RANGE_DSTPORT)
                {
                    attr_list[i].value.u32 = SAI_ACL_RANGE_TYPE_L4_DST_PORT_RANGE;
                }
                else if (flags & BCM_FIELD_RANGE_OUTER_VLAN)
                {
                    attr_list[i].value.u32 = SAI_ACL_RANGE_TYPE_OUTER_VLAN;
                }
                else if (flags & BCM_FIELD_RANGE_INNER_VLAN)
                {
                    attr_list[i].value.u32 = SAI_ACL_RANGE_TYPE_INNER_VLAN;
                }
                else if (flags & BCM_FIELD_RANGE_PACKET_LENGTH)
                {
                    attr_list[i].value.u32 = SAI_ACL_RANGE_TYPE_PACKET_LENGTH;
                }
                break;
            }
            case SAI_ACL_RANGE_ATTR_LIMIT:
                attr_list[i].value.u32range.min = min;
                attr_list[i].value.u32range.max = max;
                break;
            default:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown ACL range attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_INFO,
                             "Error processing ACL range attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Create an ACL Table Group
 *
 * @param[out] acl_table_group_id The ACL group id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_acl_table_group(_Out_ sai_object_id_t *acl_table_group_id,
                                _In_ sai_object_id_t switch_id,
                                _In_ uint32_t attr_count,
                                _In_ const sai_attribute_t *attr_list)
{
    int32_t *blist;
    sai_status_t rv;    
    _brcm_sai_indexed_data_t data;
    int i, index, stage = -1, bcount = 0;
    int type = SAI_ACL_TABLE_GROUP_TYPE_SEQUENTIAL;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(acl_table_group_id);
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_TABLE_GROUP_ATTR_ACL_STAGE:
                stage = attr_list[i].value.u32;
                break;
            case SAI_ACL_TABLE_GROUP_ATTR_ACL_BIND_POINT_TYPE_LIST:
                bcount = attr_list[i].value.s32list.count;
                blist = attr_list[i].value.s32list.list;
                break;
            case SAI_ACL_TABLE_GROUP_ATTR_TYPE:
                type = attr_list[i].value.u32;
                break;
            default:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown acl table group attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_INVALID_ATTRIBUTE_0 + i;
        }
    }
    if (-1 == stage)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "No stage info available.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_ACL_TBL_GRP, 1,
                                              _BRCM_SAI_MAX_ACL_TABLE_GROUPS,
                                              &index);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR,
                        "Unexpected acl table group resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Using acl table group: %d\n", index);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &index, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group index data get", rv);
    data.acl_tbl_grp.idx = index;
    data.acl_tbl_grp.stage = stage;
    data.acl_tbl_grp.type = type;
    if (bcount)
    {
        int b;

        data.acl_tbl_grp.bind_types_count = bcount;
        for (b=0; b<bcount; b++)
        {
            data.acl_tbl_grp.bind_types[b] = blist[b];
        }
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &index, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data set", rv);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_ACL_TBL_GRPS_COUNT, INC))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Error incrementing acl table groups count global data.\n");
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                                index);
        return SAI_STATUS_FAILURE;
    }
    *acl_table_group_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_TABLE_GROUP,
                                              index);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL table group created: %d\n", index);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Delete an ACL Group
 *
 * @param[in] acl_table_group_id The ACL group id
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_acl_table_group(_In_ sai_object_id_t acl_table_group_id)
{
    sai_status_t rv;
    int table_group;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_table_group_id,
                                  SAI_OBJECT_TYPE_ACL_TABLE_GROUP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    table_group = BRCM_SAI_GET_OBJ_VAL(int, acl_table_group_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Remove acl table group: %d\n", table_group);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &table_group, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_tbl_grp.valid)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    if (data.acl_tbl_grp.ref_count)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Non zero ref count (%d) for acl table group.\n",
                         data.acl_tbl_grp.ref_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
#if 0
    if (data.acl_tbl_grp.bind_count)
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Non zero bind count (%d) for acl table group.\n",
                         data.acl_tbl_grp.bind_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
#endif
    /*_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_ACL_TBL_GRP, table_group);*/
    data.acl_tbl_grp.valid = FALSE;
    /* FIXME: this should be done in an un-bind call. Remove the following
     * once unbind is available
     */
    if (data.acl_tbl_grp.bind_count)
    {
        _brcm_sai_acl_bind_point_t *next, *bpts = data.acl_tbl_grp.bind_pts;
        while (bpts)
        {
            if (bpts->type ==  SAI_ACL_BIND_POINT_TYPE_LAG)
            {
                /* for lags we maintain a list of acl binds, we need
                   to delete it */
                rv = _brcm_sai_lag_unbind_acl_obj(bpts->val,
                                                  acl_table_group_id);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "acl unbind", rv);
            }
            next = bpts->next;
            FREE(bpts);
            bpts = next;
        }
        data.acl_tbl_grp.bind_pts = NULL;
        data.acl_tbl_grp.bind_count = 0;
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &table_group, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
    
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_ACL_TBL_GRPS_COUNT, DEC))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Error decrementing acl tables count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL table group removed.\n");

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Set ACL table group attribute
 *
 * @param[in] acl_table_group_id The ACL table group id
 * @param[in] attr Attribute
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_acl_table_group_attribute(_In_ sai_object_id_t acl_table_group_id,
                                       _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Get ACL table group attribute
 *
 * @param[in] acl_table_group_id ACL table group id
 * @param[in] attr_count Number of attributes
 * @param[out] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_acl_table_group_attribute(_In_ sai_object_id_t acl_table_group_id,
                                       _In_ uint32_t attr_count,
                                       _Out_ sai_attribute_t *attr_list)
{
    int i, c, gid;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_table_group_id, SAI_OBJECT_TYPE_ACL_TABLE_GROUP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    gid = BRCM_SAI_GET_OBJ_VAL(int, acl_table_group_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Get acl table group: %d\n", gid);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_ACL_TABLE_GROUP_ATTR_ACL_STAGE:
                attr_list[i].value.u32 = data.acl_tbl_grp.stage;
                break;
            case SAI_ACL_TABLE_GROUP_ATTR_ACL_BIND_POINT_TYPE_LIST:
            {
                int limit = data.acl_tbl_grp.bind_types_count;

                if (attr_list[i].value.s32list.count < data.acl_tbl_grp.bind_types_count)
                {
                    limit = attr_list[i].value.s32list.count;
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                for (c=0; c<limit; c++)
                {
                    attr_list[i].value.s32list.list[c] = data.acl_tbl_grp.bind_types[c];
                }
                attr_list[i].value.s32list.count = data.acl_tbl_grp.bind_types_count;
                break;
            }
            case SAI_ACL_TABLE_GROUP_ATTR_TYPE:
                attr_list[i].value.u32 = data.acl_tbl_grp.type;
                break;
            case SAI_ACL_TABLE_GROUP_ATTR_MEMBER_LIST:
            {
                sai_status_t _rv;
                _brcm_sai_indexed_data_t _data;
                int m, c = 0, max, limit = data.acl_tbl_grp.ref_count;

                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < data.acl_tbl_grp.ref_count)
                {
                    limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i);
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                max = _BRCM_SAI_MAX_ACL_TABLES * _BRCM_SAI_MAX_ACL_TABLES;
                for (m=1; m<max && c<limit; m++)
                {
                    _rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                                     &m, &_data);
                    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group member data get", _rv);
                    if (_data.acl_tbl_grp_membr.acl_tbl_grp == gid)
                    {
                        BRCM_SAI_ATTR_LIST_OBJ_LIST(i, c++) = 
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER,
                                                        _data.acl_tbl_grp_membr.acl_tbl_grp,
                                                        _data.acl_tbl_grp_membr.acl_table, m);
                    }
                }
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = data.acl_tbl_grp.ref_count;
                break;
            }
            default:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown ACL table group attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_INFO,
                             "Error processing ACL table group attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Create an ACL Table Group Member
 *
 * @param[out] acl_table_group_member_id The ACL table group member id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_acl_table_group_member(_Out_ sai_object_id_t *acl_table_group_member_id,
                                       _In_ sai_object_id_t switch_id,
                                       _In_ uint32_t attr_count,
                                       _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv;
    bool ingress = FALSE;
    bcm_field_qset_t qset;
    sai_object_id_t table_obj;
    _brcm_sai_acl_entry_t *entries;
    _brcm_sai_acl_bind_point_t *bp;
    int b, i, ec, tid, val, mask, state;
    _brcm_sai_indexed_data_t data, _data, __data;
    int index, stage, table_group = -1, table = -1, pri = -1;
    bool f_qset_changed = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(acl_table_group_member_id);
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_GROUP_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_ACL_TABLE_GROUP))
                {
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                table_group = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                break;
            case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_ACL_TABLE))
                {
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                table = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                table_obj = BRCM_SAI_ATTR_LIST_OBJ(i);
                break;
            case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_PRIORITY:
                pri = attr_list[i].value.u32;
                break;
            default:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown acl table group member attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_INVALID_ATTRIBUTE_0 + i;
        }
    }
    if (-1 == table_group || -1 == table || -1 == pri)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "No priority or table or group info available.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &table_group, &_data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &table, &__data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (TRUE != _data.acl_tbl_grp.valid || TRUE != __data.acl_table.valid)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Acl table group or acl table not valid.\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    stage = _data.acl_tbl_grp.stage;
    if (stage != __data.acl_table.stage)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Mismatching stage types for acl table group and acl table.\n");
    }
    if (SAI_ACL_STAGE_INGRESS == stage)
    {
        ingress = TRUE;
    }
    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR, 1,
                                              _BRCM_SAI_MAX_ACL_TABLES *
                                              _BRCM_SAI_MAX_ACL_TABLE_GROUPS, &index);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_VR(SAI_LOG_LEVEL_ERROR, "Unexpected table group member resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Using table group member: %d\n", index);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                    &index, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group member data get", rv);
    data.acl_tbl_grp_membr.idx = index;
    data.acl_tbl_grp_membr.acl_tbl_grp = table_group;
    data.acl_tbl_grp_membr.acl_table = table;
    data.acl_tbl_grp_membr.pri = pri;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                    &index, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group member data set", rv);
    
    /* Create associations, update properties */
    rv = bcm_field_group_priority_set(0, table, pri);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field group priority set", rv);
    
    if (_data.acl_tbl_grp.bind_types_count)
    {
        rv = bcm_field_group_get(0, table, &qset);
        BRCM_SAI_API_CHK(SAI_API_ACL, "field group get", rv);
        for (i=0; i<_data.acl_tbl_grp.bind_types_count; i++)
        {
            switch (_data.acl_tbl_grp.bind_types[i])
            {
                case SAI_ACL_BIND_POINT_TYPE_PORT:
                    if (ingress)
                    {
                        if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts))
                        {
                            BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPorts);
                            f_qset_changed = TRUE;
                        }
                    }
                    else
                    {
                        if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOutPort))
                        {
                            BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOutPort);
                            f_qset_changed = TRUE;
                        }
                    }
                    break;
                case SAI_ACL_BIND_POINT_TYPE_LAG:
                    if (ingress)
                    {
                        if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts))
                        {
                            BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPorts);
                            f_qset_changed = TRUE;
                        }
                    }
                    else
                    {
#ifndef SAI_FIXUP_DISABLE_DEST_TRUNK_QUALIFIER
                        if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDstTrunk))
                        {
                            BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstTrunk);
                            f_qset_changed = TRUE;
                        }
#endif
                    }
                    break;
                case SAI_ACL_BIND_POINT_TYPE_VLAN:
                    if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOuterVlanId))
                    {
                        BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOuterVlanId);
                        f_qset_changed = TRUE;
                    }
                    break;
                case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF:
                    return SAI_STATUS_NOT_IMPLEMENTED;
                case SAI_ACL_BIND_POINT_TYPE_SWITCH:
                    break;
                default:
                    return SAI_STATUS_FAILURE;
            }
        }
        if (f_qset_changed)
        {
            rv = bcm_field_group_set(0, table, qset);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field group set", rv);
        }
    }

    /* FIXME: Bind to group happens after tables and/or entries
     *        have already been created - not currently supported.
     */

    if (__data.acl_table.ref_count && _data.acl_tbl_grp.bind_pts)
    {
        tid = __data.acl_table.idx;
        bp = _data.acl_tbl_grp.bind_pts;
        for (b=0; b<_data.acl_tbl_grp.bind_count; b++)
        {
            /* reload to get new counts */
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &table, &__data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
            ec = __data.acl_table.ref_count;
            entries = __data.acl_table.entries;
            switch (bp->type)
            {
                case SAI_ACL_BIND_POINT_TYPE_PORT:
                    if (ingress)
                    {
                        bcm_pbmp_t pbmp, mpbmp;
                        for (i=0; i<ec; i++)
                        {
                            if (0 == entries->uid)
                            {
                                rv = bcm_field_qualify_InPorts_get(0, entries->id, &pbmp, &mpbmp);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify inports get", rv);
                                BCM_PBMP_PORT_ADD(pbmp, bp->val);
                                _brcm_sai_switch_pbmp_fp_all_get(&mpbmp); /* required for TH */
                                rv = bcm_field_qualify_InPorts(0, entries->id, pbmp, mpbmp);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                                rv = bcm_field_entry_reinstall(0, entries->id);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                            }
                            entries = entries->next;
                        }    
                    }
                    else
                    {
                        for (i=0; i<ec; i++)
                        {
                            if (0 == entries->uid)
                            {
                                rv = bcm_field_qualify_OutPort_get(0, entries->id, &val, &mask);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort get", rv);
                                if (0 != val)
                                {
                                    if (val != bp->val)
                                    {
                                        rv = bcm_field_entry_enable_get(0, entries->id, &state);
                                        BRCM_SAI_API_CHK(SAI_API_ACL, "field entry enable get", rv);
                                        /* user entry already has out port qualifier,
                                           so just clone */
                                        rv = _brcm_sai_field_entry_copy(0, tid, entries->id, bp->type,
                                                                        bp->val, ingress, state);
                                        BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                                    }
                                }
                                else
                                {
                                    int e = i, uentry = entries->id;
                                    _brcm_sai_acl_entry_t *_entries;
 
                                    /* No out port qualifier in user entry 
                                       so update all existing clones including user entry */
                                    rv = bcm_field_qualify_OutPort(0, entries->id, bp->val,
                                                                   _BRCM_SAI_MASK_32);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
                                    rv = bcm_field_entry_reinstall(0, entries->id);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                    _entries = entries->next;
                                    while (_entries && e<ec)
                                    {
                                        if (uentry == _entries->uid)
                                        {
                                            rv = bcm_field_qualify_OutPort(0, _entries->id, bp->val,
                                                                           _BRCM_SAI_MASK_32);
                                            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
                                            rv = bcm_field_entry_reinstall(0, _entries->id);
                                            BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                            _entries->bind_mask |= (1 << bp->type);
                                        }
                                        _entries = _entries->next;
                                        e++;
                                    }
                                }
                            }
                            entries = entries->next;
                        }    
                    }
                    break;
                case SAI_ACL_BIND_POINT_TYPE_LAG:
                    if (ingress)
                    {
                        for (i=0; i<ec; i++)
                        {
                            if (0 == entries->uid)
                            {
                                /* Add or update */
                                rv = _brcm_sai_field_qualify_ing_lag(entries->id,
                                                                     bp->val);
                                BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry src trunk add ", rv);
                                rv = bcm_field_entry_reinstall(0, entries->id);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                            }
                            entries = entries->next;
                        }
                    }
                    else
                    {
                        for (i=0; i<ec; i++)
                        {
                            if (0 == entries->uid)
                            {
                                rv = bcm_field_qualify_DstTrunk_get(0, entries->id, &val, &mask);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstTrunk get", rv);
                                if (0 != mask)
                                {
                                    if (val != bp->val)
                                    {
                                        /* user entry already has lag qualifier,
                                           so just clone */
                                        rv = bcm_field_entry_enable_get(0, entries->id, &state);
                                        BRCM_SAI_API_CHK(SAI_API_ACL, "field entry enable get", rv);
                                        rv = _brcm_sai_field_entry_copy(0, tid, entries->id, bp->type,
                                                                        bp->val, ingress, state);
                                        BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                                    }
                                }
                                else
                                {
                                    int e = i, uentry = entries->id;
                                    _brcm_sai_acl_entry_t *_entries;

                                    /* No lag qualifier in user entry
                                       so update all existing clones including user entry */
                                    rv = bcm_field_qualify_DstTrunk(0, entries->id, bp->val, 0xffff);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify dst trunk", rv);
                                    rv = bcm_field_entry_reinstall(0, entries->id);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                    _entries = entries->next;
                                    while (_entries && e<ec)
                                    {
                                        if (uentry == _entries->uid)
                                        {
                                            rv = bcm_field_qualify_DstTrunk(0, entries->id, bp->val, 0xffff);
                                            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify dst trunk", rv);
                                            rv = bcm_field_entry_reinstall(0, _entries->id);
                                            BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                            _entries->bind_mask |= (1 << bp->type);
                                        }
                                        _entries = _entries->next;
                                        e++;
                                    }
                                }
                            }
                            entries = entries->next;
                        }
                    }
                    break;
                case SAI_ACL_BIND_POINT_TYPE_VLAN:
                {
                    bcm_vlan_t val, mask;

                    for (i=0; i<ec; i++)
                    {
                        if (0 == entries->uid)
                        {
                            rv = bcm_field_qualify_OuterVlanId_get(0, entries->id, &val, &mask);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId get", rv);
                            if (0 != val)
                            {
                                if (val != bp->val)
                                {
                                    /* user entry already has vlan qualifier,
                                       so clone it */
                                    rv = bcm_field_entry_enable_get(0, entries->id, &state);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry enable get", rv);
                                    rv = _brcm_sai_field_entry_copy(0, tid, entries->id, bp->type,
                                                                    bp->val, ingress, state);
                                    BRCM_SAI_RV_CHK(SAI_API_ACL, "field entry copy", rv);
                                }
                            }
                            else
                            {
                                int e = i, uentry = entries->id;
                                _brcm_sai_acl_entry_t *_entries;

                                /* No vlan qualifier in user entry 
                                   so update all existing clones including user entry */
                                rv = bcm_field_qualify_OuterVlanId(0, entries->id, bp->val,
                                                                   _BRCM_SAI_MASK_16);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
                                rv = bcm_field_entry_reinstall(0, entries->id);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                _entries = entries->next;
                                while (_entries && e<ec)
                                {
                                    if (uentry == _entries->uid)
                                    {
                                        rv = bcm_field_qualify_OuterVlanId(0, _entries->id, bp->val,
                                                                           _BRCM_SAI_MASK_16);
                                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
                                        rv = bcm_field_entry_reinstall(0, _entries->id);
                                        BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                        _entries->bind_mask |= (1 << bp->type);
                                    }
                                    _entries = _entries->next;
                                    e++;
                                }
                            }
                        }
                        entries = entries->next;
                    }    
                    break;
                }
                case SAI_ACL_BIND_POINT_TYPE_SWITCH:
                    break;
                case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF:
                     return SAI_STATUS_NOT_IMPLEMENTED;
                default:
                    return SAI_STATUS_FAILURE;
            }
            bp = bp->next;
        }
    }
    rv = _brcm_sai_acl_table_group_member_attach(table_group, table_obj);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group member attach", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &table, &__data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    __data.acl_table.group_count++;
    __data.acl_table.group[table_group-1] = table_group;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &table, &__data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_ACL_TBL_GRPS_MEMBRS_COUNT, INC))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Error incrementing acl table group members count "
                         "global data.\n");
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                                index);
        return SAI_STATUS_FAILURE;
    }
    *acl_table_group_member_id = 
        BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER,
                                    table_group, table, index);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL table group member created: %d\n", index);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Delete an ACL Group Member
 *
 * @param[in] acl_table_group_member_id The ACL table group member id
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_acl_table_group_member(_In_ sai_object_id_t acl_table_group_member_id)
{
    sai_status_t rv;
    bcm_field_qset_t qset;
    _brcm_sai_acl_entry_t *entries;
    _brcm_sai_acl_bind_point_t *bp;
    bool group_only, ingress  = FALSE;
    _brcm_sai_indexed_data_t data, _data;
    int ec, b, index, t, table, table_group, val, mask;
    bool f_qset_changed = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    
    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_table_group_member_id,
                                  SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    index = BRCM_SAI_GET_OBJ_VAL(int, acl_table_group_member_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                    &index, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group member data get", rv);
    table_group = data.acl_tbl_grp_membr.acl_tbl_grp;
    table = data.acl_tbl_grp_membr.acl_table;
    if (table_group != BRCM_SAI_GET_OBJ_SUB_TYPE(acl_table_group_member_id) ||
        table != BRCM_SAI_GET_OBJ_MAP(acl_table_group_member_id))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Acl table group or acl table do not match.\n");
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                      index);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &table_group, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &table, &_data);
    if (SAI_ACL_STAGE_INGRESS == data.acl_tbl_grp.stage)
    {
        ingress = TRUE;
    }
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    rv = bcm_field_group_priority_set(0, table, BCM_FIELD_GROUP_PRIO_ANY);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field group priority set", rv);
    
    if (_data.acl_table.ref_count && data.acl_tbl_grp.bind_pts)
    {
        int deleted = 0;

        bp = data.acl_tbl_grp.bind_pts;
        for (b=0; b<data.acl_tbl_grp.bind_count; b++)
        {
            /* reload to get new counts */
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &table, &_data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
            ec = _data.acl_table.ref_count;
            entries = _data.acl_table.entries;
            switch (bp->type)
            {
                case SAI_ACL_BIND_POINT_TYPE_PORT:
                    if (ingress)
                    {
                        bcm_pbmp_t pbmp, mpbmp;

                        for (t=0; t<ec; t++)
                        {
                            if (0 == entries->uid)
                            {
                                _brcm_sai_switch_pbmp_fp_all_get(&mpbmp);
                                rv = bcm_field_qualify_InPorts_get(0, entries->id, &pbmp, &mpbmp);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify inports get", rv);
                                BCM_PBMP_PORT_REMOVE(pbmp, bp->val);
                                rv = bcm_field_qualify_InPorts(0, entries->id, pbmp, mpbmp);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                                rv = bcm_field_entry_reinstall(0, entries->id);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                            }
                            entries = entries->next;
                        }    
                    }
                    else
                    {
                        for (t=0; t<ec; t++)
                        {
                            if (entries->uid && 0 == entries->bind_mask)
                            {
                                /* must've already removed this entry */
                                entries = entries->next;
                                continue;
                            }
                            rv = bcm_field_qualify_OutPort_get(0, entries->id, &val, &mask);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort get", rv);
                            if (0 != mask && val == bp->val)
                            {
                                /* remove qualifier from h/w entry */
                                rv = bcm_field_qualify_OutPort(0, entries->id, 0, mask);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
                                if (entries->uid)
                                {
                                    entries->bind_mask &= ~(1 << bp->type);
                                }
                                if (entries->uid && 0 == entries->bind_mask)
                                {
                                    rv = bcm_field_entry_destroy(0, entries->id);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry destroy", rv);
                                    deleted++;
                                }
                                else
                                {
                                    rv = bcm_field_entry_reinstall(0, entries->id);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                }

                            }
                            entries = entries->next;
                        }
                    }
                    break;
                case SAI_ACL_BIND_POINT_TYPE_LAG:
                     if (ingress)
                        {
                            bcm_pbmp_t pbmp, mpbmp;
                            bcm_pbmp_t new_pbmp;
                            for (t=0; t<ec; t++)
                            {
                                if (0 == entries->uid)
                                {
                                    BCM_PBMP_CLEAR(pbmp);
                                    BCM_PBMP_CLEAR(mpbmp);
                                    rv = bcm_field_qualify_InPorts_get(0, entries->id, &pbmp, &mpbmp);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts get", rv);
                                    _brcm_sai_lag_member_pbmp_get(bp->val, &new_pbmp);

                                    /* remove the old pbmp */
                                    BCM_PBMP_REMOVE(pbmp, new_pbmp);
                                    rv = bcm_field_qualify_InPorts(0, entries->id, pbmp, mpbmp);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify inports", rv);
                                    rv = bcm_field_entry_reinstall(0, entries->id);
                                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);                                   
                                }

                                
                                entries = entries->next;
                            }
                        }
                     else
                     {
                         for (t=0; t<ec; t++)
                         {
                             if (entries->uid && 0 == entries->bind_mask)
                             {
                                 /* must've already removed this entry */
                                 entries = entries->next;
                                 continue;
                             }
                             rv = bcm_field_qualify_DstTrunk_get(0, entries->id, &val, &mask);
                             BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstTrunk get", rv);
                             
                             if (0 != mask && val == bp->val)
                             {                                
                                 rv = bcm_field_qualify_DstTrunk(0, entries->id, 0, mask);
                                 BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify dst trunk", rv);
                                 if (entries->uid)
                                 {
                                     entries->bind_mask &= ~(1 << bp->type);
                                 }
                                 if (entries->uid && 0 == entries->bind_mask)
                                 {
                                     rv = bcm_field_entry_destroy(0, entries->id);
                                     BRCM_SAI_API_CHK(SAI_API_ACL, "field entry destroy", rv);
                                     deleted++;
                                 }
                                 else
                                 {
                                     rv = bcm_field_entry_reinstall(0, entries->id);
                                     BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                                 }
                             }
                             entries = entries->next;
                         }
                     }
                     break;
                case SAI_ACL_BIND_POINT_TYPE_VLAN:
                {
                    bcm_vlan_t val, mask;

                    for (t=0; t<ec; t++)
                    {
                        if (entries->uid && 0 == entries->bind_mask)
                        {
                            /* must've already removed this entry */
                            entries = entries->next;
                            continue;
                        }
                        rv = bcm_field_qualify_OuterVlanId_get(0, entries->id, &val, &mask);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId get", rv);
                        if (0 != mask && val == bp->val)
                        {
                            /* remove qualifier from h/w entry */
                            rv = bcm_field_qualify_OuterVlanId(0, entries->id, 0, mask);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
                            if (entries->uid)
                            {
                                entries->bind_mask &= ~(1 << bp->type);
                            }
                            if (entries->uid && 0 == entries->bind_mask)
                            {
                                rv = bcm_field_entry_destroy(0, entries->id);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry destroy", rv);
                                deleted++;
                            }
                            else
                            {
                                rv = bcm_field_entry_reinstall(0, entries->id);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                            }
                        }
                        entries = entries->next;
                    }
                    break;
                }
                case SAI_ACL_BIND_POINT_TYPE_SWITCH:
                    break;
                case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF:
                     return SAI_STATUS_NOT_IMPLEMENTED;
                default:
                    return SAI_STATUS_FAILURE;
            }
            bp = bp->next;
        }
        /* Traverse and remove unwanted clone entries */
        if (deleted)
        {
            _brcm_sai_acl_entry_t *prev;

            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &table, &_data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
            entries = _data.acl_table.entries;
            prev = entries;
            while (!IS_NULL(entries))
            {
                if (entries->uid && 0 == entries->bind_mask)
                {
                    prev->next = entries->next;
                    FREE(entries);
                    entries = prev;
                    _data.acl_table.ref_count--;
                }
                else
                {
                    prev = entries;
                }
                entries = entries->next;
            }
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &table, &_data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
        }
    }

    if (data.acl_tbl_grp.bind_types_count)
    {
        rv = bcm_field_group_get(0, table, &qset);
        BRCM_SAI_API_CHK(SAI_API_ACL, "field group get", rv);
        for (b=0; b<data.acl_tbl_grp.bind_types_count; b++)
        {
            group_only = TRUE;
            for (t=0; t<_data.acl_table.bind_types_count; t++)
            {
                if (data.acl_tbl_grp.bind_types[b] == _data.acl_table.bind_types[t])
                {
                    group_only = FALSE;
                    break;
                }
            }
            if (TRUE == group_only)
            {
                switch (data.acl_tbl_grp.bind_types[b])
                {
                    case SAI_ACL_BIND_POINT_TYPE_SWITCH:
                        break;
                    case SAI_ACL_BIND_POINT_TYPE_PORT:
                        if (ingress)
                        {
                            if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts))
                            {
                                BCM_FIELD_QSET_REMOVE(qset, bcmFieldQualifyInPorts);
                                f_qset_changed = TRUE;
                            }
                        }
                        else
                        {
                            if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOutPort))
                            {
                                BCM_FIELD_QSET_REMOVE(qset, bcmFieldQualifyOutPort);
                                f_qset_changed = TRUE;
                            }
                        }
                        break;
                    case SAI_ACL_BIND_POINT_TYPE_LAG:
                        if (ingress)
                        {
                            if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts))
                            {
                                BCM_FIELD_QSET_REMOVE(qset, bcmFieldQualifyInPorts);
                                f_qset_changed = TRUE;
                            }
                        }
                        else
                        {
#ifndef SAI_FIXUP_DISABLE_DEST_TRUNK_QUALIFIER
                            if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDstTrunk))
                            {
                                BCM_FIELD_QSET_REMOVE(qset, bcmFieldQualifyDstTrunk);
                                f_qset_changed = TRUE;
                            }
#endif
                        }
                        break;
                    case SAI_ACL_BIND_POINT_TYPE_VLAN:
                        if (BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOuterVlanId))
                        {
                            BCM_FIELD_QSET_REMOVE(qset, bcmFieldQualifyOuterVlanId);
                            f_qset_changed = TRUE;
                        }
                        break;
                    default:
                        return SAI_STATUS_FAILURE;
                }
            }
        }
        if (f_qset_changed)
        {
            rv = bcm_field_group_set(0, table, qset);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field group set", rv);
        }
    }
    rv = _brcm_sai_acl_table_group_member_detach(table_group, table);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group member detach", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &table, &_data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    _data.acl_table.group_count--;
    _data.acl_table.group[table_group-1] = 0;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &table, &_data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_ACL_TBL_GRPS_MEMBRS_COUNT, DEC))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Error decrementing acl table group members count "
                         "global data.\n");
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "ACL table group member removed: %d\n", index);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Set ACL table group member attribute
 *
 * @param[in] acl_table_group_member_id The ACL table group member id
 * @param[in] attr Attribute
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_acl_table_group_member_attribute(_In_ sai_object_id_t acl_table_group_member_id,
                                              _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/**
 * @brief Get ACL table group member attribute
 *
 * @param[in] acl_table_group_id ACL table group member id
 * @param[in] attr_count Number of attributes
 * @param[out] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_acl_table_group_member_attribute(_In_ sai_object_id_t acl_table_group_member_id,
                                              _In_ uint32_t attr_count,
                                              _Out_ sai_attribute_t *attr_list)
{
    int i, gmid;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(acl_table_group_member_id,
                                  SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    gmid = BRCM_SAI_GET_OBJ_VAL(int, acl_table_group_member_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Get acl table group member: %d\n", gmid);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                    &gmid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group member data get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_GROUP_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_TABLE_GROUP,
                                        data.acl_tbl_grp_membr.acl_tbl_grp);
                break;
            case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_TABLE,
                                        data.acl_tbl_grp_membr.acl_table);
                break; 
            case SAI_ACL_TABLE_GROUP_MEMBER_ATTR_PRIORITY:
                attr_list[i].value.u32 = data.acl_tbl_grp_membr.pri;
                break; 
            default:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown ACL table group member attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_INFO,
                             "Error processing ACL table group member attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
################################################################################
#                              Internal functions                              #
################################################################################
*/

STATIC sai_uint32_t
_brcm_sai_acl_table_groups_count_get()
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_global_data_get(_BRCM_SAI_ACL_TBL_GRPS_COUNT, &data))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                         "Error getting acl table groups count data.\n");
        return SAI_STATUS_FAILURE;
    }
    return data.u32;
}

STATIC sai_uint32_t
_brcm_sai_acl_tables_count_get()
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_global_data_get(_BRCM_SAI_ACL_TABLES_COUNT, &data))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                         "Error getting acl tables count data.\n");
        return SAI_STATUS_FAILURE;
    }
    return data.u32;
}

STATIC sai_uint32_t
_brcm_sai_acl_table_group_members_count_get()
{
    _brcm_sai_data_t data;
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_global_data_get(_BRCM_SAI_ACL_TBL_GRPS_MEMBRS_COUNT, &data))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                         "Error getting acl table group members count data.\n");
        return SAI_STATUS_FAILURE;
    }
    return data.u32;
}

/* Routine to allocate acl state */
sai_status_t
_brcm_sai_alloc_acl()
{
    int e;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);

    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    _BRCM_SAI_MAX_ACL_TABLE_GROUPS))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                         "Error initializing acl table group state !!\n");
        return SAI_STATUS_FAILURE;
    }
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    _BRCM_SAI_MAX_ACL_TABLES))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                         "Error initializing acl table state !!\n");
        return SAI_STATUS_FAILURE;
    }
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR,
                                    _BRCM_SAI_MAX_ACL_TABLES *
                                    _BRCM_SAI_MAX_ACL_TABLES))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                         "Error initializing acl table group member state !!\n");
        return SAI_STATUS_FAILURE;
    }
    
    if (_brcm_sai_switch_wb_state_get())
    {
        if (_brcm_sai_acl_table_groups_count_get())
        {
            for (e = 1; e < _BRCM_SAI_MAX_ACL_TABLE_GROUPS; e++)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                                &e, &data);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
                if (data.acl_tbl_grp.valid && data.acl_tbl_grp.bind_pts &&
                    data.acl_tbl_grp.bind_count)
                {
                    rv = _brcm_sai_list_init(_BRCM_SAI_LIST_ACL_GRP_BIND_POINTS,
                                             e, data.acl_tbl_grp.bind_count,
                                             (void**)&data.acl_tbl_grp.bind_pts);
                    BRCM_SAI_RV_CHK(SAI_API_ACL, "list init acl table group bind points", rv);
                }
                if (data.acl_tbl_grp.valid && data.acl_tbl_grp.acl_tables &&
                    data.acl_tbl_grp.ref_count)
                {
                    rv = _brcm_sai_list_init(_BRCM_SAI_LIST_ACL_TBL_GRP_MEMBR,
                                             e, data.acl_tbl_grp.ref_count,
                                             (void**)&data.acl_tbl_grp.acl_tables);
                    BRCM_SAI_RV_CHK(SAI_API_ACL, "list init acl table group members", rv);
                }
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                                &e, &data);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data set", rv);
            }
        }
        if (_brcm_sai_acl_tables_count_get())
        {
            for (e = 1; e < _BRCM_SAI_MAX_ACL_TABLES; e++)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                                &e, &data);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
                if (data.acl_table.valid && data.acl_table.entries &&
                    data.acl_table.ref_count)
                {
                    rv = _brcm_sai_list_init(_BRCM_SAI_LIST_ACL_ENTRIES,
                                             e, data.acl_table.ref_count,
                                             (void**)&data.acl_table.entries);
                    BRCM_SAI_RV_CHK(SAI_API_ACL, "list init acl table entries", rv);
                }
                if (data.acl_table.valid && data.acl_table.bind_pts &&
                    data.acl_table.bind_count)
                {
                    rv = _brcm_sai_list_init(_BRCM_SAI_LIST_ACL_TBL_BIND_POINTS,
                                             e, data.acl_table.bind_count,
                                             (void**)&data.acl_table.bind_pts);
                    BRCM_SAI_RV_CHK(SAI_API_ACL, "list init acl table bind points", rv);
                }
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                                &e, &data);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
            }
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_acl()
{
    int e;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_ACL_TBL_GRP_MEMBR, 
                                      1, _BRCM_SAI_MAX_ACL_TABLES *
                                      _BRCM_SAI_MAX_ACL_TABLES,
                                      _brcm_sai_acl_table_group_members_count_get());
    BRCM_SAI_RV_LVL_CHK(SAI_API_ACL, SAI_LOG_LEVEL_CRITICAL,
                        "freeing acl table group member state", rv);

    for (e = 1; e < _BRCM_SAI_MAX_ACL_TABLES; e++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                        &e, &data);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
        if (data.acl_table.valid && data.acl_table.entries &&
            data.acl_table.ref_count)
        {
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_ACL_ENTRIES,
                                     e, data.acl_table.ref_count,
                                     data.acl_table.entries);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "list free acl table entries", rv);
            data.acl_table.entries = (_brcm_sai_acl_entry_t*)(uint64_t)e;
        }
        if (data.acl_table.valid && data.acl_table.bind_pts &&
            data.acl_table.bind_count)
        {
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_ACL_TBL_BIND_POINTS,
                                     e, data.acl_table.bind_count,
                                     data.acl_table.bind_pts);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "list free acl table bind points", rv);
            data.acl_table.bind_pts = (_brcm_sai_acl_bind_point_t*)(uint64_t)e;
        }
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                        &e, &data);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
    }
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_ACL_TABLE, 
                                      1, _BRCM_SAI_MAX_ACL_TABLES,
                                      _brcm_sai_acl_tables_count_get());
    BRCM_SAI_RV_LVL_CHK(SAI_API_ACL, SAI_LOG_LEVEL_CRITICAL,
                        "freeing acl table state", rv);
    for (e = 1; e < _BRCM_SAI_MAX_ACL_TABLE_GROUPS; e++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                        &e, &data);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
        if (data.acl_tbl_grp.valid && data.acl_tbl_grp.bind_pts &&
            data.acl_tbl_grp.bind_count)
        {
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_ACL_GRP_BIND_POINTS,
                                     e, data.acl_tbl_grp.bind_count,
                                     data.acl_tbl_grp.bind_pts);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "list free acl table group bind points", rv);
            data.acl_tbl_grp.bind_pts = (_brcm_sai_acl_bind_point_t*)(uint64_t)e;
        }
        if (data.acl_tbl_grp.valid && data.acl_tbl_grp.acl_tables &&
            data.acl_tbl_grp.ref_count)
        {
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_ACL_TBL_GRP_MEMBR,
                                     e, data.acl_tbl_grp.ref_count,
                                     data.acl_tbl_grp.acl_tables);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "list free acl table group members", rv);
            data.acl_tbl_grp.acl_tables = (_brcm_sai_acl_tbl_grp_membr_tbl_t*)(uint64_t)e;
        }
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                        &e, &data);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data set", rv);
    }
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_ACL_TBL_GRP, 
                                      1, _BRCM_SAI_MAX_ACL_TABLE_GROUPS,
                                      _brcm_sai_acl_table_groups_count_get());
    BRCM_SAI_RV_LVL_CHK(SAI_API_ACL, SAI_LOG_LEVEL_CRITICAL,
                        "freeing acl table group state", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t 
_brcm_sai_acl_table_entry_attach(int tid, int eid, int uid, bool state,
                                 bool clone, int bind_point_type)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_entry_t *current;
    _brcm_sai_acl_entry_t *prev = NULL, *new_entry;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_table.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_table.entries;
    
    /* See if the object is already in the list. */
    while (NULL != current)
    {
        if (current->id == eid)
        {
            /* Node found */
            return SAI_STATUS_SUCCESS;
        }
        prev = current;
        current = current->next;
    }
    current = prev;
    
    /* Need to add to the list. */
    new_entry = ALLOC_CLEAR(1, sizeof(_brcm_sai_acl_entry_t));
    if (IS_NULL(new_entry))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                               "Error allocating memory for acl entry list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_entry->id = eid;
    new_entry->uid = uid;
    new_entry->state = state;
    if (clone)
    {
        new_entry->bind_mask |= (1 << bind_point_type);
    }
    new_entry->next = NULL;
    if (IS_NULL(current))
    {
        /* 1st object */
        current = new_entry;
        data.acl_table.entries = current;
    }
    else
    {
        current->next = new_entry;
    }
    data.acl_table.ref_count++;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_acl_table_entry_detach(int tid, int eid)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_entry_t *prev, *current;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_table.valid)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_table.entries;
    if (IS_NULL(current) || 0 == data.acl_table.ref_count)
    {        
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Unused acl table.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    do
    {
        if (current->id == eid)
        {
            data.acl_table.ref_count--;
            if (current == data.acl_table.entries)
            {
                data.acl_table.entries = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            CHECK_FREE_SIZE(current);            
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &tid, &data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
            break;
        }
        prev = current;
        current = current->next;
    } while (!IS_NULL(current));
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_acl_table_bind_point_attach(int tid, int type, int val)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_bind_point_t *current;
    _brcm_sai_acl_bind_point_t *prev = NULL, *new_bp;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_table.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_table.bind_pts;
    
    /* See if the object is already in the list. */
    while (NULL != current)
    {
        if (current->type == type && current->val == val)
        {
            /* Node found */
            goto _table_bind_attach_exit;
        }
        prev = current;
        current = current->next;
    }
    current = prev;
    
    /* Need to add to the list. */
    new_bp = ALLOC_CLEAR(1, sizeof(_brcm_sai_acl_bind_point_t));
    if (IS_NULL(new_bp))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                               "Error allocating memory for table acl bind point list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_bp->type = type;    
    new_bp->val = val;
    new_bp->next = NULL;
    if (IS_NULL(current))
    {
        /* 1st object */
        current = new_bp;
        data.acl_table.bind_pts = current;
    }
    else
    {
        current->next = new_bp;
    }
    data.acl_table.bind_count++;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
    
_table_bind_attach_exit:
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_acl_table_bind_point_detach(int tid, int type, int val)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_bind_point_t *current;
    _brcm_sai_acl_bind_point_t *prev = NULL;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_table.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_table.bind_pts;
    if (IS_NULL(current) || 0 == data.acl_table.bind_count)
    {        
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Unused acl table.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    do
    {
        if (current->type == type && current->val == val)
        {
            data.acl_table.bind_count--;
            if (current == data.acl_table.bind_pts)
            {
                data.acl_table.bind_pts = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            CHECK_FREE_SIZE(current);            
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TABLE,
                                            &tid, &data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data set", rv);
            break;
        }
        prev = current;
        current = current->next;
    } while (!IS_NULL(current));
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_acl_table_group_bind_point_attach(int gid, int type, int val)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_bind_point_t *current;
    _brcm_sai_acl_bind_point_t *prev = NULL, *new_bp;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    if (FALSE == data.acl_tbl_grp.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_tbl_grp.bind_pts;
    
    /* See if the object is already in the list. */
    while (NULL != current)
    {
        if (current->type == type && current->val == val)
        {
            /* Node found */
            goto _table_group_bind_attach_exit;
        }
        prev = current;
        current = current->next;
    }
    current = prev;
    
    /* Need to add to the list. */
    new_bp = ALLOC_CLEAR(1, sizeof(_brcm_sai_acl_bind_point_t));
    if (IS_NULL(new_bp))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                         "Error allocating memory for acl table group bind point list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_bp->type = type;    
    new_bp->val = val;
    new_bp->next = NULL;
    if (IS_NULL(current))
    {
        /* 1st object */
        current = new_bp;
        data.acl_tbl_grp.bind_pts = current;
    }
    else
    {
        current->next = new_bp;
    }
    data.acl_tbl_grp.bind_count++;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data set", rv);

_table_group_bind_attach_exit:
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_acl_table_group_bind_point_detach(int gid, int type, int val)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_bind_point_t *current;
    _brcm_sai_acl_bind_point_t *prev = NULL;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    if (FALSE == data.acl_tbl_grp.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_tbl_grp.bind_pts;
    if (IS_NULL(current) || 0 == data.acl_tbl_grp.bind_count)
    {        
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Unused acl table group.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    do
    {
        if (current->type == type && current->val == val)
        {
            data.acl_tbl_grp.bind_count--;
            if (current == data.acl_tbl_grp.bind_pts)
            {
                data.acl_tbl_grp.bind_pts = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            CHECK_FREE_SIZE(current);            
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                            &gid, &data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data set", rv);
            break;
        }
        prev = current;
        current = current->next;
    } while (!IS_NULL(current));
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_acl_table_group_member_attach(int gid, sai_object_id_t val)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_tbl_grp_membr_tbl_t *current;
    _brcm_sai_acl_tbl_grp_membr_tbl_t *prev = NULL, *new_grp_mbr;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    if (FALSE == data.acl_tbl_grp.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_tbl_grp.acl_tables;
    
    /* See if the object is already in the list. */
    while (NULL != current)
    {
        if (current->table == val)
        {
            /* Node found */
            goto _table_group_member_attach_exit;
        }
        prev = current;
        current = current->next;
    }
    current = prev;
    
    /* Need to add table to the list. */
    new_grp_mbr = ALLOC_CLEAR(1, sizeof(_brcm_sai_acl_tbl_grp_membr_tbl_t));
    if (IS_NULL(new_grp_mbr))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_CRITICAL,
                               "Error allocating memory for acl group member table list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_grp_mbr->table = val;
    new_grp_mbr->next = NULL;
    if (IS_NULL(current))
    {
        /* 1st object */
        current = new_grp_mbr;
        data.acl_tbl_grp.acl_tables = current;
    }
    else
    {
        current->next = new_grp_mbr;
    }
    data.acl_tbl_grp.ref_count++;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data set", rv);

_table_group_member_attach_exit:
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_acl_table_group_member_detach(int gid, int val)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_acl_tbl_grp_membr_tbl_t *current;
    _brcm_sai_acl_tbl_grp_membr_tbl_t *prev = NULL;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    if (FALSE == data.acl_tbl_grp.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.acl_tbl_grp.acl_tables;
    if (IS_NULL(current) || 0 == data.acl_tbl_grp.ref_count)
    {        
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Unused acl table group.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    
    do
    {
        if (BRCM_SAI_GET_OBJ_VAL(int, current->table) == val)
        {
            data.acl_tbl_grp.ref_count--;
            if (current == data.acl_tbl_grp.acl_tables)
            {
                data.acl_tbl_grp.acl_tables = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            CHECK_FREE_SIZE(current);            
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                            &gid, &data);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data set", rv);
            break;
        }
        prev = current;
        current = current->next;
    } while (!IS_NULL(current));
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_acl_table_group_bind(sai_object_id_t acl_obj, 
                               bool direction,
                               sai_object_id_t bind_obj,
                               int type) /* Bind point type */
{
    sai_status_t rv;
    bool bound = FALSE;
    int b, gid;
    _brcm_sai_indexed_data_t data;
    
    gid =  BRCM_SAI_GET_OBJ_VAL(int, acl_obj);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Bind acl table group: %d\n", gid);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                    &gid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
    if (FALSE == data.acl_tbl_grp.valid)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table group.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* Check if the this bind point type was specified for this table */
    for (b=0; b<data.acl_tbl_grp.bind_types_count; b++)
    {
        if (type == data.acl_tbl_grp.bind_types[b])
        {
            bound = TRUE;
            break;
        }
    }
    if (!bound)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "Attempting to bind object whose bind type (%d) was "
                         "not created.\n", type);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (data.acl_tbl_grp.ref_count && data.acl_tbl_grp.acl_tables)
    {
        _brcm_sai_acl_tbl_grp_membr_tbl_t *table_grp_mbr;

        table_grp_mbr = data.acl_tbl_grp.acl_tables;
        while (table_grp_mbr)
        {
            rv = _brcm_sai_acl_table_bind(table_grp_mbr->table, direction,
                                          bind_obj, type, FALSE);
            BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table bind", rv);
            table_grp_mbr = table_grp_mbr->next;
        }
    }
    
    return rv;
}

STATIC sai_status_t
_brcm_sai_acl_table_bind(sai_object_id_t acl_obj,
                         bool direction, /* 0: INGRESS, 1: EGRESS */
                         sai_object_id_t bind_obj,
                         int type, /* Bind point type */
                         bool bp_check)
{
    sai_status_t rv;
    bool bound = FALSE;
    bcm_field_qset_t qset;
    int b, tid, val = 0;
    _brcm_sai_acl_entry_t *entry;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);

    tid = BRCM_SAI_GET_OBJ_VAL(int, acl_obj);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
    if (FALSE == data.acl_table.valid)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (TRUE == bp_check)
    {
        /* Check if the this bind point type was specified for this table */
        for (b=0; b<data.acl_table.bind_types_count; b++)
        {
            if (type == data.acl_table.bind_types[b])
            {
                bound = TRUE;
                break;
            }
        }
        if (!bound)
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                             "Attempting to bind object whose bind type (%d) was "
                             "not created.\n", type);
            return SAI_STATUS_INVALID_PARAMETER;
        }
    }
    switch (type)
    {
        case SAI_ACL_BIND_POINT_TYPE_PORT:
            rv = bcm_field_group_get(0, tid, &qset);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field group get", rv);
            if (!((0 == direction && 
                   BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts)) ||
                 (direction && 
                  BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOutPort))))
            {
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                "ACL table not setup for port binding.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            val = BRCM_SAI_GET_OBJ_VAL(int, bind_obj);
            /* Update entries if available */
            if (data.acl_table.ref_count && data.acl_table.entries)
            {
                entry = data.acl_table.entries;
                while (entry)
                {
                    if (direction)
                    {
                        rv = bcm_field_qualify_OutPort(0, entry->id, val,
                                                           _BRCM_SAI_MASK_32);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
                        
                    }
                    else
                    {
                        bcm_pbmp_t pbmp, mpbmp;
                        rv = bcm_field_qualify_InPorts_get(0, entry->id, &pbmp, &mpbmp);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts get", rv);
                        BCM_PBMP_PORT_ADD(pbmp, val);
                        _brcm_sai_switch_pbmp_fp_all_get(&mpbmp);
                        rv = bcm_field_qualify_InPorts(0, entry->id, pbmp, mpbmp);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                    }
                    rv = bcm_field_entry_reinstall(0, entry->id);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                    rv = bcm_field_entry_enable_set(0, entry->id, entry->state);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "acl entry state set", rv);
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                                     "Entry %d updated for port %d.\n", entry->id, val);
                    entry = entry->next;
                }
            }
            break;
        case SAI_ACL_BIND_POINT_TYPE_LAG:
            rv = bcm_field_group_get(0, tid, &qset);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field group get", rv);
            if (!_brcm_sai_field_table_qualify_lag_test(direction, qset))
            {
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                "ACL table not setup for LAG binding.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            val = BRCM_SAI_GET_OBJ_VAL(int, bind_obj);
            /* Update entries if available */
            if (data.acl_table.ref_count && data.acl_table.entries)
            {
                entry = data.acl_table.entries;
                while (entry)
                {
                    if (!direction)
                    {
                        rv = _brcm_sai_field_qualify_ing_lag(entry->id,
                                                             val);
                        BRCM_SAI_RV_CHK(SAI_API_ACL, "field qualify lag", rv);
                    }
                    else
                    {
                        rv = bcm_field_qualify_DstTrunk(0, entry->id,
                                                        val, 0xffff);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify dst trunk", rv);
                    }

                    rv = bcm_field_entry_reinstall(0, entry->id);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                    rv = bcm_field_entry_enable_set(0, entry->id, entry->state);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "acl entry state set", rv);
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                                     "Entry %d updated for LAG %d.\n", entry->id, val);
                    entry = entry->next;
                }
            }
            break;
        case SAI_ACL_BIND_POINT_TYPE_VLAN:
            rv = bcm_field_group_get(0, tid, &qset);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field group get", rv);
            if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOuterVlanId))
            {
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                "ACL table not setup for VLAN binding.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            val = BRCM_SAI_GET_OBJ_VAL(int, bind_obj);
            if (data.acl_table.ref_count && data.acl_table.entries)
            {
                entry = data.acl_table.entries;

                while (entry)
                {
                    rv = bcm_field_qualify_OuterVlanId(0, entry->id, val, _BRCM_SAI_MASK_16);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify outer vlan id", rv);
                    rv = bcm_field_entry_reinstall(0, entry->id);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
                    rv = bcm_field_entry_enable_set(0, entry->id, entry->state);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "acl entry state set", rv);
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                                     "Entry %d updated for vlan %d.\n", entry->id, val);
                    entry = entry->next;
                }
            }
            break;
        case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF:
            return SAI_STATUS_NOT_IMPLEMENTED;
        case SAI_ACL_BIND_POINT_TYPE_SWITCH:
            break;
        default: 
            return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_acl_obj_bind(sai_object_id_t acl_obj,
                       bool direction, /* 0: INGRESS, 1: EGRESS */
                       sai_object_id_t bind_obj)
{
    sai_status_t rv;
    int acl_id, type, val = 0;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);

    acl_id = BRCM_SAI_GET_OBJ_VAL(int, acl_obj);
    val = BRCM_SAI_GET_OBJ_VAL(int, bind_obj);
    type = BRCM_SAI_GET_OBJ_TYPE(bind_obj);
    switch (type)
    {
        case SAI_OBJECT_TYPE_PORT: 
            type = SAI_ACL_BIND_POINT_TYPE_PORT;
            break;
        case SAI_OBJECT_TYPE_LAG: 
            type = SAI_ACL_BIND_POINT_TYPE_LAG;
            break;
        case SAI_OBJECT_TYPE_VLAN: 
            type = SAI_ACL_BIND_POINT_TYPE_VLAN;
            break;
        case SAI_OBJECT_TYPE_ROUTER_INTERFACE: 
            type = SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF;
            break;
        case SAI_OBJECT_TYPE_SWITCH: 
            type = SAI_ACL_BIND_POINT_TYPE_SWITCH;
            break;
        default:
            return SAI_STATUS_FAILURE;
    }
    
    if (SAI_OBJECT_TYPE_ACL_TABLE_GROUP == BRCM_SAI_GET_OBJ_TYPE(acl_obj))
    {
        rv = _brcm_sai_acl_table_group_bind(acl_obj, direction, bind_obj, type);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group bind", rv);
        /* Link bind point to table group state */
        rv = _brcm_sai_acl_table_group_bind_point_attach(acl_id, type, val);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group bind point attach", rv);
    }
    else if (SAI_OBJECT_TYPE_ACL_TABLE == BRCM_SAI_GET_OBJ_TYPE(acl_obj))
    {
        rv = _brcm_sai_acl_table_bind(acl_obj, direction, bind_obj, type, TRUE);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table bind", rv);
        /* Link bind point to table state */
        rv = _brcm_sai_acl_table_bind_point_attach(acl_id, type, val);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table bind point attach", rv);
    }
    else
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl object type.\n");
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_acl_obj_lag_unbind(sai_object_id_t acl_obj,
                             sai_object_id_t bind_obj,
                             bcm_pbmp_t* unbind_pbmp)
{
    sai_status_t rv;
    int tid, gid, val = 0;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    
    if (SAI_OBJECT_TYPE_ACL_TABLE_GROUP == BRCM_SAI_GET_OBJ_TYPE(acl_obj))
    {
        gid =  BRCM_SAI_GET_OBJ_VAL(int, acl_obj);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TBL_GRP,
                                        &gid, &data);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table group data get", rv);
        if (FALSE == data.acl_tbl_grp.valid)
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table group.\n");
            return SAI_STATUS_INVALID_PARAMETER;
        }
        val = BRCM_SAI_GET_OBJ_VAL(int, bind_obj);
        if (data.acl_tbl_grp.ref_count && data.acl_tbl_grp.acl_tables)
        {
            _brcm_sai_acl_tbl_grp_membr_tbl_t *table_grp_mbr;

            table_grp_mbr = data.acl_tbl_grp.acl_tables;
            while (table_grp_mbr)
            {
                tid = BRCM_SAI_GET_OBJ_VAL(int, table_grp_mbr->table);
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                                &tid, &data);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
                if (FALSE == data.acl_table.valid)
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                rv = _brcm_sai_field_qualify_ing_lag_remove(&data.acl_table,
                                                            unbind_pbmp,
                                                            val);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "field qualify lag remove", rv);
                table_grp_mbr = table_grp_mbr->next;
            }
        }
    }
    else if (SAI_OBJECT_TYPE_ACL_TABLE == BRCM_SAI_GET_OBJ_TYPE(acl_obj))
    {
        tid = BRCM_SAI_GET_OBJ_VAL(int, acl_obj);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_ACL_TABLE,
                                        &tid, &data);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "acl table data get", rv);
        if (FALSE == data.acl_table.valid)
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl table.\n");
            return SAI_STATUS_INVALID_PARAMETER;
        }         
        val = BRCM_SAI_GET_OBJ_VAL(int, bind_obj);
        rv = _brcm_sai_field_qualify_ing_lag_remove(&data.acl_table,
                                                    unbind_pbmp,
                                                    val);
        BRCM_SAI_RV_CHK(SAI_API_ACL, "field qualify lag remove", rv);
    }
    else
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid acl object type.\n");
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);

    return SAI_STATUS_SUCCESS;
}



/*
* Routine Description:
*   Create an ACL table
*
* Arguments:
 *  [out] acl_table_id - the the acl table id
 *  [in] attr_count - number of attributes
 *  [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
sai_status_t
_brcm_sai_create_acl_table(_Out_ sai_object_id_t* acl_table_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list,
                           int *_stage, int *bcount, int32_t **blist)
{
    sai_status_t rv;
    int i, stage = -1;
    bcm_field_qset_t qset;
    bcm_field_group_t group;
    bool bind = FALSE, got_qual = FALSE, dscp = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(acl_table_id);

    /* Initialize a qualifier set */
    BCM_FIELD_QSET_INIT(qset);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_TABLE_ATTR_ACL_STAGE:
                if (SAI_ACL_STAGE_INGRESS == attr_list[i].value.u32)
                {
                    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyStageIngress);
                }
                else if (SAI_ACL_STAGE_EGRESS == attr_list[i].value.u32)
                {
                    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyStageEgress);
                }
                else
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Table stage not valid.\n");
                    return SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                stage = attr_list[i].value.u32;
                break;
            default: break;
        }
        if (-1 != stage)
        {
            break;
        }
    }
    if (-1 == stage)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "No stage info available.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_TABLE_ATTR_ACL_STAGE:
            case SAI_ACL_TABLE_ATTR_SIZE: /* FIXME: to be implemented */
                break;
            case SAI_ACL_TABLE_ATTR_ACL_BIND_POINT_TYPE_LIST:
            {
                int b;
                
                *bcount = attr_list[i].value.s32list.count;
                if (*bcount)
                {
                    *blist = attr_list[i].value.s32list.list;
                
                    for (b=0; b<attr_list[i].value.s32list.count; b++)
                    {
                        switch (attr_list[i].value.s32list.list[b])
                        {
                            case SAI_ACL_BIND_POINT_TYPE_PORT:
                                if (SAI_ACL_STAGE_INGRESS == stage)
                                {
                                    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPorts);
                                }
                                else
                                {
                                    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOutPort);
                                }
                                got_qual = bind = TRUE;
                                break;
                            case SAI_ACL_BIND_POINT_TYPE_LAG:
                                if (SAI_ACL_STAGE_INGRESS == stage)
                                {
                                    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPorts);
#ifdef SAI_FIXUP_DISABLE_DEST_TRUNK_QUALIFIER
                                    got_qual = bind = TRUE;
#endif
                                }
#ifndef SAI_FIXUP_DISABLE_DEST_TRUNK_QUALIFIER
                                else
                                {
                                    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstTrunk);
                                }
                                got_qual = bind = TRUE;
#endif
                                break;
                            case SAI_ACL_BIND_POINT_TYPE_VLAN:
                                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOuterVlanId);
                                got_qual = bind = TRUE;
                                break;
                            case SAI_ACL_BIND_POINT_TYPE_ROUTER_INTF:
                                return SAI_STATUS_NOT_IMPLEMENTED;
                            case SAI_ACL_BIND_POINT_TYPE_SWITCH:
                                bind = TRUE;
                                break;
                            default:
                                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                                 "Invalid bind point.\n");
                                return SAI_STATUS_INVALID_ATTR_VALUE_0 + i;
                        }
                    }
                }
                break;
            }
            /* Qualifier sets */
            case SAI_ACL_TABLE_ATTR_FIELD_SRC_IPV6:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifySrcIp6);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DST_IPV6:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstIp6);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_SRC_MAC:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifySrcMac);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DST_MAC:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstMac);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_SRC_IP:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifySrcIp);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DST_IP:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstIp);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_IN_PORTS:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPorts);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUT_PORTS:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "OUT PORTS not supported on this platform.\n");
                return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            case SAI_ACL_TABLE_ATTR_FIELD_IN_PORT:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPort);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUT_PORT:
                BCM_FIELD_QSET_ADD(qset, SAI_ACL_STAGE_INGRESS == stage ?  
                                       bcmFieldQualifyDstPort :
                                       bcmFieldQualifyOutPort);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_ID:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOuterVlanId);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_PRI:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOuterVlanPri);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_OUTER_VLAN_CFI:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOuterVlanCfi);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_INNER_VLAN_ID:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInnerVlanId);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_INNER_VLAN_PRI:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInnerVlanPri);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_INNER_VLAN_CFI:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInnerVlanCfi);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_L4_SRC_PORT:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyL4SrcPort);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_L4_DST_PORT:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyL4DstPort);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ETHER_TYPE:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyEtherType);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_IP_PROTOCOL:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIpProtocol);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_DSCP:
                dscp = true; got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ECN:
                dscp = true; got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TTL:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyTtl);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TOS:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyTos);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_IP_FLAGS:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIpFlags);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TCP_FLAGS:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyTcpControl);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_TYPE:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIpType);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ACL_IP_FRAG:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIpFrag);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_TC:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIntPriority);
                got_qual = true;
                break;
            case SAI_ACL_TABLE_ATTR_FIELD_ACL_RANGE_TYPE:
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyRangeCheck);
                got_qual = true;
                break;
            default: 
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown acl table attribute %d passed\n",
                                 attr_list[i].id);
                return SAI_STATUS_INVALID_ATTRIBUTE_0 + i;
        }
    }
    if (FALSE == got_qual)
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "No qualifier info available.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    else if (DEV_IS_THX() || DEV_IS_TD3())
    {
        if (SAI_ACL_STAGE_INGRESS == stage)
        {
            if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts))
            {
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPorts);
            }
        }
        else
        {
            if (!BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyOutPort))
            {
                BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyOutPort);
            }
        }
    }
    if (dscp)
    {
        BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDSCP);
    }

    /* Initialize a group with this set */
    rv = bcm_field_group_create(0, qset, BCM_FIELD_GROUP_PRIO_ANY, &group);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field group create", rv);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Using table id: %d\n", group);
    *acl_table_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_ACL_TABLE, group);
    *_stage = stage;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

/*
* Routine Description:
*   Create an ACL entry
*
* Arguments:
*   [out] acl_entry_id - the acl entry id
*   [in] attr_count - number of attributes
*   [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
sai_status_t
_brcm_sai_create_acl_entry(_Out_ sai_object_id_t *acl_entry_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list,
                           bcm_field_entry_t entry, bool ingress,
                           sai_packet_action_t gpa, sai_packet_action_t ypa,
                           sai_packet_action_t rpa, bool *state)
{
    int i;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bool dscp = false;
    uint8_t dscp_val = 0, dscp_mask = 0;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ACL);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ACL_ENTRY_ATTR_TABLE_ID:
                break;
            case SAI_ACL_ENTRY_ATTR_PRIORITY:
                rv = bcm_field_entry_prio_set(0, entry,
                                                  attr_list[i].value.u32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "entry priority set", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ADMIN_STATE:
                *state = attr_list[i].value.booldata;
                break;
            /* Qualifier values */
            case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IPV6:
            {
                bcm_ip6_t ip6;
                bcm_ip6_t ip6_mask;

                memcpy(ip6, BRCM_SAI_ATTR_ACL_FLD_IP6(i),
                       sizeof(bcm_ip6_t));
                memcpy(ip6_mask, BRCM_SAI_ATTR_ACL_FLD_MASK_IP6(i),
                       sizeof(bcm_ip6_t));
                rv = bcm_field_qualify_SrcIp6(0, entry, ip6, ip6_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify SrcIp6", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_DST_IPV6:
            {
                bcm_ip6_t ip6;
                bcm_ip6_t ip6_mask;

                memcpy(ip6, BRCM_SAI_ATTR_ACL_FLD_IP6(i),
                       sizeof(bcm_ip6_t));
                memcpy(ip6_mask, BRCM_SAI_ATTR_ACL_FLD_MASK_IP6(i),
                       sizeof(bcm_ip6_t));
                rv = bcm_field_qualify_DstIp6(0, entry, ip6, ip6_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstIp6", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_SRC_MAC:
            {
                bcm_mac_t mac;
                bcm_mac_t mac_mask;

                memcpy(mac, BRCM_SAI_ATTR_ACL_FLD_MAC(i),
                       sizeof(bcm_mac_t));
                memcpy(mac_mask, BRCM_SAI_ATTR_ACL_FLD_MASK_MAC(i),
                       sizeof(bcm_mac_t));
                rv = bcm_field_qualify_SrcMac(0, entry, mac, mac_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify SrcMac", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_DST_MAC:
            {
                bcm_mac_t mac;
                bcm_mac_t mac_mask;

                memcpy(mac, BRCM_SAI_ATTR_ACL_FLD_MAC(i),
                       sizeof(bcm_mac_t));
                memcpy(mac_mask, BRCM_SAI_ATTR_ACL_FLD_MASK_MAC(i),
                       sizeof(bcm_mac_t));
                rv = bcm_field_qualify_DstMac(0, entry, mac, mac_mask);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstMac", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_SRC_IP:
                rv = bcm_field_qualify_SrcIp(0, entry,
                         ntohl(BRCM_SAI_ATTR_ACL_FLD_IP4(i)),
                         ntohl(BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i)));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify SrcIp", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_DST_IP:
                rv = bcm_field_qualify_DstIp(0, entry,
                         ntohl(BRCM_SAI_ATTR_ACL_FLD_IP4(i)),
                         ntohl(BRCM_SAI_ATTR_ACL_FLD_MASK_IP4(i)));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstIp", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_IN_PORTS:
            {
                int p;
                bcm_pbmp_t pbmp, mpbmp;

                BCM_PBMP_CLEAR(pbmp);
                for (p = 0; p<BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i); p++)
                {
                    BCM_PBMP_PORT_ADD(pbmp, BRCM_SAI_ATTR_ACL_FLD_OBJ_LIST_VAL(int, i, p));
                }
                if (BCM_PBMP_IS_NULL(pbmp))
                {
                    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "No ports in list\n");
                    return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
                }
                _brcm_sai_switch_pbmp_fp_all_get(&mpbmp);
                rv = bcm_field_qualify_InPorts(0, entry, pbmp, mpbmp);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORTS:
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "OUT PORTS not supported on this platform.\n");
                return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            case SAI_ACL_ENTRY_ATTR_FIELD_IN_PORT:
                rv = bcm_field_qualify_InPort(0, entry,
                         BRCM_SAI_GET_OBJ_VAL(int, BRCM_SAI_ATTR_ACL_FLD_OBJ(i)),
                         _BRCM_SAI_MASK_32);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPort", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_OUT_PORT:
                if (ingress)
                {
                    int port = BRCM_SAI_GET_OBJ_VAL(int, 
                                                    BRCM_SAI_ATTR_ACL_FLD_OBJ(i));
                    rv = bcm_field_qualify_DstPort(0, entry, 0, 0xff, port,
                                                       _BRCM_SAI_MASK_32);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstPort", rv);
                }
                else
                {
                    rv = bcm_field_qualify_OutPort(0,
                             entry, BRCM_SAI_GET_OBJ_VAL(int,
                             BRCM_SAI_ATTR_ACL_FLD_OBJ(i)),
                             _BRCM_SAI_MASK_32);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
                }
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_ID:
                rv = bcm_field_qualify_OuterVlanId(0, entry, 
                         BRCM_SAI_ATTR_ACL_FLD_16(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_PRI:
                rv = bcm_field_qualify_OuterVlanPri(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanPri", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_OUTER_VLAN_CFI:
                rv = bcm_field_qualify_OuterVlanCfi(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanCfi", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_INNER_VLAN_ID:
                rv = bcm_field_qualify_InnerVlanId(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_16(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InnerVlanId", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_INNER_VLAN_PRI:
                rv = bcm_field_qualify_InnerVlanPri(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InnerVlanPri", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_INNER_VLAN_CFI:
                rv = bcm_field_qualify_InnerVlanCfi(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InnerVlanCfi", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_L4_SRC_PORT:
                rv = bcm_field_qualify_L4SrcPort(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_16(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify L4SrcPort", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_L4_DST_PORT:
                rv = bcm_field_qualify_L4DstPort(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_16(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify L4DstPort", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_ETHER_TYPE:
                rv = bcm_field_qualify_EtherType(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_16(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_16(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_IP_PROTOCOL:
                rv = bcm_field_qualify_IpProtocol(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpProtocol", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_DSCP:
                dscp_val |= BRCM_SAI_ATTR_ACL_FLD_8(i) << 2;
                dscp_mask |= BRCM_SAI_ATTR_ACL_FLD_MASK_8(i) << 2;
                dscp = TRUE;
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_ECN:
                dscp_val |= BRCM_SAI_ATTR_ACL_FLD_8(i);
                dscp_mask |= BRCM_SAI_ATTR_ACL_FLD_MASK_8(i);
                dscp = TRUE;
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_TTL:
                rv = bcm_field_qualify_Ttl(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify Ttl", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_TOS:
                rv = bcm_field_qualify_Tos(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify Tos", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_IP_FLAGS:
                rv = bcm_field_qualify_IpFlags(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpFlags", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_TCP_FLAGS:
                rv = bcm_field_qualify_TcpControl(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify TcpControl", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_TYPE:
                rv = bcm_field_qualify_IpType(0, entry, 
                     BRCM_IPTYPE_SAI_TO_BCM(BRCM_SAI_ATTR_ACL_FLD_32(i)));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpType", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_ACL_IP_FRAG:
                rv = bcm_field_qualify_IpFrag(0, entry,
                     BRCM_IPFRAG_SAI_TO_BCM(BRCM_SAI_ATTR_ACL_FLD_32(i)));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IpFrag", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_TC:
                rv = bcm_field_qualify_IntPriority(0, entry,
                         BRCM_SAI_ATTR_ACL_FLD_8(i),
                         BRCM_SAI_ATTR_ACL_FLD_MASK_8(i));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify IntPriority", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_FIELD_ACL_RANGE_TYPE:
            {
                int r;

                for (r = 0; r<BRCM_SAI_ATTR_ACL_FLD_OBJ_COUNT(i); r++)
                {
                    rv = bcm_field_qualify_RangeCheck(0, entry, 
                             BRCM_SAI_ATTR_ACL_FLD_OBJ_LIST_VAL(bcm_field_range_t, i, r), 0);
                    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify RangeCheck", rv);
                }
                break;
            }
            
            /* Actions */
            case SAI_ACL_ENTRY_ATTR_ACTION_REDIRECT:
                 switch (BRCM_SAI_GET_OBJ_TYPE(BRCM_SAI_ATTR_ACL_ACTION_OBJ(i)))
                 {
                     case SAI_OBJECT_TYPE_PORT:
                     {
                         bcm_port_t port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, BRCM_SAI_ATTR_ACL_ACTION_OBJ(i));
                         rv = bcm_field_action_add(0, entry, bcmFieldActionRedirectPort, 0, port);
                         BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                         break;
                     }
                     case SAI_OBJECT_TYPE_LAG:
                     {
                         bcm_trunk_t lag = BRCM_SAI_GET_OBJ_VAL(bcm_trunk_t, BRCM_SAI_ATTR_ACL_ACTION_OBJ(i));
                         rv = bcm_field_action_add(0, entry, bcmFieldActionRedirectTrunk, 0, lag);
                         BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                         break;
                     }
                     case SAI_OBJECT_TYPE_NEXT_HOP:
                     {
                         _brcm_sai_indexed_data_t data;
                         bcm_if_t nhid = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, BRCM_SAI_ATTR_ACL_ACTION_OBJ(i));
                         rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                                         (int *) &nhid, &data);
                         BRCM_SAI_RV_CHK(SAI_API_ACL, "nh info data get", rv);
                         BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                                          "Redirect to nexthop index: %d, obj: %d\n", nhid, data.nh_info.if_id);
                         nhid = data.nh_info.if_id;
                         rv = bcm_field_action_add(0, entry, bcmFieldActionRedirectEgrNextHop, nhid, 0);
                         BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                         break;
                     }
                     case SAI_OBJECT_TYPE_NEXT_HOP_GROUP:
                     {
                         bcm_if_t nhid = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, BRCM_SAI_ATTR_ACL_ACTION_OBJ(i));
                         BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Redirect to nexthop group: %d\n", nhid);
                         rv = bcm_field_action_add(0, entry, bcmFieldActionRedirectEgrNextHop, nhid, 0);
                         BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                         break;
                     }
                     default:
                         BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Invalid redirect target type.\n");
                         return SAI_STATUS_INVALID_PARAMETER;
                 }
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_PACKET_ACTION:
                switch (BRCM_SAI_ATTR_ACL_ACTION_32(i))
                {
                    case SAI_PACKET_ACTION_DROP:
                        rv = bcm_field_action_add(0, entry,
                                 bcmFieldActionDrop, 0, 0);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                        break;
                    case SAI_PACKET_ACTION_FORWARD:
                        rv = bcm_field_action_add(0, entry,
                                 bcmFieldActionDropCancel, 0, 0);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                        break;
                    case SAI_PACKET_ACTION_TRAP:
                        rv = bcm_field_action_add(0, entry,
                                 bcmFieldActionCopyToCpu, 0, 0);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                        rv = bcm_field_action_add(0, entry,
                                 bcmFieldActionDrop, 0, 0);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                        if (_brcm_sai_cpu_pool_config)
                        {
                            /* CPU packets need to be accounted in CPU PG */
                            rv = bcm_field_action_add(0, entry, 
                                                      bcmFieldActionPrioIntNew, 
                                                      _brcm_sai_cpu_pg_id, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);   
                        }
                        break;
                    case SAI_PACKET_ACTION_LOG:
                        rv = bcm_field_action_add(0, entry,
                                 bcmFieldActionCopyToCpu, 0, 0);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                        rv = bcm_field_action_add(0, entry,
                                 bcmFieldActionDropCancel, 0, 0);
                        BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                        break;
                    default: break;
                }
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_FLOOD:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionDoNotLearn, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_COUNTER:
                rv = bcm_field_entry_stat_attach(0, entry,
                         BRCM_SAI_GET_OBJ_VAL(int, BRCM_SAI_ATTR_ACL_ACTION_OBJ(i)));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry stat attach", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_INGRESS:
            {
                int idx;

                if (1 < BRCM_SAI_ATTR_ACL_ACTION_OBJ_COUNT(i))
                {
                    return SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Entry %d action mirror 0x%x.\n", 
                                 entry, BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST_VAL(uint32_t, i, 0));
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionMirrorIngress, 0,
                         BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST_VAL(uint32_t, i, 0));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                idx = (BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST_VAL(uint32_t, i, 0)) & 0xff;
                rv = _brcm_sai_mirror_ref_update(0, idx, TRUE);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "mirror session ref add", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_MIRROR_EGRESS:
            {
                int idx;

                if (1 < BRCM_SAI_ATTR_ACL_ACTION_OBJ_COUNT(i))
                {
                    return SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG, "Entry %d action mirror 0x%x.\n", 
                                 entry, BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST_VAL(uint32_t, i, 0));
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionMirrorEgress, 0,
                         BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST_VAL(uint32_t, i, 0));
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                idx = (BRCM_SAI_ATTR_ACL_ACTION_OBJ_LIST_VAL(uint32_t, i, 0)) & 0xff;
                rv = _brcm_sai_mirror_ref_update(0, idx, TRUE);
                BRCM_SAI_RV_CHK(SAI_API_ACL, "mirror session ref add", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_POLICER:
            {
                uint8_t act = BRCM_SAI_GET_OBJ_SUB_TYPE(BRCM_SAI_ATTR_ACL_ACTION_OBJ(i));

                if (act & 0x1)
                {
                    switch (gpa)
                    {
                        case SAI_PACKET_ACTION_DROP:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionGpDrop, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        case SAI_PACKET_ACTION_FORWARD:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionGpDropCancel, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        case SAI_PACKET_ACTION_TRAP:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionGpCopyToCpu, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionGpDrop, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            if (_brcm_sai_cpu_pool_config)
                            {
                                /* CPU packets need to be accounted in CPU PG */
                                rv = bcm_field_action_add(0, entry, 
                                                          bcmFieldActionGpPrioIntNew, 
                                                          _brcm_sai_cpu_pg_id, 0);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);   
                            }
                            break;
                        case SAI_PACKET_ACTION_LOG:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionGpCopyToCpu, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionGpDropCancel, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        default: break;
                    }
                }
                if (act & 0x2)
                {
                    switch (ypa)
                    {
                        case SAI_PACKET_ACTION_DROP:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionYpDrop, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        case SAI_PACKET_ACTION_FORWARD:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionYpDropCancel, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        case SAI_PACKET_ACTION_TRAP:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionYpCopyToCpu, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionYpDrop, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            if (_brcm_sai_cpu_pool_config)
                            {
                                /* CPU packets need to be accounted in CPU PG */
                                rv = bcm_field_action_add(0, entry, 
                                                          bcmFieldActionYpPrioIntNew, 
                                                          _brcm_sai_cpu_pg_id, 0);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);   
                            }
                            break;
                        case SAI_PACKET_ACTION_LOG:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionYpCopyToCpu, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionYpDropCancel, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        default: break;
                    }
                }
                if (act & 0x4)
                {
                    switch (rpa)
                    {
                        case SAI_PACKET_ACTION_DROP:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionRpDrop, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        case SAI_PACKET_ACTION_FORWARD:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionRpDropCancel, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        case SAI_PACKET_ACTION_TRAP:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionRpCopyToCpu, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionRpDrop, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            if (_brcm_sai_cpu_pool_config)
                            {
                                /* CPU packets need to be accounted in CPU PG */
                                rv = bcm_field_action_add(0, entry, 
                                                          bcmFieldActionRpPrioIntNew, 
                                                          _brcm_sai_cpu_pg_id, 0);
                                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);   
                            }
                            break;
                        case SAI_PACKET_ACTION_LOG:
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionRpCopyToCpu, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            rv = bcm_field_action_add(0, entry,
                                     bcmFieldActionRpDropCancel, 0, 0);
                            BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                            break;
                        default: break;
                    }
                }
                rv = bcm_field_entry_policer_attach(0, entry, 0,
                         BRCM_SAI_GET_OBJ_VAL(uint32, BRCM_SAI_ATTR_ACL_ACTION_OBJ(i))); 
                BRCM_SAI_API_CHK(SAI_API_ACL, "field entry policer attach", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_DECREMENT_TTL:
                /* FIXME: bcmFieldActionTtlSet ? */
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_TC:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionPrioIntNew,
                         BRCM_SAI_ATTR_ACL_ACTION_8(i), 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_PACKET_COLOR:
            {
                uint32_t color;

                switch (BRCM_SAI_ATTR_ACL_ACTION_32(i))
                {
                    case SAI_PACKET_COLOR_GREEN: 
                        color = BCM_FIELD_COLOR_GREEN;
                        break;
                    case SAI_PACKET_COLOR_YELLOW:
                        color = BCM_FIELD_COLOR_YELLOW;
                        break;
                    case SAI_PACKET_COLOR_RED:
                        color = BCM_FIELD_COLOR_RED;
                        break;
                    default:
                        return SAI_STATUS_INVALID_PARAMETER;
                }
                rv = bcm_field_action_add(0, entry, 
                                              bcmFieldActionDropPrecedence,
                                              color, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_INNER_VLAN_ID:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionInnerVlanNew,
                         BRCM_SAI_ATTR_ACL_ACTION_16(i), 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_INNER_VLAN_PRI:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionInnerVlanPrioNew,
                         BRCM_SAI_ATTR_ACL_ACTION_8(i), 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_OUTER_VLAN_ID:
            {
                bcm_if_t intf1;
                bcm_l3_intf_t l3intf;
                bcm_l3_egress_t l3egress;
                sai_mac_t mac;
                bcm_l3_intf_t_init(&l3intf);
                l3intf.l3a_vid = BRCM_SAI_ATTR_ACL_ACTION_16(i);
                rv = _brcm_sai_system_mac_get(&mac);
                BRCM_SAI_RV_CHK(SAI_API_ACL,"System MAC get", rv);
                sal_memcpy(l3intf.l3a_mac_addr, mac, sizeof(bcm_mac_t));
                rv = bcm_l3_intf_create(0,&l3intf);
                BRCM_SAI_RV_CHK(SAI_API_ACL,"L3 intf for VlanNew", rv);

                bcm_l3_egress_t_init(&l3egress);
                l3egress.intf = l3intf.l3a_intf_id;
                l3egress.vlan =  l3intf.l3a_vid;
                l3egress.flags2 = BCM_L3_FLAGS2_FIELD_ONLY;
                l3egress.flags = BCM_L3_KEEP_SRCMAC | BCM_L3_KEEP_DSTMAC | BCM_L3_KEEP_TTL;
                rv =  bcm_l3_egress_create(0,0,&l3egress,&intf1);
                BRCM_SAI_API_CHK(SAI_API_ACL,"L3 egress for VlanNew", rv);
                rv = bcm_field_action_add(0, entry, bcmFieldActionChangeL2Fields, 
                                          intf1, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_OUTER_VLAN_PRI:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionOuterVlanPrioNew,
                         BRCM_SAI_ATTR_ACL_ACTION_8(i), 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_SRC_MAC:
            {
                bcm_if_t intf1;
                bcm_l3_intf_t l3intf;
                bcm_l3_egress_t l3egress;                
                bcm_l3_intf_t_init(&l3intf);
                sal_memcpy(l3intf.l3a_mac_addr, 
                           BRCM_SAI_ATTR_ACL_ACTION_MAC(i),
                           sizeof(bcm_mac_t));
                l3intf.l3a_vid = 1;
                rv = bcm_l3_intf_create(0,&l3intf);
                BRCM_SAI_RV_CHK(SAI_API_ACL,"L3 intf create for SrcMacNew", rv);

                bcm_l3_egress_t_init(&l3egress);
                l3egress.intf = l3intf.l3a_intf_id;
                l3egress.flags2 = BCM_L3_FLAGS2_FIELD_ONLY;
                l3egress.flags =  BCM_L3_KEEP_VLAN | BCM_L3_KEEP_DSTMAC | BCM_L3_KEEP_TTL;
                
                rv =  bcm_l3_egress_create(0,0,&l3egress,&intf1);
                BRCM_SAI_RV_CHK(SAI_API_ACL,"L3 egress create for SrcMacNew", rv);
                rv = bcm_field_action_add(0, entry, bcmFieldActionChangeL2Fields, 
                                          intf1, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DST_MAC:
            {
                bcm_if_t intf1;
                bcm_l3_intf_t l3intf;
                sai_mac_t mac;
                bcm_l3_egress_t l3egress;                
                bcm_l3_intf_t_init(&l3intf);
                l3intf.l3a_vid = 1;
                rv = _brcm_sai_system_mac_get(&mac);
                BRCM_SAI_RV_CHK(SAI_API_ACL,"System MAC get", rv);
                sal_memcpy(l3intf.l3a_mac_addr, mac, sizeof(bcm_mac_t));
                rv = bcm_l3_intf_create(0,&l3intf);
                BRCM_SAI_RV_CHK(SAI_API_ACL,"L3 intf for DstMacNew", rv);

                bcm_l3_egress_t_init(&l3egress);
                l3egress.intf = l3intf.l3a_intf_id;
                sal_memcpy(l3egress.mac_addr, 
                           BRCM_SAI_ATTR_ACL_ACTION_MAC(i),
                           sizeof(bcm_mac_t));
                l3egress.flags2 = BCM_L3_FLAGS2_FIELD_ONLY;
                /*  ttl should not be incrementing in l3? */
                l3egress.flags =  BCM_L3_KEEP_VLAN | BCM_L3_KEEP_SRCMAC | BCM_L3_KEEP_TTL;
                
                rv =  bcm_l3_egress_create(0,0,&l3egress,&intf1);
                BRCM_SAI_RV_CHK(SAI_API_ACL,"L3 egress for DstMacNew", rv);
                rv = bcm_field_action_add(0, entry, bcmFieldActionChangeL2Fields, 
                                          intf1, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            }
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_SRC_IP:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DST_IP:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_SRC_IPV6:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DST_IPV6:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_DSCP:
                rv = bcm_field_action_add(0, entry,
                                              bcmFieldActionDscpNew,
                                              BRCM_SAI_ATTR_ACL_ACTION_8(i), 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_ECN:
                rv = bcm_field_action_add(0, entry,
                                              bcmFieldActionEcnNew,
                                              BRCM_SAI_ATTR_ACL_ACTION_8(i), 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_L4_SRC_PORT:
            case SAI_ACL_ENTRY_ATTR_ACTION_SET_L4_DST_PORT:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            default:
                rv = SAI_STATUS_INVALID_PARAMETER;
                BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                                 "Unknown acl entry attribute %d passed\n",
                                 attr_list[i].id);
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR, "Error processing acl attributes\n");
            return rv;
        }
    }
    if (dscp)
    {
        rv = bcm_field_qualify_DSCP(0, entry, dscp_val, dscp_mask);
        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DSCP", rv);
    }
    rv = bcm_field_entry_install(0, entry);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry install", rv);
    /* Disable to begin with. Enabled after processing bindings */
    rv = bcm_field_entry_enable_set(0, entry, 0);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry enable set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ACL);
    return rv;
}

bool
_brcm_sai_field_table_qualify_lag_test(bool direction, bcm_field_qset_t qset)
{
#ifdef SAI_FIXUP_DISABLE_DEST_TRUNK_QUALIFIER
    return ((!direction &&
             BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts)));
#else
    return ((!direction && 
             BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyInPorts)) ||
            (direction && 
             BCM_FIELD_QSET_TEST(qset, bcmFieldQualifyDstTrunk)));
#endif
}

/* 
 * This is used for removing LAG members from ACL entries 
 * in an ACL Table.
 */
sai_status_t
_brcm_sai_field_qualify_ing_lag_remove(_brcm_sai_acl_table_t *acl_table,
                                       bcm_pbmp_t *unbind_pbmp,
                                       int lag)
{
    sai_status_t rv;
    _brcm_sai_acl_entry_t *entry;
    bcm_field_qset_t qset;
#ifdef SAI_DEBUG
    char pbmp_str[256];
#endif

    rv = bcm_field_group_get(0, acl_table->idx, &qset);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field group get", rv);
    if (!_brcm_sai_field_table_qualify_lag_test(INGRESS, qset))
    {
        BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_ERROR,
                         "ACL table not setup for LAG binding.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* Update entries if available */
    if (acl_table->ref_count && acl_table->entries)
    {
        entry = acl_table->entries;
        while (entry)
        {
            bcm_pbmp_t pbmp, mpbmp;
            BCM_PBMP_CLEAR(pbmp);
            BCM_PBMP_CLEAR(mpbmp);
            rv = bcm_field_qualify_InPorts_get(0, entry->id, &pbmp, &mpbmp);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify InPorts get", rv);
#ifdef SAI_DEBUG
            format_pbmp(0, (char*)&pbmp_str, 255, pbmp);
            ACL_DEBUG("Old bmp %s", pbmp_str);
#endif
            /* remove from pbmp, bits set in new_pbmp */
            BCM_PBMP_REMOVE(pbmp, (*unbind_pbmp));

#ifdef SAI_DEBUG           
            format_pbmp(0, (char*)&pbmp_str, 255, pbmp);
            ACL_DEBUG("New bmp %s", pbmp_str);
#endif
            rv = bcm_field_qualify_InPorts(0, entry->id, pbmp, mpbmp);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify inports", rv);
            rv = bcm_field_entry_reinstall(0, entry->id);
            BRCM_SAI_API_CHK(SAI_API_ACL, "field entry reinstall", rv);
            rv = bcm_field_entry_enable_set(0, entry->id, entry->state);
            BRCM_SAI_API_CHK(SAI_API_ACL, "acl entry state set", rv);

            ACL_DEBUG("Entry %d updated for LAG %d.\n", entry->id, lag);
            entry = entry->next;
        }
    }
    return rv;
}

/* 
   This is only used for adding a new lag pbmp into the existing
   ACL 
*/
sai_status_t
_brcm_sai_field_qualify_ing_lag(bcm_field_entry_t entry,
                                int lag)
{
    sai_status_t rv;
    bcm_pbmp_t lag_pbmp;
    bcm_pbmp_t pbmp, mpbmp;
#ifdef SAI_DEBUG
    char pbmp_str[256];
#endif
    
    BCM_PBMP_CLEAR(pbmp);
    BCM_PBMP_CLEAR(mpbmp);
    rv = _brcm_sai_lag_member_pbmp_get(lag, &lag_pbmp);        
    BRCM_SAI_RV_CHK(SAI_API_ACL, "lag member info get", rv); 
    /* Get the existing pbmp to merge with if any */
    rv = bcm_field_qualify_InPorts_get(0, entry, &pbmp, &mpbmp);    
    _brcm_sai_switch_pbmp_get(&mpbmp);
    if (rv == BCM_E_NONE)
    {
#ifdef SAI_DEBUG       
        format_pbmp(0, (char*)&pbmp_str, 255, pbmp);
        ACL_DEBUG("Old bmp %s", pbmp_str);
#endif
        BCM_PBMP_OR(lag_pbmp, pbmp);
    }    
    rv = bcm_field_qualify_InPorts(0, entry, lag_pbmp, mpbmp);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify inports", rv);
#ifdef SAI_DEBUG
    format_pbmp(0, (char*)&pbmp_str, 255, lag_pbmp);
    ACL_DEBUG("New bmp %s", pbmp_str);
#endif
    return rv;
}


sai_status_t
_brcm_sai_field_entry_copy(int unit, int table_id, bcm_field_entry_t u_entry,
                           int bind_point_type, int val, bool ingress,
                           bool state)
{
    sai_status_t rv;
    //bcm_policer_t pol_id;
    bcm_field_entry_t l_entry;

    rv = bcm_field_entry_copy(0, u_entry, &l_entry);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry copy", rv);
    if (SAI_ACL_BIND_POINT_TYPE_PORT == bind_point_type)
    {
        rv = bcm_field_qualify_OutPort(0, l_entry, val,
                                       _BRCM_SAI_MASK_32);
        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OutPort", rv);
    }
    else if (SAI_ACL_BIND_POINT_TYPE_LAG == bind_point_type)
    {
        if (ingress)
        {
            return SAI_STATUS_FAILURE;
        }
        /* only copy on egress */
        rv = bcm_field_qualify_DstTrunk(0, l_entry, val,
                                        _BRCM_SAI_MASK_16);
        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify DstTrunk", rv);
    }
    else if (SAI_ACL_BIND_POINT_TYPE_VLAN == bind_point_type)
    {
        rv = bcm_field_qualify_OuterVlanId(0, l_entry, val,
                                           _BRCM_SAI_MASK_16);
        BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify OuterVlanId", rv);
    }
#if 0 /* Not required with SDK 6.5.10+ */
    rv = bcm_field_entry_policer_get(unit, u_entry, 0, &pol_id);
    if (BCM_SUCCESS(rv))
    {
        rv = bcm_field_entry_policer_attach(0, l_entry, 0, pol_id);
        BRCM_SAI_API_CHK(SAI_API_ACL, "field entry policer attach", rv);
    }
#endif
    rv = bcm_field_entry_install(0, l_entry);
    BRCM_SAI_API_CHK(SAI_API_ACL, "field entry install", rv);
    rv = _brcm_sai_acl_table_entry_attach(table_id, l_entry, u_entry, state,
                                          TRUE, bind_point_type);
    BRCM_SAI_RV_CHK(SAI_API_ACL, "table entry attach", rv);

    return rv;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_acl_api_t acl_apis = {
    brcm_sai_create_acl_table,
    brcm_sai_delete_acl_table,
    brcm_sai_set_acl_table_attribute,
    brcm_sai_get_acl_table_attribute,
    brcm_sai_create_acl_entry,
    brcm_sai_delete_acl_entry,
    brcm_sai_set_acl_entry_attribute,
    brcm_sai_get_acl_entry_attribute,
    brcm_sai_create_acl_counter,
    brcm_sai_delete_acl_counter,
    brcm_sai_set_acl_counter_attribute,
    brcm_sai_get_acl_counter_attribute,
    brcm_sai_create_acl_range,
    brcm_sai_remove_acl_range,
    brcm_sai_set_acl_range_attribute,
    brcm_sai_get_acl_range_attribute,
    brcm_sai_create_acl_table_group,
    brcm_sai_remove_acl_table_group,
    brcm_sai_set_acl_table_group_attribute,
    brcm_sai_get_acl_table_group_attribute,
    brcm_sai_create_acl_table_group_member,
    brcm_sai_remove_acl_table_group_member,
    brcm_sai_set_acl_table_group_member_attribute,
    brcm_sai_get_acl_table_group_member_attribute
};

