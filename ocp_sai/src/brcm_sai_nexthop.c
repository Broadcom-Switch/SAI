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
static int __brcm_sai_nh_max = 0;
static sem_t *nh_rl_mutex;
static sem_t nh_ecmp_mutex;
extern syncdbClientHandle_t client_id;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/

/*
################################################################################
#                                Next hop functions                            #
################################################################################
*/

/*
* Routine Description:
*    Create next hop
*
* Arguments:
*    [out] next_hop_id - next hop id
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_next_hop(_Out_ sai_object_id_t* next_hop_id,
                         _In_ sai_object_id_t switch_id,
                         _In_ uint32_t attr_count,
                         _In_ const sai_attribute_t *attr_list)
{
    bcm_if_t if_id;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bool portif = FALSE;
    bool noroute = FALSE;
    bcm_l3_route_t l3_rt;
    bcm_l2_addr_t l2addr;
    bcm_l3_intf_t l3_intf;
    bcm_l3_host_t l3_host;
    bcm_l3_egress_t l3_eg;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_nh_table_t nh_table_entry;
    _brcm_sai_tunnel_info_t tunnel_info;
    bool tunnel = FALSE, no_host = FALSE;
    _brcm_sai_nbr_table_info_t nbr_table;
    sai_ip_address_t ip_address = { .addr_family = -1 };
    sai_object_id_t rif_obj = SAI_OBJECT_TYPE_NULL, tid_obj = SAI_OBJECT_TYPE_NULL;
    int i, type_state = 0, act_type = 0, tid = -1, nhid, intf_type = -1;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(next_hop_id);

    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_intf_id = -1;
    bcm_l3_host_t_init(&l3_host);
    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    for (i = 0; i < attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_NEXT_HOP_ATTR_TYPE:
                type_state = act_type = attr_list[i].value.s32;
                if (SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP == attr_list[i].value.s32)
                {
                    tunnel = true;
                }
                break;
            case SAI_NEXT_HOP_ATTR_IP:
                if (SAI_IP_ADDR_FAMILY_IPV4 ==
                    attr_list[i].value.ipaddr.addr_family)
                {
                    l3_host.l3a_ip_addr =
                        ntohl(attr_list[i].value.ipaddr.addr.ip4);
                    nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
                    nbr_table.nbr.ip4 = l3_host.l3a_ip_addr;
                }
                else if (SAI_IP_ADDR_FAMILY_IPV6 ==
                         attr_list[i].value.ipaddr.addr_family)
                {
                    sal_memcpy(l3_host.l3a_ip6_addr,
                               attr_list[i].value.ipaddr.addr.ip6,
                               sizeof(l3_host.l3a_ip6_addr));
                    nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
                    sal_memcpy(nbr_table.nbr.ip6,
                               l3_host.l3a_ip6_addr,
                               sizeof(sai_ip6_t));
                    if (_BRCM_SAI_IS_ADDR_LINKLOCAL(l3_host.l3a_ip6_addr))
                    {
                        noroute = TRUE;
                    }
                    l3_host.l3a_flags |= BCM_L3_IP6;
                }
                else
                {
                    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Invalid IP address family.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                ip_address = attr_list[i].value.ipaddr;
                break;
            case SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID:
                rif_obj = BRCM_SAI_ATTR_LIST_OBJ(i);
                l3_intf.l3a_intf_id = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                nbr_table.nbr.rif_id =  l3_intf.l3a_intf_id;
                intf_type = BRCM_SAI_ATTR_LIST_OBJ_SUBTYPE(i);
                if ((SAI_ROUTER_INTERFACE_TYPE_PORT == intf_type) ||
                    (_BRCM_SAI_RIF_TYPE_LAG == intf_type))
                {
                    portif = TRUE;
                }
                break;
            case SAI_NEXT_HOP_ATTR_TUNNEL_ID:
                tid_obj = BRCM_SAI_ATTR_LIST_OBJ(i);
                tid = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                break;
            default:
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR,
                                "Unknown nexthop attribute %d passed\n",
                                attr_list[i].id);
                _BRCM_SAI_ATTR_LIST_DEFAULT_ERR_CHECK(SAI_NEXT_HOP_ATTR_END);
        }
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "Error processing Nexthop attr create", rv);
    }
    if ((-1 == ip_address.addr_family) ||
        ((FALSE == tunnel) && (-1 == l3_intf.l3a_intf_id)))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "No ip address or router intf.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (tunnel)
    {
        int m, max, act;
        bool found = FALSE;

        bcm_l3_route_t *l3_routes;
        bcm_tunnel_initiator_t tunnel_init;
        bcm_ip_t l3a_ip_mask = _BRCM_SAI_MASK_32;

        if (0 != _brcm_sai_tunnel_info_get(tid, &tunnel_info))
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Invalid tunnel id.\n");
            return SAI_STATUS_INVALID_PARAMETER;
        }
        rv = _brcm_sai_l3_config_get(1, &max);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "ecmp max", rv);
        l3_routes = ALLOC(sizeof(bcm_l3_route_t) * max);
        if (IS_NULL(l3_routes))
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Error with alloc %d\n", max);
            return SAI_STATUS_NO_MEMORY;
        }
        /* Find nexthop route, neighbor and use its interface */
        bcm_l3_route_t_init(&l3_rt);
        l3_rt.l3a_vrf = BRCM_SAI_GET_OBJ_MAP(tunnel_info.underlay_if);
        if (SAI_IP_ADDR_FAMILY_IPV4 == ip_address.addr_family)
        {
            l3_rt.l3a_subnet = l3_host.l3a_ip_addr;
            for (m=0; m<32; m++)
            {
                l3_rt.l3a_ip_mask = l3a_ip_mask << m;
                rv = bcm_l3_route_multipath_get(0, &l3_rt, l3_routes, max, &act);
                if (BCM_E_NOT_FOUND == rv)
                {
                    continue;
                }
                else
                {
                    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 route mp get", rv);
                }
                found = TRUE;
                break;
            }
        }
        else /* assuming v6 */
        {
            int i = 0;
            uint8 l3a_ip6_mask[16];
            l3_rt.l3a_flags = BCM_L3_IP6;
            for (i=0;i<16;i++)
            {
                sal_memset(&l3a_ip6_mask[i], 0xff, sizeof(uint8));
            }
            sal_memcpy(&l3_rt.l3a_ip6_net,
                       l3_host.l3a_ip6_addr, sizeof(bcm_ip6_t));
            for (i=15;i>=0;i--)
            {
                for (m=0;m<8;m++)
                {
                    l3a_ip6_mask[i] = (uint8)0xff << m;
                    sal_memcpy(&l3_rt.l3a_ip6_mask,
                               l3a_ip6_mask, sizeof(bcm_ip6_t));
                    rv = bcm_l3_route_multipath_get(0, &l3_rt, l3_routes, max, &act);
                    if (BCM_E_NOT_FOUND == rv)
                    {
                        continue;
                    }
                    else
                    {
                        BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 route mp get", rv);
                    }
                    found = TRUE;
                    break;
                }
                if (found)
                {
                    break;
                }
            }
        }
        if (FALSE == found)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Could not find tunnel underlay path.\n");
            CHECK_FREE(l3_routes);
            return SAI_STATUS_ITEM_NOT_FOUND;
        }
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Tunnel underlay paths: %d\n", act);
        /* Randomly select the underlay nexthop to simulate hierarchical ECMP */
        l3_rt =  l3_routes[RANDOM()%act];
        CHECK_FREE(l3_routes);
        /* Get underlay egress info */
        /* Sometime after SDK 6.5.10, bcm_l3_route_multipath_get() started
         * returning egress objects in the form 20000x instead of the expected
         * form of 10000x. For TH3, it's actually returning an ECMP object.
         */
        if (200000 <= l3_rt.l3a_intf)
        {
            if (DEV_IS_TH3())
            {
                /* If ecmp object, randomly pick one of the interfaces. */
                bcm_l3_egress_ecmp_t ecmp_object;
                bcm_if_t *intfs;

                intfs = ALLOC(max * sizeof(bcm_if_t));
                if (IS_NULL(intfs))
                {
                    BRCM_SAI_LOG_NHG(SAI_LOG_LEVEL_ERROR, "Error with alloc %d\n", max);
                    return SAI_STATUS_NO_MEMORY;
                }
                bcm_l3_egress_ecmp_t_init(&ecmp_object);
                ecmp_object.ecmp_intf = l3_rt.l3a_intf;
                rv = bcm_l3_egress_ecmp_get(0, &ecmp_object, max, (bcm_if_t *) intfs, &act);
                BRCM_SAI_API_CHK_FREE(SAI_API_NEXT_HOP_GROUP, "ecmp nh group get", rv, intfs);
                l3_rt.l3a_intf = intfs[RANDOM()%act];
                CHECK_FREE(intfs);
            }
            else
            {
                l3_rt.l3a_intf = l3_rt.l3a_intf - 100000;
            }
        }
        rv = bcm_l3_egress_get(0, l3_rt.l3a_intf, &l3_eg);
        BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress get", rv);
        l3_intf.l3a_intf_id = l3_eg.intf;
        /* Get underlay intf info */
        rv = bcm_l3_intf_get(0, &l3_intf);
        BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 intf get", rv);
        /* Create overlay intf */
        rv = bcm_l3_intf_create(0, &l3_intf);
        BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 intf create", rv);
        /* Create initiator using this new intf */
        bcm_tunnel_initiator_t_init(&tunnel_init);
        if  (BRCM_SAI_IS_ATTR_FAMILY_IPV4(tunnel_info.ip_addr))
        {
            tunnel_init.sip = ntohl(tunnel_info.ip_addr.addr.ip4);
            tunnel_init.dip = l3_host.l3a_ip_addr;
            tunnel_init.type = bcmTunnelTypeIpAnyIn4;
        }
        else if (BRCM_SAI_IS_ATTR_FAMILY_IPV6(tunnel_info.ip_addr))
        {
            sal_memcpy(&tunnel_init.sip6,
                       &tunnel_info.ip_addr.addr.ip6,
                       sizeof(bcm_ip6_t));
            sal_memcpy(&tunnel_init.dip6,
                       &l3_host.l3a_ip6_addr,sizeof(bcm_ip6_t));
            tunnel_init.type = bcmTunnelTypeIpAnyIn6;
        }
        tunnel_init.ttl = SAI_TUNNEL_TTL_MODE_PIPE_MODEL == tunnel_info.encap_ttl_mode ?
                          tunnel_info.encap_ttl : 0;
        if (SAI_TUNNEL_DSCP_MODE_PIPE_MODEL == tunnel_info.encap_dscp_mode)
        {
            tunnel_init.dscp = tunnel_info.encap_dscp;
        }
        else if (SAI_TUNNEL_DSCP_MODE_UNIFORM_MODEL == tunnel_info.encap_dscp_mode)
        {
            tunnel_init.dscp_sel = bcmTunnelDscpPacket;
        }
        rv = bcm_tunnel_initiator_set(0, &l3_intf, &tunnel_init);
        BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "tunnel initiator encap set", rv);
        /* Create egress using the new intf */
        tunnel_info.intf = l3_eg.intf = l3_intf.l3a_intf_id;
        _brcm_sai_tunnel_info_set(tid, &tunnel_info);
    }
    else
    {
        int pa;

        rv = bcm_l3_intf_get(0, &l3_intf);
        BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 intf get", rv);
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "On rif_id: %d got vrf_id: %d vid: %d\n",
                        l3_intf.l3a_intf_id, l3_intf.l3a_vrf, l3_intf.l3a_vid);
        l3_host.l3a_vrf = nbr_table.nbr.vr_id = l3_intf.l3a_vrf;
        tdata.nbr_table = &nbr_table;
        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Neighbor not found\n");
            return SAI_STATUS_ITEM_NOT_FOUND;
        }
        if (noroute == FALSE)
        {
            rv = bcm_l3_host_find(0, &l3_host);
            BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 host get", rv);
        }
        else
        {
            l3_host.l3a_intf = tdata.nbr_table->if_id;
            l3_host.l3a_flags |= tdata.nbr_table->flags;
            l3_host.l3a_port_tgid = tdata.nbr_table->port_tgid;
            sal_memcpy(&l3_host.l3a_nexthop_mac, &tdata.nbr_table->mac,
                       sizeof(sai_mac_t));
        }
        /* Get the global drop intf */
        rv = _brcm_sai_drop_if_get(&if_id);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP,
                        "getting system drop intf global data", rv);

        if (FALSE == portif) /* if not a port or lag */
        {
            bcm_mac_t mac;
            _brcm_sai_nbr_info_t nbr_info;

            sal_memcpy(&mac, &nbr_table.mac, sizeof(bcm_mac_t));
            /* Need to see if the L2 entry exists.... more or less a VLAN check. */
            rv = bcm_l2_addr_get(0, mac, l3_intf.l3a_vid, &l2addr);
            if (BCM_E_NOT_FOUND == rv)
            {
                no_host = TRUE;
                type_state = _BRCM_SAI_NH_UNKOWN_NBR_TYPE;
            }
            /* get pa from state */
            /* Lookup mac-nbr db to find this mac */
            rv = _brcm_sai_mac_nbr_lookup(mac, &l3_intf, &l3_host, &nbr_info);
            if (rv == SAI_STATUS_FAILURE)
            {
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Error in mac nbr table entry lookup.");
                return rv;
            }
            if (rv == SAI_STATUS_ITEM_NOT_FOUND)
            {
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Mac Nbr entry not found.");
                return rv;
            }
            pa = nbr_info.pa;
        }
        else
        {
            /* get pa from nbr's egress object.... if_id contains
             * global drop intf at this point.
             */
            pa = (if_id == l3_host.l3a_intf) ? SAI_PACKET_ACTION_DROP :
                 SAI_PACKET_ACTION_FORWARD;
        }
        if (no_host || SAI_PACKET_ACTION_DROP == pa)
        {
            /* Get and use the global drop intf */
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "L3 egress drop object id: %d\n", if_id);
            goto _create_nhid_obj;
        }
        else
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Got rif_id: %d %s: %d\n",
                            l3_host.l3a_intf,
                            (l3_host.l3a_flags & BCM_L3_TGID) ? "trunk" : "port",
                            l3_host.l3a_port_tgid);
            bcm_l3_egress_t_init(&l3_eg);
            l3_eg.intf = l3_intf.l3a_intf_id;
            l3_eg.vlan = l3_intf.l3a_vid;
            if_id = l3_host.l3a_intf;
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "L3 egress deref object id: %d\n", if_id);
            goto _create_nhid_obj;
        }
    }
    rv = bcm_l3_egress_create(0, 0, &l3_eg, &if_id);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress create", rv);
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "L3 egress object id: %d\n", if_id);
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "global egress inuse count increase", rv);

_create_nhid_obj:
    /* Find an unused id, save if_id and create obj */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_NH_INFO, 1,
                                              __brcm_sai_nh_max, &nhid);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Unexpected nh resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Using nhid: %d\n", nhid);
    data.nh_info.idx = nhid;
    data.nh_info.act_type = act_type;
    data.nh_info.type_state = type_state;
    data.nh_info.if_id = if_id;
    data.nh_info.intf_type = intf_type;
    data.nh_info.nbr.vr_id = l3_host.l3a_vrf;
    data.nh_info.rif_obj = rif_obj;
    data.nh_info.tid_obj = tid_obj;
    data.nh_info.nbr.addr_family = ip_address.addr_family;
    if (SAI_IP_ADDR_FAMILY_IPV4 == ip_address.addr_family)
    {
        data.nh_info.nbr.ip4 = l3_host.l3a_ip_addr;
    }
    else
    {
        sal_memcpy(data.nh_info.nbr.ip6, l3_host.l3a_ip6_addr,
                   sizeof(l3_host.l3a_ip6_addr));
    }
    data.nh_info.nbr.rif_id = l3_intf.l3a_intf_id;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_NH_INFO, &nhid, &data);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Unable to save nh info data.\n");
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_NH_INFO, nhid);
        return rv;
    }

    /* Add a reference to NH Nbr DB entry and return.
     * Note: Assigning l3_host.l3a_intf to L3 intf ID
     * so following function can do a NBR lookup.
     */
    l3_host.l3a_intf = l3_intf.l3a_intf_id;
    rv = _brcm_sai_nbr_nh_ref_attach(&l3_host, nhid, -1);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nbr nh ref attach", rv);

    /* Add an entry into this NH's table DB */
    DATA_CLEAR(nh_table_entry, _brcm_sai_nh_table_t);
    nh_table_entry.nhid = nhid;
    tdata.nh_table = &nh_table_entry;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Error in nh table entry add.");
        return rv;
    }
    if (sem_init(&nh_rl_mutex[nhid], 1, 1) < 0)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "sem init nh route list mutex failed\n");
        return SAI_STATUS_FAILURE;
    }

    *next_hop_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_NEXT_HOP, nhid);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);

    return rv;
}

/*
* Routine Description:
*    Remove next hop
*
* Arguments:
*    [in] next_hop_id - next hop id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_next_hop(_In_ sai_object_id_t next_hop_id)
{
    int nhid;
    int tid;
    sai_status_t rv;
    bool noroute = FALSE;
    bcm_l3_host_t l3_host;
    bcm_l3_intf_t l3_intf;
    bool lookup_host = FALSE;
    bool nbr_priv_egr_obj = FALSE;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table_entry;
    _brcm_sai_tunnel_info_t tunnel_info;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(next_hop_id, SAI_OBJECT_TYPE_NEXT_HOP))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR,
                        "Invalid object type 0x%16lx passed\n",
                        next_hop_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    nhid = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, next_hop_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO, &nhid, &data);
    if (SAI_STATUS_ERROR(rv) || (0 == data.nh_info.if_id))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Unable to get nh info data.\n");
        return rv;
    }

    /* Remove this NH from DB */
    DATA_CLEAR(nh_table_entry, _brcm_sai_nh_table_t);
    tdata.nh_table = &nh_table_entry;
    nh_table_entry.nhid = nhid;
    sem_wait(&nh_rl_mutex[nhid]);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "nh db lookup");
        sem_post(&nh_rl_mutex[nhid]);
        return rv;
    }
    if (nh_table_entry.fwd_count || nh_table_entry.drop_count ||
        nh_table_entry.ecmp_count || nh_table_entry.entry_count)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR,
                        "nh table entry in use: fwd = %d, drop = %d, ecmp = %d, "
                        "redirect = %d\n", nh_table_entry.fwd_count,
                        nh_table_entry.drop_count, nh_table_entry.ecmp_count,
                        nh_table_entry.entry_count);
        sem_post(&nh_rl_mutex[nhid]);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    DATA_CLEAR(nh_table_entry, _brcm_sai_nh_table_t);
    nh_table_entry.nhid = nhid;
    rv = _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_NH, &tdata);
    sem_post(&nh_rl_mutex[nhid]);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nh db delete", rv);
    sem_destroy(&nh_rl_mutex[nhid]);
    /* Common check to determine shared obj */
    if (BRCM_SAI_IS_FEAT_EXT_VIEW_NO_TRNK())
    {
        if (_BRCM_SAI_RIF_TYPE_LAG == data.nh_info.intf_type)
        {
            nbr_priv_egr_obj = TRUE;
        }
        else if (SAI_ROUTER_INTERFACE_TYPE_VLAN == data.nh_info.intf_type)
        {
            lookup_host = TRUE;
        }
    }

    /* Remove NBR NH reference */
    bcm_l3_host_t_init(&l3_host);
    l3_host.l3a_vrf = data.nh_info.nbr.vr_id;
    if (SAI_IP_ADDR_FAMILY_IPV4 == data.nh_info.nbr.addr_family)
    {
        l3_host.l3a_ip_addr = data.nh_info.nbr.ip4;
    }
    else
    {
        sal_memcpy(l3_host.l3a_ip6_addr, data.nh_info.nbr.ip6,
                   sizeof(l3_host.l3a_ip6_addr));
        l3_host.l3a_flags |= BCM_L3_IP6;
        if (_BRCM_SAI_IS_ADDR_LINKLOCAL(l3_host.l3a_ip6_addr))
        {
            noroute = TRUE;
            nbr_priv_egr_obj = FALSE;
        }
    }
    if (BRCM_SAI_IS_FEAT_NO_EXT_HOST_VIEW() && (FALSE == noroute))
    {
        nbr_priv_egr_obj = TRUE;
    }
    l3_host.l3a_intf = data.nh_info.nbr.rif_id;
    rv = _brcm_sai_nbr_nh_ref_detach(&l3_host, nhid, lookup_host);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nbr nh ref detach", rv);
    /* Don't delete the global drop intf */
    if (_BRCM_SAI_NH_UNKOWN_NBR_TYPE != data.nh_info.type_state)
    {
        /* Note: Tunnel NHs are the only type we should remove. */
        if (SAI_OBJECT_TYPE_TUNNEL == BRCM_SAI_GET_OBJ_TYPE(data.nh_info.tid_obj))
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Removing if_id: %d\n", data.nh_info.if_id);
            rv = bcm_l3_egress_destroy(0, data.nh_info.if_id);
            BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress destroy", rv);
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, DEC);
            BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "global egress inuse count decrease", rv);
        }
    }

    /* Remove tunnel related items */
    if (SAI_NEXT_HOP_TYPE_TUNNEL_ENCAP == data.nh_info.act_type)
    {
        bcm_l3_intf_t_init(&l3_intf);
        l3_intf.l3a_intf_id = data.nh_info.nbr.rif_id;

        rv = bcm_tunnel_initiator_clear(0, &l3_intf);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "clear tunnel initiator", rv);

        /* delete overlay intf */
        rv = bcm_l3_intf_delete(0, &l3_intf);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "delete tunnel overlay l3 intf", rv);

        tid = BRCM_SAI_GET_OBJ_VAL(int, data.nh_info.tid_obj);
        if (SAI_STATUS_SUCCESS == _brcm_sai_tunnel_info_get(tid, &tunnel_info))
        {
            /* clear overlay intf data in tunnel_info table */
            tunnel_info.intf = 0;
            _brcm_sai_tunnel_info_set(tid, &tunnel_info);
        }
        else
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Error from _brcm_sai_tunnel_info_get(): tid = %d\n", tid);
        }
    }
    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_NH_INFO, nhid);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);
    return rv;
}

/*
* Routine Description:
*    Set Next Hop attribute
*
* Arguments:
*    [in] sai_object_id_t - next_hop_id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_next_hop_attribute(_In_ sai_object_id_t next_hop_id,
                                _In_ const sai_attribute_t *attr)
{
    int nhid;
    bcm_if_t if_id;
    sai_status_t rv;
    bcm_l3_host_t _l3_host, l3_host;
    bcm_l3_egress_t l3_eg;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Null attr param\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (BRCM_SAI_CHK_OBJ_MISMATCH(next_hop_id, SAI_OBJECT_TYPE_NEXT_HOP))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR,
                        "Invalid nexthop object 0x%16lx passed\n",
                        next_hop_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    nhid = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, next_hop_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO, (int *)&nhid, &data);
    if (SAI_STATUS_ERROR(rv) || (0 == data.nh_info.if_id))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "nh info get.\n");
        return rv;
    }
    if_id = data.nh_info.if_id;
    rv = bcm_l3_egress_get(0, if_id, &l3_eg);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress get", rv);

    switch (attr->id)
    {
        case SAI_NEXT_HOP_ATTR_IP:
            bcm_l3_host_t_init(&_l3_host);
            bcm_l3_host_t_init(&l3_host);
            if (SAI_IP_ADDR_FAMILY_IPV4 ==
                    attr->value.ipaddr.addr_family)
            {
                l3_host.l3a_ip_addr = ntohl(attr->value.ipaddr.addr.ip4);
                /* Check if different or same IP ? If same then NO-OP and break.
                 */
                if (SAI_IP_ADDR_FAMILY_IPV4 == data.nh_info.nbr.addr_family)
                {
                    if (l3_host.l3a_ip_addr == data.nh_info.nbr.ip4)
                    {
                        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Skip NH update with same v4 nbr ip");
                        break;
                    }
                    _l3_host.l3a_ip_addr = data.nh_info.nbr.ip4;
                }
                else
                {
                    sal_memcpy(_l3_host.l3a_ip6_addr, data.nh_info.nbr.ip6,
                               sizeof(l3_host.l3a_ip6_addr));
                    _l3_host.l3a_flags |= BCM_L3_IP6;
                    data.nh_info.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
                    DATA_CLEAR(data.nh_info.nbr.ip6, sizeof(l3_host.l3a_ip6_addr));
                }
                data.nh_info.nbr.ip4 = l3_host.l3a_ip_addr;
            }
            else if (SAI_IP_ADDR_FAMILY_IPV6 ==
                     attr->value.ipaddr.addr_family)
            {
                /* Check if different or same IP ? If same then NO-OP and break.
                 */
                if (SAI_IP_ADDR_FAMILY_IPV6 == data.nh_info.nbr.addr_family)
                {
                    if (MATCH == sal_memcmp(attr->value.ipaddr.addr.ip6,
                                            data.nh_info.nbr.ip6,
                                            sizeof(l3_host.l3a_ip6_addr)))
                    {
                        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Skip NH update with same v6 nbr ip");
                        break;
                    }
                    sal_memcpy(_l3_host.l3a_ip6_addr, data.nh_info.nbr.ip6,
                               sizeof(l3_host.l3a_ip6_addr));
                    _l3_host.l3a_flags |= BCM_L3_IP6;
                }
                else
                {
                    _l3_host.l3a_ip_addr = data.nh_info.nbr.ip4;
                    data.nh_info.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
                    data.nh_info.nbr.ip4 = 0;
                }
                sal_memcpy(l3_host.l3a_ip6_addr, attr->value.ipaddr.addr.ip6,
                           sizeof(l3_host.l3a_ip6_addr));
                l3_host.l3a_flags |= BCM_L3_IP6;
                sal_memcpy(data.nh_info.nbr.ip6, l3_host.l3a_ip6_addr,
                           sizeof(l3_host.l3a_ip6_addr));
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            l3_host.l3a_vrf = data.nh_info.nbr.vr_id;
            l3_host.l3a_intf = data.nh_info.nbr.rif_id;
            /* Else Detach from old Nbr, attach to new Nbr */
            rv = _brcm_sai_nbr_nh_ref_detach(&l3_host, nhid, FALSE);
            BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nbr nh ref detach", rv);
            rv = _brcm_sai_nbr_nh_ref_attach(&l3_host, nhid, -1);
            BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nbr nh ref attach", rv);

            rv = bcm_l3_host_find(0, &l3_host);
            BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 host get", rv);
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Got rif_id: %d port: %d\n",
                            l3_host.l3a_intf, l3_host.l3a_port_tgid);
            memcpy(l3_eg.mac_addr, l3_host.l3a_nexthop_mac, sizeof(bcm_mac_t));
            if (l3_host.l3a_flags & BCM_L3_TGID)
            {
                l3_eg.trunk = l3_host.l3a_port_tgid;
                l3_eg.flags |= BCM_L3_TGID;
            }
            else
            {
                l3_eg.port = l3_host.l3a_port_tgid;
            }
            rv = bcm_l3_egress_create(0, BCM_L3_WITH_ID | BCM_L3_REPLACE,
                                      &l3_eg, &if_id);
            BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress create", rv);
            break;
        case SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID:
            l3_eg.intf = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, attr->value.oid);
            rv = bcm_l3_egress_create(0, BCM_L3_WITH_ID | BCM_L3_REPLACE,
                                      &l3_eg, &if_id);
            BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress create", rv);
            data.nh_info.rif_obj = attr->value.oid;
            break;
        default:
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_INFO,
                            "Unknown nexthop attribute %d passed\n",
                            attr->id);
            _BRCM_SAI_ATTR_DEFAULT_ERR_CHECK(SAI_NEXT_HOP_ATTR_END);
    }
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "Error processing nh attr set", rv);
    /* Save updated info */
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_NH_INFO, (int *)&nhid, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nh info set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);

    return rv;
}

/*
* Routine Description:
*    Get Next Hop attribute
*
* Arguments:
*    [in] sai_object_id_t - next_hop_id
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_next_hop_attribute(_In_ sai_object_id_t next_hop_id,
                                _In_ uint32_t attr_count,
                                _Inout_ sai_attribute_t *attr_list)
{
    int i, idx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(next_hop_id, SAI_OBJECT_TYPE_NEXT_HOP);
    idx = BRCM_SAI_GET_OBJ_VAL(int, next_hop_id);
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG,
                    "Get next hop: %d\n", idx);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "next hop get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_NEXT_HOP_ATTR_TYPE:
                attr_list[i].value.s32 = data.nh_info.act_type;
                break;
            case SAI_NEXT_HOP_ATTR_IP:
                attr_list[i].value.ipaddr.addr_family = data.nh_info.nbr.addr_family;
                if (SAI_IP_ADDR_FAMILY_IPV4 == data.nh_info.nbr.addr_family)
                {
                    attr_list[i].value.ipaddr.addr.ip4 = htonl(data.nh_info.nbr.ip4);
                }
                else
                {
                    sal_memcpy(attr_list[i].value.ipaddr.addr.ip6, data.nh_info.nbr.ip6,
                               sizeof(sai_ip6_t));
                }
                break;
            case SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = data.nh_info.rif_obj;
                break;
            case SAI_NEXT_HOP_ATTR_TUNNEL_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = data.nh_info.tid_obj;
                break;
            default:
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR,
                                "Unknown next hop attribute %d passed\n",
                                attr_list[i].id);
                _BRCM_SAI_ATTR_LIST_DEFAULT_ERR_CHECK(SAI_NEXT_HOP_ATTR_END);
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_INFO,
                            "Error processing next hop attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/

STATIC sai_status_t
_brcm_sai_nh_init_rl_locks(void)
{
    sai_status_t rv;
    _brcm_sai_table_data_t data;
    _brcm_sai_nh_table_t nh_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    data.nh_table = &nh_table;

    do
    {
        rv = _brcm_sai_db_table_entry_getnext(_BRCM_SAI_TABLE_NH, &data);
        if (SAI_STATUS_SUCCESS == rv)
        {
            if (sem_init(&nh_rl_mutex[nh_table.nhid], 1, 1) < 0)
            {
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "sem init nh route list mutex failed\n");
                return SAI_STATUS_FAILURE;
            }
        }
    } while (SAI_STATUS_SUCCESS == rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);
    return SAI_STATUS_SUCCESS;
}

/* Routine to allocate nh state */
sai_status_t
_brcm_sai_alloc_nh_info()
{
    int max;
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    rv = _brcm_sai_l3_config_get(2, &max);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 info", rv);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_NH_INFO, max + 1))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_CRITICAL,
                        "Error initializing nh info state !!\n");
        return SAI_STATUS_FAILURE;
    }
    __brcm_sai_nh_max = max;

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_db_table_create(_BRCM_SAI_TABLE_NH, max))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_CRITICAL,
                        "Error creating nh table !!\n");
        return SAI_STATUS_FAILURE;
    }

    nh_rl_mutex = ALLOC(sizeof(sem_t) * (max + 1));
    if (IS_NULL(nh_rl_mutex))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Error with nh route list mutex alloc %d\n", max);
        return SAI_STATUS_NO_MEMORY;
    }

    /* Initialize per-NH semaphores */
    if (_brcm_sai_switch_wb_state_get())
    {
        rv = _brcm_sai_nh_init_rl_locks();
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "initializing route list locks", rv);
    }

    /* Now traverse the NH table and create Route lists */
    rv = _brcm_sai_db_table_node_list_init(_BRCM_SAI_TABLE_NH,
                                           _BRCM_SAI_LIST_ROUTE_LIST_FWD);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "creating nh table node route fwd lists", rv);
    rv = _brcm_sai_db_table_node_list_init(_BRCM_SAI_TABLE_NH,
                                           _BRCM_SAI_LIST_ROUTE_LIST_DROP);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "creating nh table node route drop lists", rv);
    rv = _brcm_sai_db_table_node_list_init(_BRCM_SAI_TABLE_NH,
                                           _BRCM_SAI_LIST_NH_ECMP_INFO);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "creating nh table node ecmp lists", rv);

    if (sem_init(&nh_ecmp_mutex, 1, 1) < 0)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "sem init nh_ecmp_mutex failed\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_nh_info()
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTE);

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_NH_INFO,
                                      1, __brcm_sai_nh_max, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_NEXT_HOP, SAI_LOG_LEVEL_CRITICAL,
                        "freeing nh info state", rv);
    /* Traverse the NH table and free Route lists */
    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_NH,
                                           _BRCM_SAI_LIST_ROUTE_LIST_FWD);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nh table node route fwd lists", rv);
    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_NH,
                                           _BRCM_SAI_LIST_ROUTE_LIST_DROP);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nh table node route drop lists", rv);
    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_NH,
                                           _BRCM_SAI_LIST_NH_ECMP_INFO);
    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "nh table node ecmp lists", rv);
    CHECK_FREE(nh_rl_mutex);
    nh_rl_mutex = NULL;
    sem_destroy(&nh_ecmp_mutex);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_nh_mac_update(bcm_if_t if_id, bool old_lag, bool lag,
                        int old_port_lag, int port_lag,
                        int vlan, bcm_if_t intf,
                        bcm_mac_t old_mac,
                        bcm_mac_t new_mac)
{
    sai_status_t rv;
    bcm_l3_egress_t l3_eg;
    uint32_t flags = 0;

    rv = bcm_l3_egress_get(0, if_id, &l3_eg);
    if (old_port_lag != port_lag)
    {
        if (lag)
        {
            l3_eg.trunk = port_lag;
        }
        else
        {
            l3_eg.port = port_lag;
        }
    }
    memcpy(l3_eg.mac_addr, new_mac, sizeof(bcm_mac_t));
    /* If old egress object is still around, just update it. */
    if (BCM_E_NONE == rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Update nhid: %d\n", if_id);
        flags = BCM_L3_WITH_ID | BCM_L3_REPLACE;
        rv = bcm_l3_egress_create(0, flags, &l3_eg, &if_id);
        if (BCM_E_NOT_FOUND == rv)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Skipping NH replace as entry doesn't exist.\n");
            rv = BCM_E_NONE;
        }
        else
        {
            BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress create", rv);
        }
    }
    /* Old egress object must've gotten deleted. */
    else if (BCM_E_NOT_FOUND == rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Skipping NH update as entry doesn't exist.\n");
    }

    return BRCM_RV_BCM_TO_SAI(rv);
}

STATIC sai_status_t
_brcm_sai_nh_route_update(int nhid, bcm_if_t if_id)
{
    sai_status_t rv;
    bcm_l3_route_t l3_rt;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_route_list_t *route_node;
    int _tintf;

    rv = _brcm_sai_trap_if_get(&_tintf);
    BRCM_SAI_RV_CHK(SAI_API_ROUTE,
                    "getting system trap intf global data", rv);

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    nh_table.nhid = nhid;
    tdata.nh_table = &nh_table;
    sem_wait(&nh_rl_mutex[nhid]);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh table lookup of nh %d\n", nhid);
        sem_post(&nh_rl_mutex[nhid]);
        return rv;
    }

    if (!IS_NULL(nh_table.fwd))
    {
        /* Iterate through all routes having this nhid and update them */
        ldata.route_list = nh_table.fwd;
        do
        {
            route_node = ldata.route_list;
            /* Need to handle updates to routes that are in FWD or TRAP. */
            if (FALSE == route_node->multipath && _ROUTE_STATE_DROP != route_node->state)
            {
                bcm_l3_route_t_init(&l3_rt);
                l3_rt.l3a_flags |= BCM_L3_REPLACE;
                l3_rt.l3a_vrf = route_node->route.vr_id;
                if (SAI_IP_ADDR_FAMILY_IPV4 == route_node->route.addr_family)
                {
                    l3_rt.l3a_subnet = route_node->route.ip4;
                    l3_rt.l3a_ip_mask = route_node->route.ip4m;
                }
                else
                {
                    sal_memcpy(l3_rt.l3a_ip6_net, route_node->route.ip6,
                               sizeof(l3_rt.l3a_ip6_net));
                    sal_memcpy(l3_rt.l3a_ip6_mask, route_node->route.ip6m,
                               sizeof(l3_rt.l3a_ip6_mask));
                    l3_rt.l3a_flags |= BCM_L3_IP6;
                }
                /* If TRAP state, then need to assign
                 * Global Trap interface.
                 */
                if (_ROUTE_STATE_TRAP == route_node->state)
                {
                    l3_rt.l3a_intf = _tintf;
                }
                else
                {
                    l3_rt.l3a_intf = if_id;
                }
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Update route vrf: %d, egr id: %d\n",
                                l3_rt.l3a_vrf, l3_rt.l3a_intf);
                rv = bcm_l3_route_add(0, &l3_rt);
                if (BCM_FAILURE(rv))
                {
                    sem_post(&nh_rl_mutex[nhid]);
                }
                BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 route add", rv);
            }
        } while (SAI_STATUS_SUCCESS ==
                 _brcm_sai_list_traverse(_BRCM_SAI_LIST_ROUTE_LIST_FWD, &ldata, &ldata));
    }
    sem_post(&nh_rl_mutex[nhid]);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_nh_intf_update(int nhid, bool add, bool phy_intf, bcm_l3_host_t *l3_host)
{
    int type = 0;
    sai_status_t rv;
    bcm_if_t if_id, _if_id = 0;
    _brcm_sai_indexed_data_t idata;

    idata.nh_info.idx = nhid;
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                    &idata.nh_info.idx, &idata);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Unable to get nh info data.\n");
        return rv;
    }

    if (add)
    {
        if_id = idata.nh_info.if_id = l3_host->l3a_intf;
    }
    else
    {
        /* Save current NH object for use in destroy */
        _if_id = idata.nh_info.if_id;
        /* Use drop entry */
        rv = _brcm_sai_drop_if_get(&if_id);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP,
                        "getting system drop intf global data", rv);
        if (if_id == _if_id)
        {
            return rv;
        }
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Use L3 egress drop object id: %d\n", if_id);
        type = phy_intf ? type : _BRCM_SAI_NH_UNKOWN_NBR_TYPE;
        if (!phy_intf)
        {
            idata.nh_info.if_id = if_id;
        }
    }
    /* Update NH object */
    idata.nh_info.type_state = type;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_NH_INFO,
                                    &idata.nh_info.idx, &idata);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Unable to set nh info data.\n");
        return rv;
    }
    /* Update all associated routes */
    rv = _brcm_sai_nh_route_update(nhid, if_id);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Error in route nh update.");
    }
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Route NH dependencies updated.");
    /* Update all associated NHG's */
    if (add)
    {
        rv = _brcm_sai_nhg_nh_update(add, nhid, if_id);
    }
    else
    {
        rv = _brcm_sai_nhg_nh_update(add, nhid, _if_id);
    }
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Error in nhg nh update.");
    }
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "NHG NH dependencies updated.");

    if (!add && (0 != _if_id) && !phy_intf)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Remove egress object %d\n", _if_id);
        /* Remove NH entry */
        rv = bcm_l3_egress_destroy(0, _if_id);
        if (BCM_E_NOT_FOUND == rv)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG,
                           "Skipping NH delete as entry doesn't exist.\n");
            rv = BCM_E_NONE;
        }
        else if (BCM_E_BUSY == rv)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG,
                           "Skipping NH delete as entry has a non-zero ref count.\n");
            rv = BCM_E_NONE;
        }
        else
        {
            BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "L3 egress destroy", rv);
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, DEC);
            BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "global egress inuse count decrease", rv);
        }
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Removed egress object %d\n", _if_id);
    }

    return rv;
}

sai_status_t
_brcm_sai_nh_table_route_list_route_add(int nhidx, _brcm_sai_route_list_t *route_node)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table;
    _brcm_sai_route_list_t *nh_start, *nh_end;
    int rl_count;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    nh_table.nhid = nhidx;
    tdata.nh_table = &nh_table;

    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Looking up nh %d in nh table\n", nh_table.nhid);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error looking up nh %d in nh table (%d)",
                        nh_table.nhid, rv);
        return rv;
    }

    rl_count = (_ROUTE_STATE_DROP == route_node->state) ? nh_table.drop_count : nh_table.fwd_count;
    nh_start = (_ROUTE_STATE_DROP == route_node->state) ? nh_table.drop : nh_table.fwd;
    nh_end = (_ROUTE_STATE_DROP == route_node->state) ? nh_table.drop_end : nh_table.fwd_end;
    route_node->next = NULL;
    if (nh_start)
    {
        route_node->prev = nh_end;
        nh_end->next = route_node;
    }
    else
    {
        nh_start = route_node;
    }
    nh_end = route_node;
    rl_count++;

    /* Update NH entry */
    if (_ROUTE_STATE_DROP == route_node->state)
    {
        nh_table.drop = nh_start;
        nh_table.drop_end = nh_end;
        nh_table.drop_count = rl_count;
    }
    else
    {
        nh_table.fwd = nh_start;
        nh_table.fwd_end = nh_end;
        nh_table.fwd_count = rl_count;
    }
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error updating nh %d in nh table (%d)",
                        nh_table.nhid, rv);
        return rv;
    }
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "nh_start = %p, nh_end = %p\n",
                    nh_start, nh_end);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);
    return rv;
}

sai_status_t
_brcm_sai_nh_table_route_list_route_del(int nhidx, _brcm_sai_route_list_t *route_node)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table;
    _brcm_sai_route_list_t *ptr = route_node, *prev = route_node->prev;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    nh_table.nhid = nhidx;
    tdata.nh_table = &nh_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh table lookup of nh %d\n", nhidx);
        return rv;
    }
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Removing route from route list of nh %d, count = %d\n",
                    nhidx, nh_table.fwd_count);
    /* Remove entry, decrement count */
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "nh_table.fwd = %p, nh_table.fwd_end = %p\n",
                    nh_table.fwd, nh_table.fwd_end);
    if (nh_table.fwd_count)
    {
        if (FALSE == ptr->valid)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Removing route 0x%08x from nh list, "
                            "ptr = %p, ptr->next = %p\n",
                            ptr->route.ip4, ptr, ptr->next);
            /* Unlink node, adjust "start/end" pointers if appropriate. */
            if (1 == nh_table.fwd_count)
            {
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "nh_table.fwd = %p, nh_table.fwd_end = %p\n",
                                nh_table.fwd, nh_table.fwd_end);
                nh_table.fwd = nh_table.fwd_end = NULL;
            }
            else
            {
                if (ptr == nh_table.fwd)
                {
                    nh_table.fwd = ptr->next;
                    nh_table.fwd->prev = NULL;
                }
                else
                {
                    prev->next = ptr->next;
                }
                if (IS_NULL(ptr->next))
                {
                    nh_table.fwd_end = prev;
                    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "nh_table.fwd = %p, nh_table.fwd_end = %p\n",
                                    nh_table.fwd, nh_table.fwd_end);
                }
                else
                {
                    ptr->next->prev = prev;
                }
            }
            nh_table.fwd_count--;
        }
        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NH, &tdata);
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh table add\n");
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);

    return rv;
}

sai_status_t
_brcm_sai_nh_table_route_list_dump(int nhidx)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_route_list_t *ptr, *prev;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    nh_table.nhid = nhidx;
    tdata.nh_table = &nh_table;
    sem_wait(&nh_rl_mutex[nhidx]);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh table lookup of nh %d\n", nhidx);
        sem_post(&nh_rl_mutex[nhidx]);
        return rv;
    }
    /* Traverse Route List */
    ldata.route_list = ptr = prev = nh_table.fwd;
    if (nh_table.fwd_count)
    {
        do
        {
            ptr = ldata.route_list;
            if (ptr)
            {
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG,
                                "ptr->valid = %s, ptr = %p, ptr->prev = %p, ptr->next = %p\n",
                                ptr->valid ? "TRUE" : "FALSE", ptr, ptr->prev, ptr->next);
                prev = ptr;
                ptr = ptr->next;
            }
        } while (SAI_STATUS_SUCCESS ==
                (rv = _brcm_sai_list_traverse(_BRCM_SAI_LIST_ROUTE_LIST_FWD, &ldata, &ldata)));
    }
    sem_post(&nh_rl_mutex[nhidx]);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);

    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        return SAI_STATUS_SUCCESS;
    }
    else
    {
        return rv;
    }
}

void
_brcm_sai_nh_table_route_list_lock(int nhid)
{
    sem_wait(&nh_rl_mutex[nhid]);
}

void
_brcm_sai_nh_table_route_list_unlock(int nhid)
{
    sem_post(&nh_rl_mutex[nhid]);
}

sai_status_t
_brcm_sai_nh_table_ecmp_list_ecmp_add(int nhidx, bcm_if_t ecmp_if_id)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table;
    _brcm_sai_nh_ecmp_t *ecmp_entry;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    ecmp_entry = ALLOC_CLEAR(1, sizeof(_brcm_sai_nh_ecmp_t));
    if (IS_NULL(ecmp_entry))
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                        sizeof(_brcm_sai_nh_ecmp_t));
        return SAI_STATUS_NO_MEMORY;
    }

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    nh_table.nhid = nhidx;
    tdata.nh_table = &nh_table;

    sem_wait(&nh_ecmp_mutex);
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Looking up nh %d in nh table\n", nh_table.nhid);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error looking up nh %d in nh table (%d)",
                        nh_table.nhid, rv);
        sem_post(&nh_ecmp_mutex);
        return rv;
    }
    ecmp_entry->intf = ecmp_if_id;
    if (nh_table.ecmp)
    {
        nh_table.ecmp_end->next = ecmp_entry;
    }
    else
    {
        nh_table.ecmp = ecmp_entry;
    }
    nh_table.ecmp_end = ecmp_entry;
    nh_table.ecmp_count++;

    /* Update NH entry */
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error updating nh %d in nh table (%d)",
                        nh_table.nhid, rv);
        sem_post(&nh_ecmp_mutex);
        return rv;
    }
    sem_post(&nh_ecmp_mutex);
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "nh_table.ecmp = %p, nh_table.ecmp_end = %p\n",
                    nh_table.ecmp, nh_table.ecmp_end);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);
    return rv;
}

sai_status_t
_brcm_sai_nh_table_ecmp_list_ecmp_del(int nhidx, bcm_if_t ecmp_if_id)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_nh_ecmp_t *ptr, *prev;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    nh_table.nhid = nhidx;
    tdata.nh_table = &nh_table;
    sem_wait(&nh_ecmp_mutex);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh table lookup of nh %d\n", nhidx);
        sem_post(&nh_ecmp_mutex);
        return rv;
    }
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG,
                    "Removing ECMP entry from ECMP list of nh %d, count = %d\n",
                    nhidx, nh_table.ecmp_count);
    /* Traverse ECMP List, remove entry, decrement count, free memory */
    ldata.nh_ecmp = ptr = prev = nh_table.ecmp;
    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "nh_table.ecmp = %p, nh_table.ecmp_end = %p\n",
                    nh_table.ecmp, nh_table.ecmp_end);
    do
    {
        ptr = ldata.nh_ecmp;
        if (ptr->intf == ecmp_if_id)
        {
            BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG, "Removing ECMP %d from nh ECMP list, "
                            "ptr = %p, ptr->next = %p\n", ptr->intf, ptr, ptr->next);
            /* Adjust "start/end" pointers if appropriate. */
            if (1 == nh_table.ecmp_count)
            {
                BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG,
                                "nh_table.ecmp = %p, nh_table.ecmp_end = %p\n",
                                nh_table.ecmp, nh_table.ecmp_end);
                nh_table.ecmp = nh_table.ecmp_end = NULL;
            }
            else
            {
                if (ptr == nh_table.ecmp)
                {
                    nh_table.ecmp = ptr->next;
                }
                else
                {
                    prev->next = ptr->next;
                }
                if (IS_NULL(ptr->next))
                {
                    nh_table.ecmp_end = prev;
                    BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_DEBUG,
                                    "nh_table.ecmp = %p, nh_table.ecmp_end = %p\n",
                                    nh_table.ecmp, nh_table.ecmp_end);
                }
            }
            nh_table.ecmp_count--;
            FREE(ptr);
            ptr = NULL;
            break;
        }
        prev = ptr;
        ptr = ptr->next;
    } while (SAI_STATUS_SUCCESS ==
            (rv = _brcm_sai_list_traverse(_BRCM_SAI_LIST_NH_ECMP_INFO, &ldata, &ldata)));
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh ecmp list traverse\n");
        sem_post(&nh_ecmp_mutex);
        return rv;
    }
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh table add\n");
    }
    sem_post(&nh_ecmp_mutex);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);

    return rv;
}

sai_status_t
_brcm_sai_nh_table_ecmp_get(int nhid, int *count, _brcm_sai_nh_ecmp_t **ecmp)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_table_t nh_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEXT_HOP);

    DATA_CLEAR(nh_table, _brcm_sai_nh_table_t);
    nh_table.nhid = nhid;
    tdata.nh_table = &nh_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NH, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NH(SAI_LOG_LEVEL_ERROR, "error in nh table lookup of nh %d\n", nhid);
        return rv;
    }
    *count = nh_table.ecmp_count;
    *ecmp = nh_table.ecmp;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEXT_HOP);
    return rv;
}

void
_brcm_sai_nh_table_ecmp_lock(int nhid)
{
    sem_wait(&nh_ecmp_mutex);
}

void
_brcm_sai_nh_table_ecmp_unlock(int nhid)
{
    sem_post(&nh_ecmp_mutex);
}

int
_brcm_sai_max_nh_count_get()
{
    return __brcm_sai_nh_max;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_next_hop_api_t next_hop_apis = {
    brcm_sai_create_next_hop,
    brcm_sai_remove_next_hop,
    brcm_sai_set_next_hop_attribute,
    brcm_sai_get_next_hop_attribute
};

