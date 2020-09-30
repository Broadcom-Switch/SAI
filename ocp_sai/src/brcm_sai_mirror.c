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
_brcm_sai_update_acl_mirror_session(int unit,
                                    bcm_field_group_t group,
                                    void *user_data);

/*
################################################################################
#                              Mirror functions                                #
################################################################################
*/
/**
 * @brief Create mirror session.
 *
 * @param[out] session_id Port mirror session id
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Value of attributes
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
STATIC sai_status_t
brcm_sai_create_mirror_session(_Out_ sai_object_id_t *session_id,
                               _In_ sai_object_id_t switch_id,
                               _In_  uint32_t attr_count,
                               _In_  const sai_attribute_t *attr_list)
{
    return _brcm_sai_create_mirror_session(session_id, attr_count, attr_list);
}

/**
 * @brief Remove mirror session.
 *
 * @param[in] session_id Port mirror session id
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
STATIC sai_status_t
brcm_sai_remove_mirror_session(_In_ sai_object_id_t session_id)
{
    int idx;
    sai_status_t rv;
    bcm_gport_t mirror_dest_id;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_MIRROR);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(session_id, SAI_OBJECT_TYPE_MIRROR_SESSION))
    {
        BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR,
                            "Invalid mirror session object 0x%16lx passed\n",
                            session_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    mirror_dest_id = BRCM_SAI_GET_OBJ_VAL(int, session_id);
    idx = mirror_dest_id & 0xff;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data get", rv);
    if (data.ms.ref_count)
    {
        BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Mirror session %d in use %d\n",
                            idx, data.ms.ref_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    rv = bcm_mirror_destination_destroy(0, mirror_dest_id);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror dest destroy", rv);
    data.ms.destid = 0;
    data.ms.gport = 0;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_MIRROR);

    return rv;
}

/**
 * @brief Set mirror session attributes.
 *
 * @param[in] session_id Port mirror session id
 * @param[in] attr Value of attribute
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
STATIC sai_status_t
brcm_sai_set_mirror_session_attribute(_In_ sai_object_id_t session_id,
                                      _In_ const  sai_attribute_t *attr)
{
    bcm_mirror_destination_t md;
    bcm_port_config_t config;
    int p, idx, midx, md_count;
#ifndef BRCM_SAI_MIRROR_MTP_COUNT
#define BRCM_SAI_MIRROR_MTP_COUNT (_BRCM_SAI_MAX_MIRROR_SESSIONS/2)
#endif
    bcm_gport_t mirror_dest[BRCM_SAI_MIRROR_MTP_COUNT];
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_MIRROR);
    if (BRCM_SAI_CHK_OBJ_MISMATCH(session_id, SAI_OBJECT_TYPE_MIRROR_SESSION))
    {
        BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR,
                            "Invalid mirror session object 0x%16lx passed\n",
                            session_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    bcm_mirror_destination_t_init(&md);
    md.mirror_dest_id = BRCM_SAI_GET_OBJ_VAL(int, session_id);

    midx = md.mirror_dest_id & 0xff;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                    &midx, &data);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data get", rv);
    if (0 == data.ms.destid || 0 == data.ms.gport)
    {
        BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR,
                            "Mirror session %d invalid state\n", midx);
        return SAI_STATUS_FAILURE;
    }
    rv = bcm_mirror_destination_get(0, md.mirror_dest_id, &md);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror dest get", rv);

    switch (attr->id)
    {
        case SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS:
            sal_memcpy(md.dst_mac, attr->value.mac, sizeof(sai_mac_t));
            break;
        case SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS:
            sal_memcpy(md.src_mac, attr->value.mac, sizeof(sai_mac_t));
            break;
        case SAI_MIRROR_SESSION_ATTR_MONITOR_PORT:
            if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_ATTR_PTR_OBJ_TYPE()) /* Port */
            {   
                rv = bcm_port_gport_get(0, BRCM_SAI_ATTR_PTR_OBJ_VAL(bcm_port_t), 
                                        &md.gport);
            }
            else if (SAI_OBJECT_TYPE_LAG == BRCM_SAI_ATTR_PTR_OBJ_TYPE())
            {
                BCM_GPORT_TRUNK_SET(md.gport, BRCM_SAI_ATTR_PTR_OBJ_VAL(bcm_port_t));
            }
            break;
        case SAI_MIRROR_SESSION_ATTR_VLAN_ID:
            md.vlan_id &= 0xf000;
            md.vlan_id |= (attr->value.u16 & 0xfff);
            break;
        default:
            BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_INFO,
                                "Unknown or unsupported mirror session attribute %d passed\n",
                                attr->id);
            return SAI_STATUS_INVALID_PARAMETER;
    }

    md.flags |= (BCM_MIRROR_DEST_REPLACE | BCM_MIRROR_DEST_WITH_ID);
    rv = bcm_mirror_destination_create(0, &md);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror dest create", rv);

    /* Now go and update all the dependencies */
    /* ACLs */
    rv = bcm_field_group_traverse(0, _brcm_sai_update_acl_mirror_session, 
                                  (void *)(uint64_t)md.mirror_dest_id);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "field group traverse", rv);

    /* Ports */
    rv = bcm_port_config_get(0, &config);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "port config get", rv);
    BCM_PBMP_ITER(config.port, p)
    {
        /* Try ingress */
        rv = bcm_mirror_port_dest_get(0, p, 
                                      BCM_MIRROR_PORT_ENABLE | BCM_MIRROR_PORT_INGRESS,
                                      BRCM_SAI_MIRROR_MTP_COUNT,
                                      &mirror_dest[0],
                                      &md_count);
        BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror port dest get", rv);
        if (md_count)
        {
            for (idx = 0; idx < md_count; idx++)
            {
                if (mirror_dest[idx] == md.mirror_dest_id)
                {
                    rv = bcm_mirror_port_dest_delete(0, p, 
                                                     BCM_MIRROR_PORT_ENABLE | BCM_MIRROR_PORT_INGRESS,
                                                     md.mirror_dest_id);
                    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror port dest delete ingress", rv);
                    rv = bcm_mirror_port_dest_add(0, p, 
                                                  BCM_MIRROR_PORT_ENABLE | BCM_MIRROR_PORT_INGRESS,
                                                  md.mirror_dest_id);
                    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror port dest add ingress", rv);
                }
            }
        }
        /* Try egress */
        rv = bcm_mirror_port_dest_get(0, p, 
                                      BCM_MIRROR_PORT_ENABLE | BCM_MIRROR_PORT_EGRESS,
                                      BRCM_SAI_MIRROR_MTP_COUNT,
                                      &mirror_dest[0],
                                      &md_count);
        BRCM_SAI_API_CHK(SAI_API_SWITCH, "mirror port dest get", rv);
        if (md_count)
        {
            for (idx = 0; idx < md_count; idx++)
            {
                if (mirror_dest[idx] == md.mirror_dest_id)
                {
                    rv = bcm_mirror_port_dest_delete(0, p, 
                                                     BCM_MIRROR_PORT_ENABLE | BCM_MIRROR_PORT_EGRESS,
                                                     md.mirror_dest_id);
                    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror port dest delete egress", rv);
                    rv = bcm_mirror_port_dest_add(0, p, 
                                                  BCM_MIRROR_PORT_ENABLE | BCM_MIRROR_PORT_EGRESS,
                                                  md.mirror_dest_id);
                    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror port dest add egress", rv);
                }
            }
        }
    }
    data.ms.gport = md.gport;
    sal_memcpy(&data.ms.src_mac, &md.src_mac, sizeof(bcm_mac_t));
    sal_memcpy(&data.ms.dst_mac, &md.dst_mac, sizeof(bcm_mac_t));
    data.ms.vlan_id = md.vlan_id;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_MIRROR_SESSION,
                                    &midx, &data); 
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_MIRROR);

    return rv;
}

/**
 * @brief Get mirror session attributes.
 *
 * @param[in] session_id Port mirror session id
 * @param[in] attr_count Number of attributes
 * @param[inout] attr_list Value of attribute
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
STATIC sai_status_t
brcm_sai_get_mirror_session_attribute(_In_ sai_object_id_t session_id,
                                      _In_ uint32_t attr_count,
                                      _Inout_ sai_attribute_t *attr_list)
{
    int i;
    sai_status_t rv;
    bcm_gport_t msid;
    bcm_mirror_destination_t md;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_MIRROR);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(session_id, SAI_OBJECT_TYPE_MIRROR_SESSION))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    msid = BRCM_SAI_GET_OBJ_VAL(int, session_id);
    BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_DEBUG,
                        "Get mirror session: %d\n", msid);
    rv = bcm_mirror_destination_get(0, msid, &md);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror dest get", rv);
    
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_MIRROR_SESSION_ATTR_TYPE:
                if (md.vlan_id && (md.flags & BCM_MIRROR_DEST_TUNNEL_L2))
                {
                    attr_list[i].value.s32 = SAI_MIRROR_SESSION_TYPE_REMOTE;
                }
                else if (md.gre_protocol && (md.flags |= BCM_MIRROR_DEST_TUNNEL_IP_GRE))
                {
                    attr_list[i].value.s32 = SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE;
                }
                else
                {
                    attr_list[i].value.s32 = SAI_MIRROR_SESSION_TYPE_LOCAL;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_MONITOR_PORT:
                if (BCM_GPORT_IS_TRUNK(md.gport))
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) =
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG, BCM_GPORT_TRUNK_GET(md.gport));
                }
                else
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) =
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, BCM_GPORT_MODPORT_PORT_GET(md.gport));
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_TPID:
                attr_list[i].value.u16 = md.tpid;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_ID:
                attr_list[i].value.u16 = md.vlan_id & 0xfff;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_PRI:
                attr_list[i].value.u8 = (md.vlan_id >> 13) & 0x7;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_CFI:
                attr_list[i].value.u8 = (md.vlan_id >> 12) & 0x1;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_HEADER_VALID:
                attr_list[i].value.booldata = FALSE;
                if (md.vlan_id && md.gre_protocol && (md.flags |= BCM_MIRROR_DEST_TUNNEL_IP_GRE))
                {
                    attr_list[i].value.booldata = TRUE;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_ERSPAN_ENCAPSULATION_TYPE:
                attr_list[i].value.s32 = 0;
                if (md.gre_protocol && (md.flags |= BCM_MIRROR_DEST_TUNNEL_IP_GRE))
                {
                    attr_list[i].value.s32 = SAI_ERSPAN_ENCAPSULATION_TYPE_MIRROR_L3_GRE_TUNNEL;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION:
                attr_list[i].value.u8 = md.version;
                break;
            case SAI_MIRROR_SESSION_ATTR_TOS:
                attr_list[i].value.u16 = md.tos;
                break;
            case SAI_MIRROR_SESSION_ATTR_TTL:
                attr_list[i].value.u8 = md.ttl;
                break;
            case SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS:
                if (md.src_addr)
                {
                    attr_list[i].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
                    attr_list[i].value.ipaddr.addr.ip4 = htonl(md.src_addr);
                }
                else
                {
                    attr_list[i].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
                    sal_memcpy(attr_list[i].value.ipaddr.addr.ip6, md.src6_addr,
                               sizeof(md.src6_addr));
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS:
                if (md.dst_addr)
                {
                    attr_list[i].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
                    attr_list[i].value.ipaddr.addr.ip4 = htonl(md.dst_addr);
                }
                else
                {
                    attr_list[i].value.ipaddr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
                    sal_memcpy(attr_list[i].value.ipaddr.addr.ip6, md.dst6_addr,
                               sizeof(md.dst6_addr));
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS:
                sal_memcpy(attr_list[i].value.mac, md.src_mac, sizeof(bcm_mac_t));
                break;
            case SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS:
                sal_memcpy(attr_list[i].value.mac, md.dst_mac, sizeof(bcm_mac_t));
                break;
            case SAI_MIRROR_SESSION_ATTR_GRE_PROTOCOL_TYPE:
                attr_list[i].value.u16 = md.gre_protocol;
                break;
            default:
                BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR,
                                    "Unknown mirror session attribute %d passed\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_INFO,
                                "Error processing mirror session attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_MIRROR);

    return rv;
}

/*
################################################################################
#                              Internal functions                              #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_mirror()
{
    int i;
    sai_status_t rv;
    bcm_mirror_destination_t md;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_MIRROR);

    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_MIRROR_SESSION,
                                     _BRCM_SAI_MAX_MIRROR_SESSIONS);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "init mirror session state", rv);

    if (_brcm_sai_switch_wb_state_get())
    {
        for (i=0; i<_BRCM_SAI_MAX_MIRROR_SESSIONS; i++)
        {
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                            &i, &data);
            BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data get", rv);
            if (data.ms.gport && 0 == data.ms.ref_count)
            {
                bcm_mirror_destination_t_init(&md);
                md.mirror_dest_id = data.ms.destid;
                md.flags = data.ms.flags | BCM_MIRROR_DEST_REPLACE|BCM_MIRROR_DEST_WITH_ID;
                md.gport = data.ms.gport;
                sal_memcpy(&md.src_mac, &data.ms.src_mac, sizeof(bcm_mac_t));
                sal_memcpy(&md.dst_mac, &data.ms.dst_mac, sizeof(bcm_mac_t));
                md.tpid = data.ms.tpid;
                md.vlan_id = data.ms.vlan_id;
                md.version = data.ms.version;
                md.tos = data.ms.tos;
                md.ttl = data.ms.ttl;
                md.src_addr = data.ms.src_addr;
                md.dst_addr = data.ms.dst_addr;
                sal_memcpy(&md.src6_addr, &data.ms.src6_addr, sizeof(bcm_ip6_t));
                sal_memcpy(&md.dst6_addr, &data.ms.dst6_addr, sizeof(bcm_ip6_t));
                md.gre_protocol = data.ms.gre_protocol;
                rv = bcm_mirror_destination_create(0, &md);
                BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror dest create", rv);
            }
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_MIRROR);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_mirror()
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_MIRROR);

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                      0, _BRCM_SAI_MAX_MIRROR_SESSIONS, -1);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "freeing mirror session state", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_MIRROR);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_update_acl_mirror_session(int unit,
                                    bcm_field_group_t group,
                                    void *user_data)
{
    uint32 param0, param1;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_field_entry_t *entry_array, entry;
    int idx, entry_count, entry_num, alloc_sz;
    uint32 mirror_session;

    /*
     *  This routine is invoked for every group.
     *  
     *  1. For each group get entry count
     *  2. For all entries get ingress and egress mirror action session id
     *     a. If success then match the session id with the provided session id.
     *     b. If match then reinstall entry
     */

    rv = bcm_field_entry_multi_get(unit, group, 0, NULL, &entry_num);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "field entry multi get", rv);

    if (entry_num) 
    {
        alloc_sz = sizeof(bcm_field_entry_t) * entry_num;
        entry_array = ALLOC(alloc_sz);
        if (NULL == entry_array) 
        {
            return SAI_STATUS_NO_MEMORY;
        }
        rv = bcm_field_entry_multi_get(unit, group, entry_num,
                                       entry_array, &entry_count);
        BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field entry multi get", rv, entry_array);

        mirror_session = (uint32)(uint64_t)user_data;
        for (idx = 0; idx < entry_num; idx++) 
        {
            entry = entry_array[idx];
            rv = bcm_field_action_get(0, entry,
                                      bcmFieldActionMirrorIngress, &param0, &param1);
            if (BCM_FAILURE(rv) && (BCM_E_NOT_FOUND != rv))
            {
                BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field action get", rv, entry_array);
            }
            else if ((!BCM_FAILURE(rv)) && (mirror_session == param1))
            {
                rv = bcm_field_action_remove(0, entry, bcmFieldActionMirrorIngress);
                BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field action remove", rv, entry_array);
                rv = bcm_field_action_add(0, entry, bcmFieldActionMirrorIngress, param0, param1);
                BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field action add", rv, entry_array);
                rv = bcm_field_entry_reinstall(0, entry);
                BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field entry reinstall", rv, entry_array);
            }
            else if (BCM_E_NOT_FOUND == rv)
            {
                rv = bcm_field_action_get(0, entry,
                                          bcmFieldActionMirrorEgress, &param0, &param1);
                if (rv && BCM_E_NOT_FOUND != rv)
                {
                    BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field action get", rv, entry_array);
                }
                else if ((BCM_E_NONE == rv) && (mirror_session == param1))
                {
                    rv = bcm_field_action_remove(0, entry, bcmFieldActionMirrorEgress);
                    BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field action remove", rv, entry_array);
                    rv = bcm_field_action_add(0, entry, bcmFieldActionMirrorEgress, param0, param1);
                    BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field action add", rv, entry_array);
                    rv = bcm_field_entry_reinstall(0, entry);
                    BRCM_SAI_API_CHK_FREE(SAI_API_MIRROR, "field entry reinstall", rv, entry_array);
                }
            }
        }
        CHECK_FREE(entry_array);
    }
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Create mirror session.
 *
 * @param[out] session_id Port mirror session id
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Value of attributes
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
sai_status_t
_brcm_sai_create_mirror_session(_Out_ sai_object_id_t *session_id,
                                _In_  uint32_t attr_count,
                                _In_  const sai_attribute_t *attr_list)
{
    int i, idx, mode = -1;
    bcm_mirror_destination_t md;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    uint16 vlan_priority = 0, vlan_cfi = 0;
    bool smac = FALSE, dmac = FALSE, erspan_vlan_enable = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_MIRROR);

    bcm_mirror_destination_t_init(&md);
    md.gport = -1;
    
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_MIRROR_SESSION_ATTR_TYPE:
                mode = attr_list[i].value.s32;
                if (SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE < mode)
                {
                    rv = SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_MONITOR_PORT:
                if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_ATTR_LIST_OBJ_TYPE(i)) /* Port */
                {   
                    rv = bcm_port_gport_get(0, BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_port_t, i), 
                                            &md.gport);
                }
                else if (SAI_OBJECT_TYPE_LAG == BRCM_SAI_ATTR_LIST_OBJ_TYPE(i))
                {
                    BCM_GPORT_TRUNK_SET(md.gport, 
                                        BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_port_t, i));
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_TRUNCATE_SIZE:
            case SAI_MIRROR_SESSION_ATTR_TC:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_TPID:
                md.tpid = attr_list[i].value.u16;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_ID:
                md.vlan_id = attr_list[i].value.u16;
                if (0 == md.vlan_id)
                {
                    BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Invalid vid=0 specified.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_PRI:
                vlan_priority = (attr_list[i].value.u8 & 0x7) << 13;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_CFI:
                vlan_cfi = (attr_list[i].value.u8 & 0x1) << 12;
                break;
            case SAI_MIRROR_SESSION_ATTR_VLAN_HEADER_VALID:
                erspan_vlan_enable = attr_list[i].value.booldata;
                break;
            case SAI_MIRROR_SESSION_ATTR_ERSPAN_ENCAPSULATION_TYPE:
                if (SAI_ERSPAN_ENCAPSULATION_TYPE_MIRROR_L3_GRE_TUNNEL != attr_list[i].value.s32)
                {
                    rv = SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION:
                md.version = attr_list[i].value.u8;
                break;
            case SAI_MIRROR_SESSION_ATTR_TOS:
                md.tos = attr_list[i].value.u16;
                break;
            case SAI_MIRROR_SESSION_ATTR_TTL:
                md.ttl = attr_list[i].value.u8;
                break;
            case SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS:
                if (SAI_IP_ADDR_FAMILY_IPV4 ==
                    attr_list[i].value.ipaddr.addr_family)
                {

                    md.src_addr =
                        ntohl(attr_list[i].value.ipaddr.addr.ip4);
                }
                else if (SAI_IP_ADDR_FAMILY_IPV6 ==
                         attr_list[i].value.ipaddr.addr_family)
                {
                    sal_memcpy(md.src6_addr,
                               attr_list[i].value.ipaddr.addr.ip6,
                               sizeof(md.src6_addr));
                }
                else
                {
                    BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Invalid IP address family.\n");
                    rv = SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS:
                if (SAI_IP_ADDR_FAMILY_IPV4 ==
                    attr_list[i].value.ipaddr.addr_family)
                {

                    md.dst_addr =
                        ntohl(attr_list[i].value.ipaddr.addr.ip4);
                }
                else if (SAI_IP_ADDR_FAMILY_IPV6 ==
                         attr_list[i].value.ipaddr.addr_family)
                {
                    sal_memcpy(md.dst6_addr,
                               attr_list[i].value.ipaddr.addr.ip6,
                               sizeof(md.dst6_addr));
                }
                else
                {
                    BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Invalid IP address family.\n");
                    rv = SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            case SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS:
                sal_memcpy(md.src_mac, attr_list[i].value.mac, sizeof(bcm_mac_t));
                smac = TRUE;
                break;
            case SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS:
                sal_memcpy(md.dst_mac, attr_list[i].value.mac, sizeof(bcm_mac_t));
                dmac = TRUE;
                break;
            case SAI_MIRROR_SESSION_ATTR_GRE_PROTOCOL_TYPE:
                md.gre_protocol = attr_list[i].value.u16;
                break;
            default:
                BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Unknown or unimplemented attribute passed\n");
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR,
                                "Error processing mirror attributes\n");
            return rv;
        }
    }
    if (-1 == mode || -1 == md.gport)
    {
        BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Mandatory attribs not found.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (SAI_MIRROR_SESSION_TYPE_REMOTE == mode && 0 == md.vlan_id)
    {
        BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Mandatory attribs not found.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    else if ((SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE == mode) && 
             (FALSE == smac || FALSE == dmac || 0 == md.gre_protocol))
    {
        BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Mandatory attribs not found.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (SAI_MIRROR_SESSION_TYPE_REMOTE == mode)
    {
        md.flags |= BCM_MIRROR_DEST_TUNNEL_L2;
    }
    else if (SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE == mode)
    {
        if (TRUE == erspan_vlan_enable && 0 == md.vlan_id)
        {
            BRCM_SAI_LOG_MIRROR(SAI_LOG_LEVEL_ERROR, "Mandatory attribs not found.\n");
            return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
        }
        else if (FALSE == erspan_vlan_enable)
        {
            md.vlan_id = 0;
        }
        md.flags |= BCM_MIRROR_DEST_TUNNEL_IP_GRE;
        /* Set the Don't Fragment bit to '1' by default. */
        md.df = 1;
    }
    md.vlan_id |= vlan_priority | vlan_cfi;
    rv = bcm_mirror_destination_create(0, &md);
    BRCM_SAI_API_CHK(SAI_API_MIRROR, "mirror dest create", rv);
    *session_id = BRCM_SAI_CREATE_OBJ_SUB(SAI_OBJECT_TYPE_MIRROR_SESSION, mode,
                                          md.mirror_dest_id);
    idx = md.mirror_dest_id & 0xff;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data get", rv);
    data.ms.idx = idx;
    data.ms.destid = md.mirror_dest_id;
    data.ms.flags = md.flags;
    data.ms.gport = md.gport;
    sal_memcpy(&data.ms.src_mac, &md.src_mac, sizeof(bcm_mac_t));
    sal_memcpy(&data.ms.dst_mac, &md.dst_mac, sizeof(bcm_mac_t));
    data.ms.tpid = md.tpid;
    data.ms.vlan_id = md.vlan_id;
    data.ms.version = md.version;
    data.ms.tos = md.tos;
    data.ms.ttl = md.ttl;
    data.ms.src_addr = md.src_addr;
    data.ms.dst_addr = md.dst_addr;
    sal_memcpy(&data.ms.src6_addr, &md.src6_addr, sizeof(bcm_ip6_t));
    sal_memcpy(&data.ms.dst6_addr, &md.dst6_addr, sizeof(bcm_ip6_t));
    data.ms.gre_protocol = md.gre_protocol;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_MIRROR);

    return rv;
}

sai_status_t
_brcm_sai_mirror_ref_update(int unit, int id, bool add)
{
    int idx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    idx = id & 0xff;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data get", rv);
    data.ms.ref_count = add ? data.ms.ref_count+1 : data.ms.ref_count-1;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_MIRROR_SESSION, 
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_MIRROR, "mirror session data set", rv);
    return rv;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_mirror_api_t mirror_apis = {
    brcm_sai_create_mirror_session,
    brcm_sai_remove_mirror_session,
    brcm_sai_set_mirror_session_attribute,
    brcm_sai_get_mirror_session_attribute
};
