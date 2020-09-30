/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

#define LAG_DEBUG(__fmt, __args...)             \
  COMP_DBG_LEVEL(LAG, DEBUG, __fmt,##__args)


/*
################################################################################
#                                Local state                                   #
################################################################################
*/
static int max_lags = 0;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t
_brcm_sai_lag_member_vlan_add(int tid, bcm_vlan_t vid, int utag, bcm_port_t port);
STATIC sai_status_t
_brcm_sai_lag_member_vlan_remove(bcm_vlan_t vid, bcm_port_t port);
STATIC sai_status_t
_brcm_sai_lag_acl_data_add(_brcm_sai_lag_info_t* p_lag_info, int type,
                           sai_object_id_t bind_obj);
STATIC sai_status_t
_brcm_sai_lag_acl_unbind_all(_brcm_sai_lag_info_t* p_lag_info);

STATIC sai_status_t
_brcm_sai_lag_acl_bind_update(_brcm_sai_lag_info_t* p_lag_info,
                              bool add_remove, bcm_port_t port);
/*
################################################################################
#                                Trunk functions                               #
################################################################################
*/
/*
    \brief Create LAG
    \param[out] lag_id LAG id
    \param[in] attr_count number of attributes
    \param[in] attr_list array of attributes
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_lag(_Out_ sai_object_id_t* lag_id,
                    _In_ sai_object_id_t switch_id,
                    _In_ uint32_t attr_count,
                    _In_ const sai_attribute_t *attr_list)
{
    int i;
    bcm_vlan_t vid;
    bcm_trunk_t trunk_id;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(lag_id))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_LAG_ATTR_INGRESS_ACL:
            case SAI_LAG_ATTR_EGRESS_ACL:
            case SAI_LAG_ATTR_PORT_VLAN_ID:
                break;
            default:
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                 "Unknown LAG attribute %d passed\n", attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_INFO,
                             "Error processing lag attributes\n");
            return rv;
        }
    }
    rv = bcm_trunk_create(0, 0, &trunk_id);
    BRCM_SAI_API_CHK(SAI_API_LAG, "trunk create", rv);
    *lag_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG, trunk_id);
    rv = bcm_trunk_psc_set(0, trunk_id, BCM_TRUNK_PSC_PORTFLOW);
    BRCM_SAI_API_CHK(SAI_API_LAG, "trunk psc set", rv);
    DATA_CLEAR(data.lag_info, _brcm_sai_lag_info_t);
    data.lag_info.idx = trunk_id;
    data.lag_info.valid = TRUE;
    rv = bcm_vlan_default_get(0, &vid);
    data.lag_info.vid = vid;
    BRCM_SAI_API_CHK(SAI_API_LAG, "default vlan get", rv);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_LAG_ATTR_INGRESS_ACL:
                rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_LIST_OBJ(i), INGRESS,
                                            *lag_id);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj LAG bind", rv);

                _brcm_sai_lag_acl_data_add(&(data.lag_info),
                                           INGRESS,
                                           BRCM_SAI_ATTR_LIST_OBJ(i));
                BRCM_SAI_RV_CHK(SAI_API_LAG, "lag acl bind add", rv);
                break;
            case SAI_LAG_ATTR_EGRESS_ACL:
                rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_LIST_OBJ(i), EGRESS,
                                            *lag_id);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj LAG bind", rv);
                break;
            case SAI_LAG_ATTR_PORT_VLAN_ID:
                data.lag_info.vid = attr_list[i].value.u16;
                break;
            default:
                break;
        }
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                    &trunk_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
    \brief Remove LAG
    \param[in] lag_id LAG id
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_lag(_In_ sai_object_id_t lag_id)
{
    sai_status_t rv;
    int max, tid, actual;
    bcm_trunk_member_t *members;
    bcm_trunk_info_t trunk_info;
    _brcm_sai_indexed_data_t data;
    bcm_trunk_chip_info_t trunk_chip_info;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(lag_id, SAI_OBJECT_TYPE_LAG))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         lag_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    tid = BRCM_SAI_GET_OBJ_VAL(bcm_trunk_t, lag_id);

    /* Check if this LAG still has members/objects */
    rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
    BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
    max = trunk_chip_info.trunk_group_count;
    members = ALLOC(sizeof(bcm_trunk_member_t) * max);
    if (IS_NULL(members))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                         "Error allocating memory for lag members.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &actual);
    BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
    CHECK_FREE(members);
    if (actual)
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "LAG still has %d members.\n",
                         actual);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    /* Check if this LAG is still part of an active RIF */
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data get", rv);
    if (0 != data.lag_info.rif_obj)
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "LAG in use as RIF.\n");
        return SAI_STATUS_OBJECT_IN_USE;
    }
    if (data.lag_info.acl_bind_count > 0)
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "LAG is bound to ACL.\n");
        return SAI_STATUS_OBJECT_IN_USE;
    }
    
    rv = bcm_trunk_destroy(0, tid);
    BRCM_SAI_API_CHK(SAI_API_LAG, "Trunk destroy", rv);

    data.lag_info.valid = FALSE;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag  data set", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
    \brief Set LAG Attribute
    \param[in] lag_id LAG id
    \param[in] attr Structure containing ID and value to be set
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_lag_attribute(_In_ sai_object_id_t  lag_id,
                           _In_ const sai_attribute_t *attr)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    int tid;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BRCM_SAI_SWITCH_INIT_CHECK;
    tid = BRCM_SAI_GET_OBJ_VAL(bcm_trunk_t, lag_id);   
    
    switch(attr->id)
    {
        case SAI_LAG_ATTR_INGRESS_ACL:
        {
            DATA_CLEAR(data.lag_info, _brcm_sai_lag_info_t);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &tid, &data); 
            BRCM_SAI_RV_CHK(SAI_API_LAG, "lag acl bind data get", rv);
            
            if (SAI_OBJECT_TYPE_NULL == BRCM_SAI_GET_OBJ_TYPE(attr->value.oid))
            {
                rv = _brcm_sai_lag_acl_unbind_all(&(data.lag_info));
                BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj lag unbind", rv);
            }
            else
            {
                rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_PTR_OBJ(), INGRESS,
                                            lag_id);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj lag bind", rv);                
                
                rv = _brcm_sai_lag_acl_data_add(&(data.lag_info),
                                                INGRESS,
                                                BRCM_SAI_ATTR_PTR_OBJ());
            }
            BRCM_SAI_RV_CHK(SAI_API_LAG, "lag acl bind add", rv);
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &tid, &data);            
            break;
        }
        case SAI_LAG_ATTR_EGRESS_ACL:
            rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_PTR_OBJ(), EGRESS,
                                        lag_id);
            BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj LAG bind", rv);
            break;
        case SAI_LAG_ATTR_PORT_VLAN_ID:
        {
            bcm_port_t port;
            int p, max, actual;
            bcm_trunk_member_t *members;
            bcm_trunk_info_t trunk_info;
         
            bcm_trunk_chip_info_t trunk_chip_info;
            DATA_CLEAR(data.lag_info, _brcm_sai_lag_info_t);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &tid, &data); 
            BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data get", rv);
            if (0 != data.lag_info.rif_obj)
            {
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "LAG in use as RIF.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            data.lag_info.vid = attr->value.u16;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &tid, &data);
            BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data set", rv);
            /* Update members */
            rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
            BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
            max = trunk_chip_info.trunk_group_count;
            members = ALLOC(sizeof(bcm_trunk_member_t) * max);
            if (IS_NULL(members))
            {
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                                 "Error allocating memory for lag members.\n");
                return SAI_STATUS_NO_MEMORY;
            }
            rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &actual);
            BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
            for (p=0; p<actual; p++)
            {
                rv = bcm_port_local_get(0, members[p].gport, &port);
                BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                                      members);
                rv = bcm_port_untagged_vlan_set(0, port, data.lag_info.vid);
                BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "pvlan set", rv, members);
            }
            CHECK_FREE(members);
            break;
        }
        default:
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, 
                             "Unknown lag attribute %d passed\n", attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
    }
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
    \brief Get LAG Attribute
    \param[in] lag_id LAG id
    \param[in] attr_count Number of attributes to be get
    \param[in,out] attr_list List of structures containing ID and value to be get
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_lag_attribute(_In_ sai_object_id_t lag_id,
                           _In_ uint32_t attr_count,
                           _Inout_ sai_attribute_t *attr_list)
{
    int i, tid;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(lag_id, SAI_OBJECT_TYPE_LAG))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         lag_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    tid = BRCM_SAI_GET_OBJ_VAL(bcm_trunk_t, lag_id);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_LAG_ATTR_PORT_LIST:
            {
                bcm_port_t port;
                bcm_trunk_member_t *members;
                bcm_trunk_info_t trunk_info;
                int rv1, p, actual, limit, max;
                bcm_trunk_chip_info_t trunk_chip_info;

                limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i);
                rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
                BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
                max = trunk_chip_info.trunk_group_count;
                members = ALLOC(sizeof(bcm_trunk_member_t) * max);
                if (IS_NULL(members))
                {
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                                     "Error allocating memory for lag members.\n");
                    return SAI_STATUS_NO_MEMORY;
                }
                rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &actual);
                BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
                if (actual > limit)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                else
                {
                    limit = actual;
                }
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = actual;
                for (p=0; p<limit; p++)
                {
                    rv1 = bcm_port_local_get(0, members[p].gport, &port);
                    BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv1,
                                          members);
                    BRCM_SAI_ATTR_LIST_OBJ_LIST(i, p) =
                        BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_LAG_MEMBER, 0,
                                                    port, tid);
                }
                CHECK_FREE(members);
                break;
            }
            case SAI_LAG_ATTR_PORT_VLAN_ID:
            {
                _brcm_sai_indexed_data_t data;

                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                                &tid, &data);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data get", rv);
                attr_list[i].value.u16 = data.lag_info.vid;
                break;
            }
            default:
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                 "Unknown LAG attribute %d passed\n", attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
        }
        if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_BUFFER_OVERFLOW != rv)
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_INFO,
                             "Error processing lag attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
    \brief Create LAG Member
    \param[out] lag_member_id LAG Member id
    \param[in] attr_count number of attributes
    \param[in] attr_list array of attributes
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_lag_member(_Out_ sai_object_id_t* lag_member_id,
                           _In_ sai_object_id_t switch_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list)
{
    bcm_vlan_t vid = 0;
    bcm_trunk_t tid = -1;
    bcm_port_t port = -1;
    bcm_trunk_member_t member;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_list_key_t list_key;
    _brcm_sai_list_data_t ldata, vdata;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, state = -1, utag = SAI_VLAN_TAGGING_MODE_UNTAGGED, lf = 0;
    bool rif = FALSE, egr_dis = FALSE, ing_dis = FALSE, lag_vlans_found = FALSE;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(lag_member_id);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_LAG_MEMBER_ATTR_LAG_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_LAG))
                {
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                     "Invalid object type 0x%16lx passed\n",
                                     BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                tid = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_trunk_t, i);
                break;
            case SAI_LAG_MEMBER_ATTR_PORT_ID:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i),
                                              SAI_OBJECT_TYPE_PORT))
                {
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                     "Invalid object type 0x%16lx passed\n",
                                     BRCM_SAI_ATTR_LIST_OBJ(i));
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                port = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_trunk_t, i);
                break;
            case SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE:
                egr_dis = attr_list[i].value.booldata;
                break;
            case SAI_LAG_MEMBER_ATTR_INGRESS_DISABLE:
                ing_dis = attr_list[i].value.booldata;
                break;
            default:
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                 "Unknown LAG member attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
        }
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_INFO,
                             "Error processing lag attributes\n");
            return rv;
        }
    }
    if (-1 == tid || -1 == port)
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "No lag or port provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    bcm_trunk_member_t_init(&member);
    rv = bcm_port_gport_get(0, port, &member.gport);
    BRCM_SAI_API_CHK(SAI_API_LAG, "port gport get", rv);
    if (egr_dis)
    {
        member.flags |= BCM_TRUNK_MEMBER_EGRESS_DISABLE;
    }
    if (ing_dis)
    {
        member.flags |= BCM_TRUNK_MEMBER_INGRESS_DISABLE;
    }
    rv = bcm_trunk_member_add(0, tid, &member);
    BRCM_SAI_API_CHK(SAI_API_LAG, "trunk member add", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data get", rv);

    /* Go ahead update ACL bindings */
    rv = _brcm_sai_lag_acl_bind_update(&(data.lag_info), TRUE, port);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag acl data update", rv);
    
    *lag_member_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_LAG_MEMBER, 0,
                                                 port, tid);
    list_key.obj_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                                  SAI_BRIDGE_PORT_TYPE_PORT,
                                                  SAI_OBJECT_TYPE_LAG, tid);
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key, 
                            &ldata);
    if (SAI_STATUS_SUCCESS != rv && rv != SAI_STATUS_ITEM_NOT_FOUND)
    {
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "sai bridge lag port list get", rv);
    }
    else if (rv != SAI_STATUS_ITEM_NOT_FOUND)
    {
        /* If this LAG is used as a bridge port */
        state = ldata.bdg_lag_ports->bridge_port_state;
        lf = ldata.bdg_lag_ports->learn_flags;
        vdata.vid_list = ldata.bdg_lag_ports->vid_list;
        if (!IS_NULL(vdata.vid_list))
        {
            lag_vlans_found = TRUE;
            do
            {
                vid = vdata.vid_list->vid;
                utag = vdata.vid_list->utag;
                /* Apply VLAN membership/tagging */
                rv = _brcm_sai_lag_member_vlan_add(tid, vid, utag, port);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "lag member vlan add", rv);
            } while (SAI_STATUS_SUCCESS ==
                     _brcm_sai_list_traverse(_BRCM_SAI_LIST_LAG_BP_VLAN_LIST, &vdata, &vdata));
        }
    }
    if (FALSE == lag_vlans_found)
    {
        vid = data.lag_info.vid;
        /* If this LAG is used as a RIF */
        if (0 != data.lag_info.rif_obj)
        {
            rif = TRUE;
        }
        /* Apply VLAN membership/tagging */
        rv = _brcm_sai_lag_member_vlan_add(tid, vid, utag, port);
        BRCM_SAI_RV_CHK(SAI_API_LAG, "lag member vlan add", rv);
    }
    /* Apply PVID */
    rv = bcm_port_untagged_vlan_set(0, port, data.lag_info.vid);
    BRCM_SAI_API_CHK(SAI_API_LAG, "pvlan set", rv);
    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG, "Added pvid %d for lag %d port %d\n", 
                     data.lag_info.vid, tid, port);
    if (0 != state)
    {
        if (rif) /* LAG rif */
        {
            rv = bcm_port_learn_set(0, port, (-1 == state) ? 
                                    BCM_PORT_LEARN_FWD : lf);
            BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 learn set", rv);
            rv = bcm_port_control_set(0, port, bcmPortControlL2Move, 
                                      (-1 == state) ? 
                                      BCM_PORT_LEARN_FWD : lf);
            BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 move set", rv);
        }
        else /* vlan rif or vanilla lag */
        {
            rv = bcm_port_learn_set(0, port, (-1 == state) ?
                                    BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD :
                                    lf);
            BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 learn set", rv);
            rv = bcm_port_control_set(0, port, bcmPortControlL2Move, 
                                      (-1 == state) ?
                                      BCM_PORT_LEARN_ARL | BCM_PORT_LEARN_FWD : 
                                      lf);
            BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 move set", rv);
        }
    }
    else if (0 == state)
    {
        rv = bcm_port_learn_set(0, port, 0);
        BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 learn set", rv);
        rv = bcm_port_control_set(0, port, bcmPortControlL2Move, 0);
        BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 move set", rv);
    }
    if (TRUE == data.lag_info.bcast_ip && FALSE == egr_dis)
    {
        rv = _brcm_sai_nbr_bcast_update(&data.lag_info.nbr, TRUE, tid, port);
        BRCM_SAI_RV_CHK(SAI_API_LAG, "bcast ip update", rv);
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                    &port, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "port info data get", rv);
    data.port_info.trunk = TRUE;
    data.port_info.tid = tid;

    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                    &port, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "port info data set", rv);


                
    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
    \brief Remove LAG Member
    \param[in] lag_member_id LAG Member id
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_lag_member(_In_ sai_object_id_t lag_member_id)
{
    int flags = 0;
    sai_status_t rv;
    bcm_trunk_t tid;
    bcm_port_t port;
    bcm_vlan_t vid = 0;
    bcm_trunk_member_t member;
    bcm_pbmp_t _pbm;
    _brcm_sai_list_data_t ldata, vdata;
    _brcm_sai_list_key_t list_key;
    _brcm_sai_indexed_data_t data, pdata;
    bool bp = FALSE, state, lag_vlans_found = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(lag_member_id, SAI_OBJECT_TYPE_LAG_MEMBER))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         lag_member_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    port = BRCM_SAI_GET_OBJ_MAP(lag_member_id);
    tid = BRCM_SAI_GET_OBJ_VAL(int, lag_member_id);
    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG, "Port: %d, LAG: %d\n", port, tid);
    bcm_trunk_member_t_init(&member);
    rv = bcm_port_gport_get(0, port, &member.gport);
    BRCM_SAI_API_CHK(SAI_API_LAG, "port gport get", rv);
    list_key.obj_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_BRIDGE_PORT,
                                                  SAI_BRIDGE_PORT_TYPE_PORT,
                                                  SAI_OBJECT_TYPE_LAG, tid);
    rv = _brcm_sai_list_get(_BRCM_SAI_LIST_BRIDGE_LAG_PORTS, NULL, &list_key, 
                            &ldata);
    if (SAI_STATUS_SUCCESS != rv && rv != SAI_STATUS_ITEM_NOT_FOUND)
    {
        BRCM_SAI_RV_CHK(SAI_API_BRIDGE, "sai bridge lag port list get.", rv);
    }
    else if (rv != SAI_STATUS_ITEM_NOT_FOUND)
    {
        /* If this LAG is used as a bridge port */
        bp = TRUE;
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG, "LAG is a bridge port\n");
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data get", rv);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO,
                                    &port, &pdata);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "port info data get", rv);

    /* remove any port from any acls that use this lag */
    _brcm_sai_lag_acl_bind_update(&(data.lag_info), FALSE, port);
    
    if (0 != data.lag_info.rif_obj || bp)
    {
        if (_BP_DELETED != pdata.port_info.bdg_port)
        {
            state = pdata.port_info.bdg_port_admin_state;
            if (state)
            {
                flags = pdata.port_info.learn_flags;
            }
        }
        /* If this lag was part of a RIF then revert learning disable of port */
        rv = bcm_port_learn_set(0, port, flags);
        BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 learn set", rv);
        rv = bcm_port_control_set(0, port, bcmPortControlL2Move, flags);
        BRCM_SAI_API_CHK(SAI_API_LAG, "port L2 move set", rv);
    }
    if (bp)
    {
        /* If this LAG is used as a bridge port */
        vdata.vid_list = ldata.bdg_lag_ports->vid_list;
        if (!IS_NULL(vdata.vid_list))
        {
            lag_vlans_found = TRUE;
            do
            {
                vid = vdata.vid_list->vid;
                /* Remove VLAN membership */
                rv = _brcm_sai_lag_member_vlan_remove(vid, port);
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                 "Remove member port: %d from vlan: %d\n", port, vid);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "lag member vlan remove", rv);
            } while (SAI_STATUS_SUCCESS ==
                     _brcm_sai_list_traverse(_BRCM_SAI_LIST_LAG_BP_VLAN_LIST, &vdata, &vdata));
        }
    }
    /* At this point, if 0 == vid, it means:
     *     1) the LAG was never made into a BP or
     *     2) the LAG is a BP, but was never added to any VLANs
     */
    if (FALSE == lag_vlans_found && 0 != data.lag_info.rif_obj)
    {
        /* Assuming bridge port and rif to be exclusive */
        vid = data.lag_info.vid;
        if (0 != vid)
        {
            BCM_PBMP_CLEAR(_pbm);
            BCM_PBMP_PORT_ADD(_pbm, port);
            rv = bcm_vlan_port_remove(0, vid, _pbm);
            if (BCM_FAILURE(rv) && (BCM_E_NOT_FOUND != rv))
            {
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                 "Error %s removing port from vlan id %u\n",
                                 bcm_errmsg(rv), vid);
                return BRCM_RV_BCM_TO_SAI(rv);
            }
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG, "Removed member port %d from vlan %d\n",
                             port, vid);
        }
    }
    if (TRUE == data.lag_info.bcast_ip)
    {
        int max, p, actual;
        bcm_port_t _port;
        bcm_trunk_member_t *members;
        bcm_trunk_info_t trunk_info;
        bcm_trunk_chip_info_t trunk_chip_info;

        rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
        BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
        max = trunk_chip_info.trunk_group_count;
        members = ALLOC(sizeof(bcm_trunk_member_t) * max);
        if (IS_NULL(members))
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                            "Error allocating memory for lag members.\n");
            return SAI_STATUS_NO_MEMORY;
        }
        rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &actual);
        BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
        for (p=0; p<actual; p++)
        {
            rv = bcm_port_local_get(0, members[p].gport, &_port);
            BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                                  members);
            if (_port == port)
            {
                break;
            }    
        }
        if (p == actual)
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                             "Port not found in LAG members.\n");
            CHECK_FREE(members);
            return SAI_STATUS_INVALID_PARAMETER;
        }
        if (!(members[p].flags & BCM_TRUNK_MEMBER_EGRESS_DISABLE))
        {
            rv = _brcm_sai_nbr_bcast_update(&data.lag_info.nbr, FALSE, tid, port);
            BRCM_SAI_RV_CHK_FREE(SAI_API_LAG, "bcast ip update", rv, members);
        }
        CHECK_FREE(members);
    }
    pdata.port_info.trunk = FALSE;
    pdata.port_info.tid = 0;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO,
                                    &port, &pdata);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "port info data set", rv);
    rv = bcm_trunk_member_delete(0, tid, &member);
    BRCM_SAI_API_CHK(SAI_API_LAG, "trunk member add", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
    \brief Set LAG Member Attribute
    \param[in] lag_member_id LAG Member id
    \param[in] attr Structure containing ID and value to be set
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_lag_member_attribute(_In_ sai_object_id_t  lag_member_id,
                                  _In_ const sai_attribute_t *attr)
{
    sai_status_t rv;
    bcm_trunk_t tid;
    bcm_trunk_member_t *members;
    bcm_trunk_info_t trunk_info;
    bcm_port_t member_port, port;
    _brcm_sai_indexed_data_t data;
    bcm_trunk_chip_info_t trunk_chip_info;
    int p, actual, max, egr_dis = -1, ing_dis = -1;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "NULL attr passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    switch(attr->id)
    {
        case SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE:
            egr_dis = attr->value.booldata;
            break;
        case SAI_LAG_MEMBER_ATTR_INGRESS_DISABLE:
            ing_dis = attr->value.booldata;
            break;
        default:
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                 "Unknown LAG member attribute %d passed\n",
                                 attr->id);
            return SAI_STATUS_INVALID_PARAMETER;
    }
    if (-1 == egr_dis && -1 == ing_dis)
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "No disable control provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    member_port = BRCM_SAI_GET_OBJ_MAP(lag_member_id);
    tid = BRCM_SAI_GET_OBJ_VAL(int, lag_member_id);
    
    rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
    BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
    max = trunk_chip_info.trunk_group_count;
    members = ALLOC(sizeof(bcm_trunk_member_t) * max);
    if (IS_NULL(members))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                         "Error allocating memory for lag members.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &actual);
    BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
    for (p=0; p<actual; p++)
    {
        rv = bcm_port_local_get(0, members[p].gport, &port);
        BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                               members);
        if (member_port == port)
        {
            break;
        }    
    }
    if (p == actual)
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                         "Port not found in LAG members.\n");
        CHECK_FREE(members);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (-1 != egr_dis)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &tid, &data);
        BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data get", rv);
        if (egr_dis)
        {
            members[p].flags |= BCM_TRUNK_MEMBER_EGRESS_DISABLE;
            if (TRUE == data.lag_info.bcast_ip)
            {
                rv = _brcm_sai_nbr_bcast_update(&data.lag_info.nbr, FALSE, tid, port);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "bcast ip update", rv);
            }
        }
        else
        {
            members[p].flags &= ~BCM_TRUNK_MEMBER_EGRESS_DISABLE;
            if (TRUE == data.lag_info.bcast_ip)
            {
                rv = _brcm_sai_nbr_bcast_update(&data.lag_info.nbr, TRUE, tid, port);
                BRCM_SAI_RV_CHK(SAI_API_LAG, "bcast ip update", rv);
            }
        }
    }
    if (-1 != ing_dis)
    {
        if (ing_dis)
        {
            members[p].flags |= BCM_TRUNK_MEMBER_INGRESS_DISABLE;
        }
        else
        {
            members[p].flags &= ~BCM_TRUNK_MEMBER_INGRESS_DISABLE;
        }
    }
    rv = bcm_trunk_set(0, tid, &trunk_info, actual, members);
    BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk set", rv, members);
    CHECK_FREE(members);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
    \brief Get LAG Member Attribute
    \param[in] lag_member_id LAG Member id
    \param[in] attr_count Number of attributes to be get
    \param[in,out] attr_list List of structures containing ID and value to be get
    \return Success: SAI_STATUS_SUCCESS
            Failure: Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_lag_member_attribute(_In_ sai_object_id_t lag_member_id,
                                  _In_ uint32_t attr_count,
                                  _Inout_ sai_attribute_t *attr_list)
{
    int i, p, tid, max, actual;
    bcm_trunk_member_t *members;
    bcm_trunk_info_t trunk_info;
    bcm_port_t member_port, port;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_trunk_chip_info_t trunk_chip_info;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(lag_member_id, SAI_OBJECT_TYPE_LAG_MEMBER))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    tid = BRCM_SAI_GET_OBJ_VAL(int, lag_member_id);
    port = BRCM_SAI_GET_OBJ_MAP(lag_member_id);
    BRCM_SAI_LOG_ACL(SAI_LOG_LEVEL_DEBUG,
                     "Get lag member: %d\n", tid);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE:
            case SAI_LAG_MEMBER_ATTR_INGRESS_DISABLE:
                rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
                BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
                max = trunk_chip_info.trunk_group_count;
                members = ALLOC(sizeof(bcm_trunk_member_t) * max);
                if (IS_NULL(members))
                {
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                                     "Error allocating memory for lag members.\n");
                    return SAI_STATUS_NO_MEMORY;
                }
                rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &actual);
                BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
                for (p=0; p<actual; p++)
                {
                    rv = bcm_port_local_get(0, members[p].gport, &member_port);
                    BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                                           members);
                    if (member_port == port)
                    {
                        break;
                    }    
                }
                if (p == actual)
                {
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                                     "Port not found in LAG members.\n");
                    CHECK_FREE(members);
                    return SAI_STATUS_INVALID_PARAMETER;
                }                
                break;
            default:
                break;
        }
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_LAG_MEMBER_ATTR_LAG_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG, tid);
                break;
            case SAI_LAG_MEMBER_ATTR_PORT_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, port);
                break;
            case SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE:
                attr_list[i].value.booldata = 
                    (members[p].flags & BCM_TRUNK_MEMBER_EGRESS_DISABLE) ? TRUE : FALSE;
                break;
            case SAI_LAG_MEMBER_ATTR_INGRESS_DISABLE:
                attr_list[i].value.booldata = 
                    (members[p].flags & BCM_TRUNK_MEMBER_INGRESS_DISABLE) ? TRUE : FALSE;
                break;
            default:
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                 "Unknown lag member attribute %d passed\n",
                                 attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_INFO,
                             "Error processing lag member attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/

sai_status_t
_brcm_sai_alloc_lag_acl_binding()
{
    int i;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
                                              
    for (i=0;i<max_lags;i++)
    {
        DATA_CLEAR(data.lag_info, _brcm_sai_lag_info_t);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &i, &data);
        BRCM_SAI_RV_CHK(SAI_API_LAG, "lag info data get", rv);
        
        if (data.lag_info.acl_bind_count > 0)
        {
            _brcm_sai_list_init(_BRCM_SAI_LIST_LAG_ACL_BIND_POINTS,
                                i, data.lag_info.acl_bind_count,
                                (void**)&data.lag_info.list_head);
            BRCM_SAI_RV_CHK(SAI_API_LAG, "list init acl bind", rv);
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &i, &data);
        }
    }
    return SAI_STATUS_SUCCESS;
}


sai_status_t
_brcm_sai_free_lag_acl_binding()
{
    sai_status_t rv;
    int i;
    _brcm_sai_indexed_data_t data;
    uint64 val;
    
    for (i=0;i<max_lags;i++)
    {
        DATA_CLEAR(data.lag_info, _brcm_sai_lag_info_t);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &i, &data);
        BRCM_SAI_RV_CHK(SAI_API_LAG, "lag info data get", rv);
        
        if (data.lag_info.acl_bind_count > 0)
        {
            _brcm_sai_list_free(_BRCM_SAI_LIST_LAG_ACL_BIND_POINTS,
                                i, data.lag_info.acl_bind_count,
                                (void*)data.lag_info.list_head);
            val = i;
            data.lag_info.list_head = (_brcm_sai_acl_bind_point_t*)val;
            BRCM_SAI_RV_CHK(SAI_API_LAG, "list free acl bind", rv);
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &i, &data);
        }
    }
    
    return SAI_STATUS_SUCCESS;
}


sai_status_t
_brcm_sai_alloc_lag()
{
    int max;
    sai_status_t rv;
    bcm_trunk_chip_info_t trunk_chip_info;

    rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
    BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
    max_lags = max = trunk_chip_info.trunk_group_count;
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                     max);
    BRCM_SAI_RV_LVL_CHK(SAI_API_LAG, SAI_LOG_LEVEL_CRITICAL,
                        "initializing lag info table", rv);
    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_INFO, "Max lag info: %d.\n", 
                     max);

    /* now alloc the acl lists */
    rv = _brcm_sai_alloc_lag_acl_binding();    
    BRCM_SAI_RV_CHK(SAI_API_LAG, "acl list alloc", rv);
    
    return rv;
}


sai_status_t
_brcm_sai_free_lag()
{
    sai_status_t rv;
    /* first free the attached lists */
    _brcm_sai_free_lag_acl_binding();
    
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_LAG_INFO_TABLE, 
                                      0, max_lags, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_LAG, SAI_LOG_LEVEL_CRITICAL,
                        "freeing lag rif state", rv);
    return rv;
}

STATIC sai_status_t
_brcm_sai_lag_member_vlan_add(int tid, bcm_vlan_t vid, int utag, bcm_port_t port)
{
    sai_status_t rv;
    pbmp_t pbm, upbm;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    /* Lag port vlan mgmt */
    rv  = bcm_vlan_port_get(0, vid, &pbm, &upbm);
    BRCM_SAI_API_CHK(SAI_API_LAG, "vlan port get", rv);
    /* Add port to lag vlan */
    BCM_PBMP_PORT_ADD(pbm, port);
    if (SAI_VLAN_TAGGING_MODE_UNTAGGED == utag)
    {
        BCM_PBMP_PORT_ADD(upbm, port);
    } 
    rv = bcm_vlan_port_add(0, vid, pbm, upbm);
    BRCM_SAI_API_CHK(SAI_API_LAG, "vlan port add", rv);
    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG, "Added member tid %d port %d to vlan %d with utag %d\n",
                     tid, port, (int)vid, utag);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

STATIC sai_status_t
_brcm_sai_lag_member_vlan_remove(bcm_vlan_t vid, bcm_port_t port)
{
    sai_status_t rv;
    bcm_pbmp_t _pbm;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_LAG);
    BCM_PBMP_CLEAR(_pbm);
    BCM_PBMP_PORT_ADD(_pbm, port);
    /* Remove port from lag vlan */
    rv = bcm_vlan_port_remove(0, vid, _pbm);
    if (BCM_FAILURE(rv) && (BCM_E_NOT_FOUND != rv))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                         "Error %s removing port from vlan id %u\n",
                         bcm_errmsg(rv), vid);
        return BRCM_RV_BCM_TO_SAI(rv);
    }
    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG, "Removed member port %d from vlan %d\n",
                     port, vid);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_LAG);
    return rv;
}

sai_status_t
_brcm_sai_lag_member_pbmp_get(bcm_trunk_t tid, bcm_pbmp_t* member_pbmp)
{
    bcm_port_t port;
    bcm_trunk_member_t *members;
    bcm_trunk_info_t trunk_info;
    bcm_trunk_chip_info_t trunk_chip_info;
    sai_status_t rv;
    int actual,p;

    static int max = 0;

    /* first find the max */
    if (!max)
    {
        rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
        BRCM_SAI_API_CHK(SAI_API_LAG, "trunk chip info get", rv);
        max = trunk_chip_info.trunk_group_count;
    }
    members = ALLOC(sizeof(bcm_trunk_member_t) * max);
    if (IS_NULL(members))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                         "Error allocating memory for lag members.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    
    rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &actual);    
    BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);

    BCM_PBMP_CLEAR((*member_pbmp));
    
    for (p=0; p<actual; p++)
    {
        rv = bcm_port_local_get(0, members[p].gport, &port);
        BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                              members);
        BCM_PBMP_PORT_ADD((*member_pbmp), port);        
    }
    CHECK_FREE(members);
    return SAI_STATUS_SUCCESS;
}

/*
  Add a lag->acl bind info to the acl_bind_list
 */
STATIC sai_status_t
_brcm_sai_lag_acl_data_add(_brcm_sai_lag_info_t* p_lag_info, int type,
                           sai_object_id_t bind_obj)
{
    _brcm_sai_acl_bind_point_t* new_acl;
    _brcm_sai_acl_bind_point_t* current;
    
    
    current = p_lag_info->acl_bind_list;
    new_acl = ALLOC_CLEAR(1, sizeof(_brcm_sai_acl_bind_point_t));
    if (IS_NULL(new_acl))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_CRITICAL,
                         "Error allocating memory for table acl bind point list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_acl->type = type;
    /* store acl table/group obj value */
    new_acl->val = bind_obj;
    new_acl->next = NULL;
    if (p_lag_info->acl_bind_count > 0)
    {
        current->next  = new_acl;
    }
    else
    {
        p_lag_info->acl_bind_list = new_acl;
        p_lag_info->list_head = new_acl;
    }
    p_lag_info->acl_bind_count++;
    LAG_DEBUG("Lag %d to bound acl 0x%x count %d\n",
              p_lag_info->idx,
              BRCM_SAI_GET_OBJ_VAL(int, new_acl->val),
              p_lag_info->acl_bind_count);
    return SAI_STATUS_SUCCESS;
}


/*
  Used to perform unbind of all INGRESS ACLS on a lag!
 */
STATIC sai_status_t
_brcm_sai_lag_acl_unbind_all(_brcm_sai_lag_info_t* p_lag_info)
{
    _brcm_sai_acl_bind_point_t* temp = p_lag_info->list_head;
    _brcm_sai_acl_bind_point_t* current;
    bcm_pbmp_t unbind_pbmp;
    sai_status_t rv;
    int acl_bp_id;
    sai_object_id_t lag = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG,
                                              p_lag_info->idx);
    if (p_lag_info->acl_bind_count == 0)
    {
        p_lag_info->acl_bind_list = NULL;
        p_lag_info->list_head = NULL;
        return SAI_STATUS_SUCCESS;
    }

    rv = _brcm_sai_lag_member_pbmp_get(p_lag_info->idx,
                                       &unbind_pbmp);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag members get", rv);
    
    while (temp)
    {
        acl_bp_id = BRCM_SAI_GET_OBJ_VAL(int, temp->val);
        LAG_DEBUG("Lag %d to unbind acl 0x%x\n",
                  p_lag_info->idx, acl_bp_id);
        _brcm_sai_acl_obj_lag_unbind(temp->val, lag, &unbind_pbmp);
        BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj lag unbind", rv);
        if (SAI_OBJECT_TYPE_ACL_TABLE_GROUP == BRCM_SAI_GET_OBJ_TYPE(temp->val))
        {
            rv = _brcm_sai_acl_table_group_bind_point_detach(acl_bp_id, 
                                                             SAI_ACL_BIND_POINT_TYPE_LAG, 
                                                             lag);
            BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL table bind point detach", rv);
        }
        else if (SAI_OBJECT_TYPE_ACL_TABLE == BRCM_SAI_GET_OBJ_TYPE(temp->val))
        {
            rv = _brcm_sai_acl_table_bind_point_detach(acl_bp_id, 
                                                       SAI_ACL_BIND_POINT_TYPE_LAG, 
                                                       lag);
            BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL table bind point detach", rv);
        }
        else
        {
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR, "Invalid ACL object type.\n");
            return SAI_STATUS_INVALID_OBJECT_TYPE;
        }
        current = temp;
        temp = temp->next;
        CHECK_FREE(current);
    }
    
    /* need sync here bind_count checked against list elem?*/
    p_lag_info->acl_bind_count = 0;
    p_lag_info->acl_bind_list = NULL;
    p_lag_info->list_head = NULL;
    
    return SAI_STATUS_SUCCESS;
}


/*
  remove the table/group object from the lag
 */

sai_status_t
_brcm_sai_lag_unbind_acl_obj(int tid,
                             sai_object_id_t acl_obj)
{
    _brcm_sai_indexed_data_t data;
    _brcm_sai_lag_info_t* p_lag_info;
    _brcm_sai_acl_bind_point_t* temp, *prev = NULL;
    sai_status_t rv;
      
    DATA_CLEAR(data.lag_info, _brcm_sai_lag_info_t);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                    &tid, &data);    
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data get", rv);


    p_lag_info = &(data.lag_info);
    if (p_lag_info->acl_bind_count == 0)
    {
        return SAI_STATUS_SUCCESS;
    }
    temp = p_lag_info->list_head;
    while (temp)        
    {
        if (temp->val == acl_obj)
        {
            LAG_DEBUG("Unbinding acl %d from lag %d\n",
                      BRCM_SAI_GET_OBJ_VAL(int, temp->val),
                      p_lag_info->idx);
            if (prev != NULL)
            {
                prev->next = temp->next;
                /* last elem del */
                if (temp->next == NULL)
                {
                    p_lag_info->acl_bind_list = prev;
                }                
            }
            else
            {
                /* 1st elem del */
                p_lag_info->list_head = temp->next;
            }
            CHECK_FREE(temp);
            p_lag_info->acl_bind_count--;
            if (p_lag_info->acl_bind_count == 0)
            {
                p_lag_info->acl_bind_list = NULL;
                p_lag_info->list_head = NULL;
            }
            LAG_DEBUG("lag %d bindcount %d\n",
                      p_lag_info->idx,
                      p_lag_info->acl_bind_count);
            break;
        }
        prev = temp;
        temp = temp->next;
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                    &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_LAG, "lag rif data set", rv);
    return SAI_STATUS_SUCCESS;
}
                         


/*
  Used to perform bind after lag member changes or lag removal
 */
STATIC sai_status_t
_brcm_sai_lag_acl_bind_update(_brcm_sai_lag_info_t* p_lag_info,
                              bool add_remove, bcm_port_t port)
{
    _brcm_sai_acl_bind_point_t* temp = p_lag_info->list_head;
    sai_object_id_t lag =  BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG,
                                           p_lag_info->idx);
    sai_status_t rv;
    
    if (p_lag_info->acl_bind_count == 0)
    {
        return SAI_STATUS_SUCCESS;
    }

    while (temp)
    {
        if (TRUE == add_remove)
        {
            /* add is called after the lag member has been added */            
            rv = _brcm_sai_acl_obj_bind(temp->val, INGRESS,
                                        lag);
            LAG_DEBUG("Lag %d to bind acl 0x%x\n",
                      p_lag_info->idx,
                      BRCM_SAI_GET_OBJ_VAL(int, temp->val));
            BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj lag bind", rv);
        }
        else
        {
            bcm_pbmp_t unbind_pbmp;
            BCM_PBMP_CLEAR(unbind_pbmp);
            BCM_PBMP_PORT_ADD(unbind_pbmp, port);
            
            LAG_DEBUG("Lag %d added port %d to unbind acl 0x%x\n",
                      p_lag_info->idx,
                      port,
                      BRCM_SAI_GET_OBJ_VAL(int, temp->val));
            rv = _brcm_sai_acl_obj_lag_unbind(temp->val, lag, &unbind_pbmp);
            BRCM_SAI_RV_CHK(SAI_API_LAG, "ACL obj lag unbind", rv);
        }
        temp = temp->next;
    }
    return SAI_STATUS_SUCCESS;
}



/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_lag_api_t lag_apis = {
   brcm_sai_create_lag,
   brcm_sai_remove_lag,
   brcm_sai_set_lag_attribute,
   brcm_sai_get_lag_attribute,
   brcm_sai_create_lag_member,
   brcm_sai_remove_lag_member,
   brcm_sai_set_lag_member_attribute,
   brcm_sai_get_lag_member_attribute,
   NULL,
   NULL
};
