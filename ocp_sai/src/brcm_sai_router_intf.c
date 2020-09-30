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
#                                Local state                                   #
################################################################################
*/
static int max_rif = 0;
static int max_tunnel_rif = 0;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC void
_brcm_sai_rif_info_set(sai_uint32_t rif_id, int vrf, int station_id,
                       sai_object_id_t vid_obj, int nb_mis_pa);
STATIC void
_brcm_sai_rif_tunnel_info_set(sai_uint32_t rif_id, int vrf, int station_id);

STATIC sai_status_t
_brcm_sai_rif_tunnel_info_get(sai_uint32_t rif_id, int *vrf, int *station_id);

/*
################################################################################
#                        Router interface functions                            #
################################################################################
*/
/*
* Routine Description:
*    Create router interface.
*
* Arguments:
*    [out] rif_id - router interface id
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_router_interface(_Out_ sai_object_id_t* rif_id,
                                 _In_ sai_object_id_t switch_id,
                                 _In_ sai_uint32_t attr_count,
                                 _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv;
    bcm_port_t port;
    bcm_vrf_t vrf = 0;
    bcm_trunk_t tid = 0;
    bcm_pbmp_t pbm, upbm;
    bcm_l3_intf_t l3_intf;
    bcm_l2_station_t l2_stn;
    sai_object_id_t vid_obj;
    int i, p, station_id = 0;
    bool imac = FALSE, lag = FALSE;
    _brcm_sai_indexed_data_t idata;
    sai_router_interface_type_t type = -1;
    int port_count = 0, nb_mis_pa = SAI_PACKET_ACTION_TRAP;
    bcm_mac_t dst_mac_mask = {0xff,0xff, 0xff, 0xff, 0xff, 0xff};

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTER_INTERFACE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(rif_id);

    for (i=0; i<attr_count; i++)
    {
        if (SAI_ROUTER_INTERFACE_ATTR_TYPE == attr_list[i].id)
        {
            type = attr_list[i].value.u32;
            break;
        }
    }
    if (-1 == type)
    {
        BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "No interface type.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (SAI_ROUTER_INTERFACE_TYPE_SUB_PORT == type || 
        SAI_ROUTER_INTERFACE_TYPE_BRIDGE == type)
    {
        BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Un-implemented interface types.\n");
        return SAI_STATUS_NOT_IMPLEMENTED;
    }
    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_mtu = _BRCM_SAI_RIF_DEFAULT_MTU;
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ROUTER_INTERFACE_ATTR_TYPE:
                break;
            case SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID:
            {
                vrf = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_vrf_t, i);
                if (vrf && false == _brcm_sai_vrf_valid(vrf))
                {
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Invalid vrf value.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                l3_intf.l3a_vrf = vrf;
                break;
            }
            case SAI_ROUTER_INTERFACE_ATTR_PORT_ID:
                if (SAI_ROUTER_INTERFACE_TYPE_PORT != type)
                {
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Type value mismatch.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                BCM_PBMP_CLEAR(pbm);
                BCM_PBMP_CLEAR(upbm);
                if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_ATTR_LIST_OBJ_TYPE(i)) /* Port */
                {
                    port_count = 1;
                    port = BRCM_SAI_ATTR_LIST_OBJ(i);
                    BCM_PBMP_PORT_ADD(pbm, port);
                    BCM_PBMP_PORT_ADD(upbm, port);
                }
                else /* LAG */
                {
                    int max;
                    bcm_trunk_member_t *members;
                    bcm_trunk_info_t trunk_info;
                    bcm_trunk_chip_info_t trunk_chip_info;

                    type = _BRCM_SAI_RIF_TYPE_LAG;
                    tid = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_trunk_t, i);
                    rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
                    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "trunk chip info get", rv);
                    max = trunk_chip_info.trunk_group_count;
                    members = ALLOC(sizeof(bcm_trunk_member_t) * max);
                    if (IS_NULL(members))
                    {
                        BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_CRITICAL,
                                           "Error allocating memory for lag members.\n");
                        return SAI_STATUS_NO_MEMORY;
                    }
                    rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &port_count);
                    BRCM_SAI_API_CHK_FREE(SAI_API_ROUTER_INTERFACE, "trunk get", rv,
                                          members);
                    lag = TRUE;
                    if (port_count)
                    {
                        for (p=0; p<port_count; p++)
                        {
                            /* Add port(s) to vlan */
                            rv = bcm_port_local_get(0, members[p].gport, &port);
                            BRCM_SAI_API_CHK_FREE(SAI_API_ROUTER_INTERFACE,
                                                  "port local get", rv, members);
                            BCM_PBMP_PORT_ADD(pbm, port);
                            BCM_PBMP_PORT_ADD(upbm, port);
                        }
                    }
                    CHECK_FREE(members);
                }
                /* For port and lag interaces, get an unused vlan */
                {
                    sai_attribute_t vlan_attr_list = { SAI_VLAN_ATTR_VLAN_ID };

                    rv = _brcm_sai_get_max_unused_vlan_id(&l3_intf.l3a_vid);
                    BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "max unused vlan get", rv);
                    if (0 == l3_intf.l3a_vid)
                    {
                        BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "No free vlans found\n");
                        return SAI_STATUS_FAILURE;
                    }
                    /* Create vlan */
                    vlan_attr_list.value.u16 = l3_intf.l3a_vid;
                    rv = vlan_apis.create_vlan(&vid_obj, 0, 1, &vlan_attr_list);
                    if (SAI_STATUS_ERROR(rv))
                    {
                        BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR,
                                           "Creating new vlan %d failed\n",
                                           (int)l3_intf.l3a_vid);
                        return rv;
                    }
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_DEBUG,
                                       "Allocated new vlan %d for %s intf.\n",
                                       (int)l3_intf.l3a_vid, lag ? "lag" : "port");
                }
                if (port_count)
                {
                    /* Add to new vlan */
                    rv = bcm_vlan_port_add(0, l3_intf.l3a_vid, pbm, upbm);
                    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "vlan port add", rv);
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_DEBUG,
                                       "Added port(s) to new vlan %d\n",
                                       (int)l3_intf.l3a_vid);
                    BCM_PBMP_ITER(pbm, p)
                    {
                        rv = bcm_port_untagged_vlan_set(0, p, l3_intf.l3a_vid);
                        BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "pvlan set", rv);
                        /* Disable learning on port */
                        rv = bcm_port_learn_set(0, p, BCM_PORT_LEARN_FWD);
                        BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "port L2 learn set", rv);
                        rv = bcm_port_control_set(0, p, bcmPortControlL2Move, 
                                                  BCM_PORT_LEARN_FWD);
                        BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "port L2 move set", rv);
                    }
                }
                break;
            case SAI_ROUTER_INTERFACE_ATTR_VLAN_ID:
                if (SAI_ROUTER_INTERFACE_TYPE_VLAN != type)
                {
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Type value mismatch.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                vid_obj = attr_list[i].value.oid;
                l3_intf.l3a_vid = BRCM_SAI_GET_OBJ_VAL(int, vid_obj);
                break;
            case SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS:
                memcpy(l3_intf.l3a_mac_addr, attr_list[i].value.mac,
                       sizeof(l3_intf.l3a_mac_addr));
                imac = TRUE;
                break;
            case SAI_ROUTER_INTERFACE_ATTR_NEIGHBOR_MISS_PACKET_ACTION:
                nb_mis_pa = attr_list[i].value.s32;
                break;
            case SAI_ROUTER_INTERFACE_ATTR_MTU:
                if (_BRCM_SAI_RIF_MAX_MTU < attr_list[i].value.u32)
                {
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Invalid MTU size\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                l3_intf.l3a_mtu = attr_list[i].value.u32;
                break;
            default:
                BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_INFO,
                                   "Unknown attribute %d passed\n",
                                   attr_list[i].id);
                break;
        }
    }
    if (SAI_ROUTER_INTERFACE_TYPE_PORT == type && 0 == port_count)
    {
        BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "No port id for port interface.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (FALSE == imac)
    {
        if (0 == vrf)
        {
            if ((rv = _brcm_sai_system_mac_get(&l3_intf.l3a_mac_addr))
                != SAI_STATUS_SUCCESS)
            {
                BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Error retreiving system mac.\n");
                return rv;
            }
        }
        else if (_brcm_sai_vrf_info_get(vrf, (sai_mac_t*)l3_intf.l3a_mac_addr) < 0)
        {
            BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Error retreiving VR mac.\n");
            return SAI_STATUS_ITEM_NOT_FOUND;
        }
    }
    bcm_l2_station_t_init(&l2_stn);
    l2_stn.flags = BCM_L2_STATION_IPV4 | BCM_L2_STATION_IPV6 | BCM_L2_STATION_ARP_RARP;
    sal_memcpy(l2_stn.dst_mac, l3_intf.l3a_mac_addr, sizeof(l2_stn.dst_mac));
    sal_memcpy(l2_stn.dst_mac_mask, dst_mac_mask, sizeof(dst_mac_mask));
    if (SAI_ROUTER_INTERFACE_TYPE_LOOPBACK == type)
    {
        int index;
        /* Reserve an unused entry */
        rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO, 1,
                                                  max_tunnel_rif, &index);
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR,
                               "Unexpected rif tunnel resource issue.\n");
            return rv;
        }
        rv = _brcm_sai_l2_station_add(0, &l2_stn, &station_id);
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Error adding my stn entry.\n");
            (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO,
                                                    index);
            return BRCM_RV_BCM_TO_SAI(rv);
        }
        _brcm_sai_rif_tunnel_info_set(index, vrf, station_id);
        *rif_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_ROUTER_INTERFACE,
                                              type, vrf, index);
        /* Skip creating any physical intf */
    }
    else
    {
        l3_intf.l3a_ttl = _BRCM_SAI_VR_DEFAULT_TTL;
        rv = bcm_l3_intf_create(0, &l3_intf);
        BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf create", rv);
        l2_stn.vlan = l3_intf.l3a_vid;
        l2_stn.vlan_mask = 0xfff;
        /* Note: Currently only matching vlan (for port, vlan intf) and trunk
         *       (for lag intf) values. Can match port as well in the future.
         *       And match trunk for vlan-trunk intf as well.
         *       Need a way to detect vlan-lag intf.
         */
        if (lag)
        {
            BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_DEBUG,
                               "Add my station for vid %d, tid %d\n",
                               l3_intf.l3a_vid, tid);
        }
        else
        {
            BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_DEBUG,
                               "Add my station for vid %d\n", l3_intf.l3a_vid);
        }
        rv = _brcm_sai_l2_station_add(tid, &l2_stn, &station_id);
        BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "Add my stn entry", rv);
        _brcm_sai_rif_info_set(l3_intf.l3a_intf_id, vrf, station_id, vid_obj,
                               nb_mis_pa);
        *rif_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_ROUTER_INTERFACE, type,
                                             ((SAI_ROUTER_INTERFACE_TYPE_VLAN == type) ?
                                               l3_intf.l3a_vid : (lag ? tid : port)),
                                               l3_intf.l3a_intf_id);

        /* Add DB entry */
        if (SAI_ROUTER_INTERFACE_TYPE_PORT == type)
        {
            DATA_CLEAR(idata.port_rif, _brcm_sai_port_rif_t);
            idata.port_rif.idx = port;
            idata.port_rif.rif_obj = *rif_id;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_RIF_TABLE,
                                            &port, &idata);
            BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "port rif data set", rv);
        }
        else if (SAI_ROUTER_INTERFACE_TYPE_VLAN == type)
        {
            int vlan;

            vlan = l3_intf.l3a_vid;
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                            &vlan, &idata);
            BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "vlan rif data get", rv);
            idata.vlan_rif.idx = vlan;
            idata.vlan_rif.rif_obj = *rif_id;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                            &vlan, &idata);
            BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "vlan rif data set", rv);
        }
        else
        {
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &tid, &idata);
            BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "lag rif data get", rv);
            idata.lag_info.rif_obj = *rif_id;
            idata.lag_info.vid = l3_intf.l3a_vid;
            idata.lag_info.internal = TRUE;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                            &tid, &idata);
            BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "lag rif data set", rv);
        }
        /* Add mac to arp trap */
        rv = _brcm_sai_arp_trap_add(0, -1, -1, 1, &l2_stn.dst_mac);   
        BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "arp trap add", rv);
    }
    rv = _brcm_sai_vrf_ref_count_update(vrf, INC);
    BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "vrf refcount inc", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTER_INTERFACE);
    return rv;
}

/*
* Routine Description:
*    Remove router interface
*
* Arguments:
*    [in] rif_id - router interface id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_router_interface(_In_ sai_object_id_t rif_id)
{
    int vrf = 0;
    sai_status_t rv;
    bcm_l2_station_t l2_stn;
    sai_object_id_t vid_obj;
    int station_id, port_count = 0;
    sai_router_interface_type_t type;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTER_INTERFACE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(rif_id, SAI_OBJECT_TYPE_ROUTER_INTERFACE))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         rif_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    type = BRCM_SAI_GET_OBJ_SUB_TYPE(rif_id);
    if (SAI_ROUTER_INTERFACE_TYPE_LOOPBACK == type)
    {
        if (SAI_STATUS_SUCCESS == 
            _brcm_sai_rif_tunnel_info_get(BRCM_SAI_GET_OBJ_VAL(bcm_if_t, rif_id),
                                          &vrf, &station_id))
        {
            if (station_id != 0)
            {
                /* station id 0 means dup entry */
                rv = bcm_l2_station_delete(0, station_id);
                BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "Delete my stn entry", rv);
            }
        }
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO, 
                  BRCM_SAI_GET_OBJ_VAL(bcm_if_t, rif_id));
        
    }
    else
    {
        bcm_port_t port;
        bcm_l3_intf_t l3_intf;
        bcm_pbmp_t pbm, upbm;

        bcm_l3_intf_t_init(&l3_intf);
        l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, rif_id);
        
        if (SAI_STATUS_SUCCESS == 
            _brcm_sai_rif_info_get(l3_intf.l3a_intf_id, &vrf, &station_id,
                                   &vid_obj, NULL))
        {
            rv = bcm_l2_station_get(0, station_id, &l2_stn);
            BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "Get my stn entry", rv);
            rv = bcm_l2_station_delete(0, station_id);
            BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "Delete my stn entry", rv);
        }
        rv = bcm_l3_intf_delete(0, &l3_intf);
        BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf delete", rv);
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_RIF_INFO, 
                                                l3_intf.l3a_intf_id);

        if (type != SAI_ROUTER_INTERFACE_TYPE_VLAN)        
        {
            int p;

            rv = vlan_apis.remove_vlan(vid_obj);
            BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "remove vlan", rv);
            BCM_PBMP_CLEAR(pbm);
            BCM_PBMP_CLEAR(upbm);
            if (SAI_ROUTER_INTERFACE_TYPE_PORT == type)
            {
                port_count = 1;
                port = BRCM_SAI_GET_OBJ_MAP(rif_id);
                BCM_PBMP_PORT_ADD(pbm, port);
                BCM_PBMP_PORT_ADD(upbm, port);
                _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_PORT_RIF_TABLE, port);
            }
            else /* (_BRCM_SAI_RIF_TYPE_LAG == type) */
            {
                int max;
                bcm_trunk_t tid;
                bcm_trunk_member_t *members;
                bcm_trunk_info_t trunk_info;
                bcm_trunk_chip_info_t trunk_chip_info;
            
                rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
                BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "trunk chip info get", rv);
                max = trunk_chip_info.trunk_group_count;
                members = ALLOC(sizeof(bcm_trunk_member_t) * max);
                if (IS_NULL(members))
                {
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_CRITICAL,
                                       "Error allocating memory for lag members.\n");
                    return SAI_STATUS_NO_MEMORY;
                }
                tid = BRCM_SAI_GET_OBJ_MAP(rif_id);
                rv = bcm_trunk_get(0, tid, &trunk_info, max, members, &port_count);
                BRCM_SAI_API_CHK_FREE(SAI_API_ROUTER_INTERFACE, "trunk get", rv, members);
                for (p=0; p<port_count; p++)
                {
                    rv = bcm_port_local_get(0, members[p].gport, &port);
                    BRCM_SAI_API_CHK_FREE(SAI_API_ROUTER_INTERFACE, "port local get", rv,
                                          members);
                    BCM_PBMP_PORT_ADD(pbm, port);
                    BCM_PBMP_PORT_ADD(upbm, port);
                }
                CHECK_FREE(members);
                _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_LAG_INFO_TABLE, tid);
            }
            if (port_count)
            {
                BCM_PBMP_ITER(pbm, p)
                {
                    /* Enable learning on port */
                    rv = bcm_port_learn_set(0, p, BCM_PORT_LEARN_ARL | 
                                            BCM_PORT_LEARN_FWD);
                    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "port L2 learn set", rv);
                    rv = bcm_port_control_set(0, p, bcmPortControlL2Move, 
                                              BCM_PORT_LEARN_ARL | 
                                              BCM_PORT_LEARN_FWD);
                    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "port L2 move set", rv);
                }
            }
        }
        else
        {
            _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                              BRCM_SAI_GET_OBJ_MAP(rif_id));
        }
        /* remove mac from arp trap */
        rv = _brcm_sai_ucast_arp_trap_entry_del(0, 0, 1, &l2_stn.dst_mac);   
        BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "arp trap entry del", rv);
        rv = _brcm_sai_ucast_arp_trap_entry_del(0, 1, 1, &l2_stn.dst_mac);   
        BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "arp trap entry del", rv);
    }
    rv = _brcm_sai_vrf_ref_count_update(vrf, DEC);
    BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "vrf refcount dec", rv);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTER_INTERFACE);
    return rv;
}

/*
* Routine Description:
*    Set router interface attribute
*
* Arguments:
*    [in] sai_router_interface_id_t - router_interface_id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_router_interface_attribute(_In_ sai_object_id_t rif_id,
                                        _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_FAILURE;
    bcm_l3_intf_t l3_intf;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTER_INTERFACE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(rif_id, SAI_OBJECT_TYPE_ROUTER_INTERFACE))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         rif_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, rif_id);
    rv = bcm_l3_intf_get(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf get", rv);
    switch (attr->id)
    {
        case SAI_ROUTER_INTERFACE_ATTR_MTU:
            if (_BRCM_SAI_RIF_MAX_MTU < attr->value.u32)
            {
                BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Invalid MTU size\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            l3_intf.l3a_mtu = attr->value.u32;
            break;
        default:
            BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_INFO,
                              "Unknown or unsupported rif attribute %d passed\n",
                              attr->id);
            return SAI_STATUS_INVALID_PARAMETER;
    }
    l3_intf.l3a_flags |= (BCM_L3_REPLACE | BCM_L3_WITH_ID);
    rv = bcm_l3_intf_create(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf create", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTER_INTERFACE);

    return rv;
}

/*
* Routine Description:
*    Get router interface attribute
*
* Arguments:
*    [in] sai_router_interface_id_t - router_interface_id
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_router_interface_attribute(_In_ sai_object_id_t rif_id,
                                        _In_ sai_uint32_t attr_count,
                                        _Inout_ sai_attribute_t *attr_list)
{
    sai_status_t rv;
    bcm_l3_intf_t l3_intf;
    int i, type, nb_mis_pa;
    sai_object_id_t vid_obj;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTER_INTERFACE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(rif_id, SAI_OBJECT_TYPE_ROUTER_INTERFACE))
    {
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                         "Invalid object type 0x%16lx passed\n",
                         rif_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, rif_id);
    type = BRCM_SAI_GET_OBJ_SUB_TYPE(rif_id);
    rv = bcm_l3_intf_get(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf get", rv);
    rv = _brcm_sai_rif_info_get(l3_intf.l3a_intf_id, NULL, NULL,
                                &vid_obj, &nb_mis_pa);
    BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "rif info get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ROUTER_INTERFACE_ATTR_TYPE:
                attr_list[i].value.u32 = (_BRCM_SAI_RIF_TYPE_LAG == type) ?
                                         SAI_ROUTER_INTERFACE_TYPE_PORT : type;
                break;
            case SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VIRTUAL_ROUTER,
                                        l3_intf.l3a_vrf);
                break;
            case SAI_ROUTER_INTERFACE_ATTR_PORT_ID:
                if (!((SAI_ROUTER_INTERFACE_TYPE_PORT == type) ||
                      (_BRCM_SAI_RIF_TYPE_LAG == type)))
                {
                    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_ERROR, "Type value mismatch.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                if (SAI_ROUTER_INTERFACE_TYPE_PORT == type)
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) =
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT , BRCM_SAI_GET_OBJ_MAP(rif_id));
                }
                else
                {
                    BRCM_SAI_ATTR_LIST_OBJ(i) =
                        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_LAG , BRCM_SAI_GET_OBJ_MAP(rif_id));
                }
                break;
            case SAI_ROUTER_INTERFACE_ATTR_VLAN_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = vid_obj;
                break;
            case SAI_ROUTER_INTERFACE_ATTR_SRC_MAC_ADDRESS:
                memcpy(attr_list[i].value.mac, l3_intf.l3a_mac_addr,
                       sizeof(l3_intf.l3a_mac_addr));
                break;
            case SAI_ROUTER_INTERFACE_ATTR_NEIGHBOR_MISS_PACKET_ACTION:
                attr_list[i].value.s32 = nb_mis_pa;
                break;
            case SAI_ROUTER_INTERFACE_ATTR_MTU:
                 attr_list[i].value.u32 = l3_intf.l3a_mtu;
                break;
            default:
                BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_INFO,
                                  "Unknown or unsupported rif attribute %d passed\n",
                                  attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTER_INTERFACE);

    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
/* Routine to allocate rif state */
sai_status_t
_brcm_sai_alloc_rif()
{
    int max;
    sai_status_t rv;

    rv = _brcm_sai_l3_config_get(7, &max);
    if (BCM_FAILURE(rv) || (0 == max))
    {
        BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_CRITICAL,
                           "Error %d retreiving max rif !!\n", rv);
        return SAI_STATUS_FAILURE;
    }
    max_rif = max;
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_RIF_INFO, max_rif);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "initializing rif state", rv);
    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_INFO, "Max rifs: %d.\n", max_rif);
    
    rv = _brcm_sai_l3_config_get(3, &max_tunnel_rif);
    BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "l3 ifo", rv);
    max_tunnel_rif = max_tunnel_rif + 1;
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO,
                                     max_tunnel_rif);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "initializing tunnel rif state", rv);
    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_INFO, "Max tunnel rifs: %d.\n",
                       max_tunnel_rif);

    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_PORT_RIF_TABLE,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "initializing port rif table", rv);
    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_INFO, "Max port rifs: %d.\n", 
                       _BRCM_SAI_MAX_PORTS);

    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                     _BRCM_SAI_VR_MAX_VID);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "initializing vlan rif table", rv);
    BRCM_SAI_LOG_RINTF(SAI_LOG_LEVEL_INFO, "Max vlan rifs: %d.\n", 
                       _BRCM_SAI_VR_MAX_VID);
    
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_rif()
{
    sai_status_t rv;
    
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_RIF_INFO, 
                                      1, max_rif, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "freeing rif state", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO, 
                                      1, max_tunnel_rif, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "freeing rif tunnel state", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_PORT_RIF_TABLE, 
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "freeing port rif state", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE, 
                                      1, _BRCM_SAI_VR_MAX_VID, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_ROUTER_INTERFACE, SAI_LOG_LEVEL_CRITICAL,
                        "freeing vlan rif state", rv);
    return SAI_STATUS_SUCCESS;
}

STATIC void
_brcm_sai_rif_info_set(sai_uint32_t rif_id, int vrf, int station_id,
                       sai_object_id_t vid_obj, int nb_mis_pa)
{
    _brcm_sai_indexed_data_t data;
    
    (void)_brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_RIF_INFO, (int *)&rif_id,
                                     &data);
    data.rif_info.idx = rif_id;
    data.rif_info.valid = TRUE;
    data.rif_info.vr_id = vrf;
    data.rif_info.vid_obj = vid_obj;
    data.rif_info.station_id = station_id;
    data.rif_info.nb_mis_pa = nb_mis_pa;
    (void)_brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_RIF_INFO, (int *)&rif_id,
                                     &data);
}

void
_brcm_sai_vlan_rif_info_set(sai_uint32_t vid, bool val, _brcm_sai_nbr_t *nbr)
{
    _brcm_sai_indexed_data_t data;

    (void)_brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE, (int *)&vid,
                                     &data);
    data.vlan_rif.bcast_ip = val;
    if (TRUE == val)
    {
        data.vlan_rif.nbr = *nbr;
    }
    else
    {
        DATA_CLEAR(data.vlan_rif.nbr, _brcm_sai_nbr_t);
    }
    (void)_brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE, (int *)&vid,
                                     &data);
}

sai_status_t
_brcm_sai_rif_info_get(sai_uint32_t rif_id, int *vrf, int *station_id,
                       sai_object_id_t *vid_obj, int *nb_mis_pa)
{
    _brcm_sai_indexed_data_t data;
    
    (void)_brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_RIF_INFO, (int *)&rif_id,
                                     &data);
    if (!data.rif_info.valid)
    {
        return SAI_STATUS_FAILURE;
    }
    if (vrf)
    {
        *vrf = data.rif_info.vr_id;
    }
    if (station_id)
    {
        *station_id = data.rif_info.station_id;
    }
    if (vid_obj)
    {
        *vid_obj = data.rif_info.vid_obj;
    }
    if (nb_mis_pa)
    {
        *nb_mis_pa = data.rif_info.nb_mis_pa;
    }
    return SAI_STATUS_SUCCESS;
}

STATIC void
_brcm_sai_rif_tunnel_info_set(sai_uint32_t rif_id, int vrf, int station_id)
{
    _brcm_sai_indexed_data_t data;

    (void)_brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO,
                                     (int *)&rif_id, &data);
    data.trif_info.idx = rif_id;
    data.trif_info.valid = TRUE;
    data.trif_info.vr_id = vrf;
    data.trif_info.station_id = station_id;
    (void)_brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO,
                                     (int *)&rif_id, &data);
}

STATIC sai_status_t
_brcm_sai_rif_tunnel_info_get(sai_uint32_t rif_id, int *vrf, int *station_id)
{
    _brcm_sai_indexed_data_t data;
    
    (void)_brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_RIF_TUNNEL_INFO,
                                     (int *)&rif_id, &data);
    
    if (!data.trif_info.valid)
    {
        return SAI_STATUS_FAILURE;
    }
    if (vrf)
    {
        *vrf = data.trif_info.vr_id;
    }    
    if (station_id)
    {
        *station_id = data.trif_info.station_id;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_rif_traverse(int unit, _brcm_sai_unicast_arp_cb_info_t *cb_info,
                       _brcm_sai_rif_cb cb_fn)
{
    int i;
    bcm_l3_intf_t l3_intf;
    _brcm_sai_indexed_data_t idata;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    switch(cb_info->rif_type)
    {
        case SAI_ROUTER_INTERFACE_TYPE_PORT:
        {
            for (i=1; i<_BRCM_SAI_MAX_PORTS; i++)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_RIF_TABLE,
                                                &i, &idata);
                BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "port rif data get", rv);
                if (0 != idata.port_rif.rif_obj)
                {
                    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, idata.port_rif.rif_obj);
                    rv = bcm_l3_intf_get(0, &l3_intf);
                    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf get", rv);
                    cb_info->mac = &l3_intf.l3a_mac_addr;
                    cb_fn(unit, cb_info);
                }
            }
            break;
        }
        case SAI_ROUTER_INTERFACE_TYPE_VLAN:
        {
            for (i=1; i<_BRCM_SAI_VR_MAX_VID; i++)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                                &i, &idata);
                BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "vlan rif data get", rv);
                if (0 != idata.vlan_rif.rif_obj)
                {
                    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, idata.vlan_rif.rif_obj);
                    rv = bcm_l3_intf_get(0, &l3_intf);
                    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf get", rv);
                    cb_info->mac = &l3_intf.l3a_mac_addr;
                    cb_fn(unit, cb_info);
                }
            }
            break;
        }
        case _BRCM_SAI_RIF_TYPE_LAG: /* LAG RIF */ 
        {
            bcm_trunk_chip_info_t trunk_chip_info;

            rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
            BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "trunk chip info get", rv);
            for (i=0; i<trunk_chip_info.trunk_group_count; i++)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                                &i, &idata);
                BRCM_SAI_RV_CHK(SAI_API_ROUTER_INTERFACE, "port rif data get", rv);
                if (0 != idata.lag_info.rif_obj)
                {
                    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, idata.lag_info.rif_obj);
                    rv = bcm_l3_intf_get(0, &l3_intf);
                    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "L3 intf get", rv);
                    cb_info->mac = &l3_intf.l3a_mac_addr;
                    cb_fn(unit, cb_info);
                }
            }
            break;
        }
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    return rv;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_router_interface_api_t router_intf_apis = {
    brcm_sai_create_router_interface,
    brcm_sai_remove_router_interface,
    brcm_sai_set_router_interface_attribute,
    brcm_sai_get_router_interface_attribute
};
