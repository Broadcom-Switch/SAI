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
_brcm_sai_nbr_mac_db_add(bcm_mac_t mac, bcm_l3_intf_t* l3_intf,
                         bcm_l3_host_t *l3_host, int pa, bcm_if_t egr_if_id);
STATIC sai_status_t
_brcm_sai_nbr_mac_db_delete(bcm_mac_t mac, bcm_l3_intf_t* l3_intf,
                            bcm_l3_host_t *l3_host);
STATIC sai_status_t
_brcm_sai_update_host_entry(bcm_l3_host_t *l3_host, bcm_l2_addr_t *l2_addr,
                            bcm_l3_intf_t *l3_intf, bcm_if_t *egr_if_id);
STATIC sai_status_t
_brcm_sai_nbr_nh_intf_update(bool add, bool phy_intf,
                             bcm_if_t l3_intf_id,
                             bcm_l3_host_t *l3_host);
STATIC sai_status_t
_brcm_sai_nbr_bcast_ip_create(_brcm_sai_nbr_table_info_t *nbr_table,
                              bcm_l3_host_t *l3_host, bcm_l3_intf_t *l3_intf,
                              int pa);
STATIC sai_status_t
_brcm_sai_nbr_bcast_ip_remove(_brcm_sai_nbr_table_info_t *nbr_table,
                              bcm_l3_host_t *l3_host, bcm_vlan_t vid);

/*
################################################################################
#                     Local state - non-persistent across WB                   #
################################################################################
*/
static sem_t mac_nbr_mutex;
static sem_t nbr_nh_mutex;
static int _max_nbr = -1;

/*
################################################################################
#                               Neighbor functions                             #
################################################################################
*/

/*
* Routine Description:
*    Create neighbor entry
*
* Arguments:
*    [in] neighbor_entry - neighbor entry
*    [in] attr_count - number of attributes
*    [in] attrs - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_neighbor_entry(_In_ const sai_neighbor_entry_t* neighbor_entry,
                               _In_ uint32_t attr_count,
                               _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    sai_uint32_t port_lag;
    bcm_l2_addr_t l2_addr;
    bcm_l3_host_t l3_host;
    bcm_l3_intf_t l3_intf;
    _brcm_sai_table_data_t data;
    _brcm_sai_indexed_data_t idata;
    _brcm_sai_nbr_table_info_t nbr_table;
    sai_router_interface_type_t rif_type;
    int i, pa = SAI_PACKET_ACTION_FORWARD;
    bcm_mac_t mac, bcast_mac = _BCAST_MAC_ADDR;
    bool got_mac = FALSE, nbr_exists = TRUE, bcast_ip = FALSE;
    bool noroute = FALSE;
    _brcm_sai_data_t gdata;
    int index;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(neighbor_entry);

    bcm_l3_host_t_init(&l3_host);
    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    if (SAI_IP_ADDR_FAMILY_IPV4 == neighbor_entry->ip_address.addr_family)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        l3_host.l3a_ip_addr = ntohl(neighbor_entry->ip_address.addr.ip4);
        nbr_table.nbr.ip4 = l3_host.l3a_ip_addr;
    }
    else if (SAI_IP_ADDR_FAMILY_IPV6 == neighbor_entry->ip_address.addr_family)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(l3_host.l3a_ip6_addr, neighbor_entry->ip_address.addr.ip6,
                   sizeof(l3_host.l3a_ip6_addr));
        l3_host.l3a_flags |= BCM_L3_IP6;
        sal_memcpy(nbr_table.nbr.ip6, l3_host.l3a_ip6_addr,
                   sizeof(l3_host.l3a_ip6_addr));
        if (_BRCM_SAI_IS_ADDR_LINKLOCAL(l3_host.l3a_ip6_addr))
        {
            noroute = nbr_table.noroute = TRUE;
        }
    }
    else
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Invalid IP address family\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS:

                got_mac = true;
                sal_memcpy(mac, attr_list[i].value.mac,
                           sizeof(bcm_mac_t));
                if (MATCH == memcmp(bcast_mac,
                                    mac,
                                    sizeof(bcm_mac_t)))
                {
                    bcast_ip = TRUE;
                }
                break;
            case SAI_NEIGHBOR_ENTRY_ATTR_PACKET_ACTION:
                pa = attr_list[i].value.u32;
                if (SAI_PACKET_ACTION_DROP != pa && SAI_PACKET_ACTION_FORWARD != pa)
                {
                    return SAI_STATUS_INVALID_ATTR_VALUE_0;
                }
                break;
            case SAI_NEIGHBOR_ENTRY_ATTR_NO_HOST_ROUTE:
                /* no host table install */
                noroute = attr_list[i].value.booldata;
                break;
            default:
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_INFO,
                                  "Unknown or unsupported neighbor attribute %d passed\n",
                                  attr_list[i].id);
                _BRCM_SAI_ATTR_LIST_DEFAULT_ERR_CHECK(SAI_NEIGHBOR_ENTRY_ATTR_END);
        }
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Error processing nbr entry attributes", rv);
    }
    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, neighbor_entry->rif_id);
    rif_type = BRCM_SAI_GET_OBJ_SUB_TYPE(neighbor_entry->rif_id);
    port_lag = BRCM_SAI_GET_OBJ_MAP(neighbor_entry->rif_id);
    nbr_table.nbr.rif_id = l3_intf.l3a_intf_id;
    l3_host.l3a_intf = l3_intf.l3a_intf_id;
    if (FALSE == got_mac)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "No mac address found.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (bcast_ip && SAI_ROUTER_INTERFACE_TYPE_VLAN != rif_type)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                          "BCAST ip neighbor only valid on VLAN RIF.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (SAI_ROUTER_INTERFACE_TYPE_PORT == rif_type ||
        _BRCM_SAI_RIF_TYPE_LAG == rif_type)
    {
        if (SAI_ROUTER_INTERFACE_TYPE_PORT == rif_type)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "port: %d\n", port_lag);
        }
        else
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "tid: %d\n", port_lag);
        }
    }
    rv = bcm_l3_intf_get(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 interface get", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Intf: %d, Got vr_id: %d vid: %d\n",
                      l3_intf.l3a_intf_id, l3_intf.l3a_vrf, l3_intf.l3a_vid);
    if (0 != l3_intf.l3a_vrf)
    {
        nbr_table.nbr.vr_id = l3_host.l3a_vrf = l3_intf.l3a_vrf;
    }
    data.nbr_table = &nbr_table;
    if (noroute == FALSE)
    {
        /* Check if host already exists and if so then exit */
        rv = bcm_l3_host_find(0, &l3_host);
        if (BCM_E_NONE == rv)
        {
            rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
            if (SAI_STATUS_ERROR(rv) && SAI_STATUS_ITEM_NOT_FOUND != rv)
            {
                BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Neighbor table lookup", rv);
            }
            if (SAI_STATUS_SUCCESS == rv) {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_INFO, "Skip pre-existing nbr creation.\n");
                goto _sai_nbr_create_exit;
            }
        }
        if (BCM_E_NOT_FOUND != rv)
        {
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host find", rv);
        }
    }
    sal_memcpy(l3_host.l3a_nexthop_mac, mac, sizeof(bcm_mac_t));
    l3_host.l3a_intf = l3_intf.l3a_intf_id;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
    if (SAI_STATUS_ERROR(rv) && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Neighbor table lookup", rv);
    }
    else if (SAI_STATUS_SUCCESS == rv && nbr_table.bcast_ip)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Neighbor bcast_ip exists.");
        goto _sai_nbr_create_exit;
    }
    else if (noroute == TRUE)
    {
        if (rv == SAI_STATUS_SUCCESS)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Neighbor noroute ip exists\n");
            goto _sai_nbr_create_exit;
        }
    }

    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_NBR_ID, 1,
                                                  _max_nbr, &index);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr db table id get", rv);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NBR_ID,
                                        &index, &idata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr id index data get", rv);
        idata.nbr_id.idx = index;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_NBR_ID,
                                        &index, &idata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr id index data set", rv);
        nbr_table.id = index;
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_NBR_COUNT, INC);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global host count increase", rv);
        rv = _brcm_sai_global_data_get(_BRCM_SAI_NBR_COUNT, &gdata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global nbr count", rv);
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Added new Nbr id: %d total count: %d",
                          index, gdata.u32);
    }

    sal_memcpy(data.nbr_table->mac, l3_host.l3a_nexthop_mac, sizeof(bcm_mac_t));
    nbr_table.pa = pa;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info db table entry add", rv);
    if (SAI_ROUTER_INTERFACE_TYPE_VLAN == rif_type)
    {
        bcm_vlan_t vid;
        bcm_if_t egr_if_id = -1;

        vid = l3_intf.l3a_vid;
        if (bcast_ip)
        {
            /* create mcast group and add rule */
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Create bcast ip entry");
            rv = _brcm_sai_nbr_bcast_ip_create(&nbr_table, &l3_host, &l3_intf, pa);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr bcast ip create", rv);
            _brcm_sai_vlan_rif_info_set(vid, TRUE, &nbr_table.nbr);
            goto _sai_nbr_create_exit;
        }
        /* Need to find the port/lag on which this mac exists */
        sal_memcpy(mac, l3_host.l3a_nexthop_mac, sizeof(bcm_mac_t));
        DATA_CLEAR(l2_addr, bcm_l2_addr_t);
        rv = bcm_l2_addr_get(0, mac, vid, &l2_addr);
        if (BCM_E_NOT_FOUND == rv)
        {
            nbr_exists = FALSE;
        }
        if ((rv != BCM_E_NOT_FOUND) && !BCM_SUCCESS(rv))
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in L2 addr get: %s (%d)",
                              bcm_errmsg(rv), rv);
            return BRCM_RV_BCM_TO_SAI(rv);
        }
        if (nbr_exists && SAI_PACKET_ACTION_DROP != pa)
        {
            if ((0 == (l2_addr.flags & BCM_L2_TRUNK_MEMBER)) && (0 == l2_addr.port))
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Incorrect port number.\n");
                return SAI_STATUS_FAILURE;
            }
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Got %s: %d\n",
                              (l2_addr.flags & BCM_L2_TRUNK_MEMBER) ?
                              "trunk" : "port",
                              (l2_addr.flags & BCM_L2_TRUNK_MEMBER) ?
                              l2_addr.tgid : l2_addr.port);
            l3_host.l3a_port_tgid = (l2_addr.flags & BCM_L2_TRUNK_MEMBER) ?
                                     l2_addr.tgid : l2_addr.port;

            if (l2_addr.flags & BCM_L2_TRUNK_MEMBER)
            {
                l3_host.l3a_flags |= BCM_L3_TGID;
            }
            /*
             * Always create egress object for VLAN-based neighbors
             * (if the neighbor is known).
             * (1) Create nextHop first.
             * (2) Pass the nextHop index to use along with the L3 host entry.
             */
            bcm_l3_egress_t l3_eg;
            bcm_if_t if_id;

            bcm_l3_egress_t_init(&l3_eg);
            l3_eg.intf = l3_intf.l3a_intf_id;
            l3_eg.vlan = l3_intf.l3a_vid;
            memcpy(l3_eg.mac_addr, l3_host.l3a_nexthop_mac, sizeof(bcm_mac_t));
            if (l2_addr.flags & BCM_L2_TRUNK_MEMBER) /* LAG */
            {

                l3_eg.trunk = l3_host.l3a_port_tgid;
                l3_eg.flags |= BCM_L3_TGID;
            }
            else
            {
                l3_eg.port = l3_host.l3a_port_tgid;
            }
            rv = bcm_l3_egress_create(0, 0, &l3_eg, &if_id);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 egress create", rv);
            l3_host.l3a_intf = egr_if_id = if_id;
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                              "Created host priv egr obj: %d\n", if_id);
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count increase", rv);
        }
        rv = _brcm_sai_nbr_mac_db_add(mac, &l3_intf, &l3_host, pa, egr_if_id);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "mac nbr db add", rv);
    }
    else /* Port or LAG */
    {
        l3_host.l3a_port_tgid = port_lag;
        if (_BRCM_SAI_RIF_TYPE_LAG == rif_type) /* LAG */
        {
            l3_host.l3a_flags |= BCM_L3_TGID;
        }
        /* Update L2 entry if it exists for port/lag (vlan=0)
         * Checking for vlan=0 makes sense for port-based routing interfaces
         * as the vlan may not be known when creating a static FDB entry for
         * the NH. However, in practice, it seems that the NH FDB entries will
         * be LEARNED.
         */
        sal_memcpy(mac, l3_host.l3a_nexthop_mac, sizeof(bcm_mac_t));
        DATA_CLEAR(l2_addr, bcm_l2_addr_t);
        rv = bcm_l2_addr_get(0, mac, 0, &l2_addr);
        if (BCM_SUCCESS(rv))
        {
            rv = bcm_l2_addr_delete(0, mac, 0);
            l2_addr.vid = l3_intf.l3a_vid;
            rv = bcm_l2_addr_add(0, &l2_addr);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L2 addr add", rv);
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Updated neighbor L2 entry having vid=0\n");
        }
        /*
         * Always create egress object for port/LAG-based neighbors.
         * (1) Create nextHop first.
         * (2) Pass the nextHop index to use along with the L3 host entry.
         */
        bcm_l3_egress_t l3_eg;
        bcm_if_t if_id;

        bcm_l3_egress_t_init(&l3_eg);
        l3_eg.intf = l3_intf.l3a_intf_id;
        l3_eg.vlan = l3_intf.l3a_vid;
        memcpy(l3_eg.mac_addr, l3_host.l3a_nexthop_mac, sizeof(bcm_mac_t));
        if (_BRCM_SAI_RIF_TYPE_LAG == rif_type)
        {
            l3_eg.trunk = l3_host.l3a_port_tgid;
            l3_eg.flags |= BCM_L3_TGID;
        }
        else
        {
            l3_eg.port = l3_host.l3a_port_tgid;
        }
        rv = bcm_l3_egress_create(0, 0, &l3_eg, &if_id);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 egress create", rv);
        l3_host.l3a_intf = nbr_table.if_id = if_id;
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count increase", rv);
    }
    l3_host.l3a_flags |= _BCM_L3_HOST_AS_ROUTE;
    if ((FALSE == nbr_exists) || (SAI_PACKET_ACTION_DROP == pa))
    {
        if (FALSE == noroute)
        {
            rv = _brcm_sai_drop_if_get(&l3_host.l3a_intf);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "getting global drop intf", rv);
        }
        else
        {
            rv = _brcm_sai_drop_if_get(&nbr_table.if_id);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "getting global drop intf", rv);
        }
    }
    if (TRUE == noroute)
    {
        /* create nb db entry right now */
        rv = _brcm_sai_nbr_nh_ref_attach(&l3_host, -1, pa);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Neighbor noroute table add", rv);
        nbr_table.flags = l3_host.l3a_flags;
        nbr_table.port_tgid = l3_host.l3a_port_tgid;
    }
    else
    {
        rv = bcm_l3_host_add(0, &l3_host);
        if (!BCM_SUCCESS(rv) && BCM_E_EXISTS == rv)
        {
            l3_host.l3a_flags |= BCM_L3_REPLACE;
            rv = bcm_l3_host_add(0, &l3_host);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host add", rv);
        }
        else if (!BCM_SUCCESS(rv))
        {
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host add", rv);
        }
    }
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info db table entry add", rv);
_sai_nbr_create_exit:

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

/*
* Routine Description:
*    Remove neighbor entry
*
* Arguments:
*    [in] neighbor_entry - neighbor entry
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_neighbor_entry(_In_ const sai_neighbor_entry_t* neighbor_entry)
{
    sai_status_t rv;
    bcm_if_t _dintf, _if_id;
    bcm_l3_host_t l3_host;
    bcm_l3_intf_t l3_intf;
    _brcm_sai_table_data_t data;
    _brcm_sai_nbr_table_info_t nbr_table;
    sai_router_interface_type_t rif_type;
    bool noroute = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(neighbor_entry))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Null neighbor entry param.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    bcm_l3_host_t_init(&l3_host);
    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    if (SAI_IP_ADDR_FAMILY_IPV4 == neighbor_entry->ip_address.addr_family)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        l3_host.l3a_ip_addr = ntohl(neighbor_entry->ip_address.addr.ip4);
        nbr_table.nbr.ip4 = l3_host.l3a_ip_addr;
    }
    else if (SAI_IP_ADDR_FAMILY_IPV6 == neighbor_entry->ip_address.addr_family)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(l3_host.l3a_ip6_addr, neighbor_entry->ip_address.addr.ip6,
                   sizeof(l3_host.l3a_ip6_addr));
        l3_host.l3a_flags |= BCM_L3_IP6;
        sal_memcpy(nbr_table.nbr.ip6, l3_host.l3a_ip6_addr,
                   sizeof(l3_host.l3a_ip6_addr));
        if (_BRCM_SAI_IS_ADDR_LINKLOCAL(l3_host.l3a_ip6_addr))
        {
            noroute = TRUE;
        }
    }
    else
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Invalid IP Address family.");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, neighbor_entry->rif_id);
    rv = bcm_l3_intf_get(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 interface get", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Got vr_id: %d vid: %d\n",
                      l3_intf.l3a_vrf, l3_intf.l3a_vid);
    nbr_table.nbr.rif_id = l3_intf.l3a_intf_id;
    if (0 != l3_intf.l3a_vrf)
    {
        nbr_table.nbr.vr_id = l3_host.l3a_vrf = l3_intf.l3a_vrf;
    }
    data.nbr_table = &nbr_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
    if (SAI_STATUS_ERROR(rv) && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "Neighbor table lookup", rv);
    }
    else if (SAI_STATUS_SUCCESS == rv && nbr_table.nh_count)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Neighbor NH %d references found.",
                          nbr_table.nh_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    if (nbr_table.bcast_ip) /* no host entry in this case */
    {
        bcm_vlan_t vid = l3_intf.l3a_vid;

        /* remove bcast entry and fp rule */
        rv = _brcm_sai_nbr_bcast_ip_remove(&nbr_table, &l3_host, vid);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "bcast ip remove", rv);
        _brcm_sai_vlan_rif_info_set(vid, FALSE, NULL);
        goto _sai_nbr_remove_exit;
    }
    if ((noroute == FALSE) && (nbr_table.bcast_ip == FALSE))
    {
        rv = bcm_l3_host_find(0, &l3_host);
        if (rv == BCM_E_NOT_FOUND)
        {
            return SAI_STATUS_ITEM_NOT_FOUND;
        }
        if (rv == BCM_E_NONE)
        {
            rv = bcm_l3_host_delete(0, &l3_host);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host delete", rv);
        }
    }
    rif_type = BRCM_SAI_GET_OBJ_SUB_TYPE(neighbor_entry->rif_id);
    rv = _brcm_sai_drop_if_get(&_dintf);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "getting global drop intf", rv);
    /* For VLAN RIFs, the host's egress object may be pointing to the
     * global drop interface. For Port-based RIFs, if NBR's PA=DROP,
     * egress object also will be pointing to global drop interface,
     * so need to get egress object from NBR Table DB.
     */
    if ((SAI_ROUTER_INTERFACE_TYPE_VLAN == rif_type) &&
        (FALSE == noroute))
    {
        _if_id = l3_host.l3a_intf;
    }
    else
    {
        _if_id = nbr_table.if_id;
    }
    if (_if_id != _dintf)
    {
        rv = bcm_l3_egress_destroy(0, _if_id);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 egress destroy", rv);
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, DEC);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count decrease", rv);
    }

    if (SAI_ROUTER_INTERFACE_TYPE_VLAN == rif_type)
    {
        /* Remove this neighbor from DB */
        rv = _brcm_sai_nbr_mac_db_delete(nbr_table.mac, &l3_intf, &l3_host);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "mac nbr db delete", rv);
    }
    {
        _brcm_sai_data_t nbr_data;
        _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_NBR_ID, nbr_table.id);
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_NBR_COUNT, DEC);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global host count decrease", rv);
        rv = _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_NBR_INFO, &data);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr table entry delete", rv);
        rv = _brcm_sai_global_data_get(_BRCM_SAI_NBR_COUNT, &nbr_data);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global nbr count", rv);
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Removed Nbr id: %d total count: %d",
                          nbr_table.id, nbr_data.u32);
    }
_sai_nbr_remove_exit:

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

/*
* Routine Description:
*    Set neighbor attribute value
*
* Arguments:
*    [in] neighbor_entry - neighbor entry
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_neighbor_entry_attribute(_In_ const sai_neighbor_entry_t* neighbor_entry,
                                      _In_ const sai_attribute_t *attr)
{
    bcm_if_t if_id, _dropif;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int _pa = SAI_PACKET_ACTION_FORWARD, pa = -1;
    bool old_lag, new_lag;
    bcm_l2_addr_t l2_addr;
    bcm_l3_host_t l3_host;
    bcm_l3_intf_t l3_intf;
    bcm_l3_egress_t l3_eg;
    bool update_db = FALSE;
    bcm_port_t old_port_lag;
    bcm_mac_t old_mac, new_mac;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nbr_info_t nbr_info;
    bool nbr_priv_egr_obj = FALSE;
    sai_router_interface_type_t rif_type;
    _brcm_sai_nbr_table_info_t nbr_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(neighbor_entry) || IS_NULL(attr))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Null neighbor entry or attr param.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    switch (attr->id)
    {
        case SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS:
            sal_memcpy(new_mac, attr->value.mac,
                       sizeof(l3_host.l3a_nexthop_mac));
            break;
        case SAI_NEIGHBOR_ENTRY_ATTR_PACKET_ACTION:
            pa = attr->value.u32;
            if (SAI_PACKET_ACTION_DROP != pa && SAI_PACKET_ACTION_FORWARD != pa)
            {
                return SAI_STATUS_INVALID_ATTR_VALUE_0;
            }
            break;
        default:
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_INFO,
                              "Unknown or unsupported neighbor attribute %d passed\n",
                              attr->id);
            _BRCM_SAI_ATTR_DEFAULT_ERR_CHECK(SAI_NEIGHBOR_ENTRY_ATTR_END);
    }
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Error processing nbr entry attribute", rv);

    bcm_l3_host_t_init(&l3_host);
    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    l3_host.l3a_flags = 0;
    if (SAI_IP_ADDR_FAMILY_IPV4 == neighbor_entry->ip_address.addr_family)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        l3_host.l3a_ip_addr = ntohl(neighbor_entry->ip_address.addr.ip4);
        nbr_table.nbr.ip4 = l3_host.l3a_ip_addr;
    }
    else if (SAI_IP_ADDR_FAMILY_IPV6 == neighbor_entry->ip_address.addr_family)
    {
        sal_memcpy(l3_host.l3a_ip6_addr, neighbor_entry->ip_address.addr.ip6,
                   sizeof(l3_host.l3a_ip6_addr));
        l3_host.l3a_flags |= BCM_L3_IP6;
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(nbr_table.nbr.ip6, l3_host.l3a_ip6_addr, sizeof(sai_ip6_t));
    }
    else
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Invalid IP Address family.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    rif_type = BRCM_SAI_GET_OBJ_SUB_TYPE(neighbor_entry->rif_id);
    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, neighbor_entry->rif_id);
    rv = bcm_l3_intf_get(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 interface get", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Got vr_id: %d vid: %d\n",
                      l3_intf.l3a_vrf, l3_intf.l3a_vid);
    if (0 != l3_intf.l3a_vrf)
    {
        l3_host.l3a_vrf = l3_intf.l3a_vrf;
        nbr_table.nbr.vr_id = l3_host.l3a_vrf;
    }
    nbr_table.nbr.rif_id = l3_intf.l3a_intf_id;
    tdata.nbr_table = &nbr_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "error in nbr table lookup.\n");
        return rv;
    }
    else if (SAI_STATUS_SUCCESS == rv)
    {
        update_db = TRUE;
        if (TRUE == nbr_table.bcast_ip)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "bcast nbr ip found\n");
            if (-1 != pa)
            {
                rv = bcm_field_entry_enable_set(0, nbr_table.entry,
                         SAI_PACKET_ACTION_DROP == pa ? 0 : 1);
                BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field entry enable/disable", rv);
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                  "bcast nbr ip entry %d toggle: %d\n",
                                  nbr_table.entry,
                                  SAI_PACKET_ACTION_DROP == pa ? 0 : 1);
                nbr_table.pa = pa;
                goto _nbr_modify_exit;
            }
            else
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                                  "Only PA updates supported on bcast ip");
                return SAI_STATUS_INVALID_PARAMETER;
            }
        }
    }
    if_id = l3_intf.l3a_intf_id;
    if (FALSE == nbr_table.noroute)
    {
        rv = bcm_l3_host_find(0, &l3_host);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host find", rv);
    }
    else
    {
        /* populate the l3_host */
        l3_host.l3a_intf = nbr_table.if_id;
        l3_host.l3a_flags = nbr_table.flags;
        l3_host.l3a_port_tgid = nbr_table.port_tgid;
        memcpy(&l3_host.l3a_nexthop_mac, &nbr_table.mac, sizeof(sai_mac_t));
    }
    if_id = l3_host.l3a_intf;
    rv = _brcm_sai_drop_if_get(&_dropif);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "getting global drop intf", rv);

    if (-1 != pa) /* pa processing */
    {
        /* If pa state and discard state match - don't update h/w and skip nh processing */
        if (SAI_ROUTER_INTERFACE_TYPE_VLAN != rif_type)
        {
            if ((SAI_PACKET_ACTION_DROP == pa) && (if_id == _dropif))
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                  "Non vlan intf based nbr already in drop state.\n");
                goto _nbr_modify_exit;
            }
            else if ((SAI_PACKET_ACTION_FORWARD == pa) && (if_id != _dropif))
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                  "Non vlan intf based nbr already in forward state.\n");
                goto _nbr_modify_exit;
            }
        }
        _pa = pa;
    }

    /* Load info for common use in the remaining logic */
    if (nbr_table.noroute == FALSE)
    {
        nbr_priv_egr_obj = TRUE;
    }
    if (SAI_ROUTER_INTERFACE_TYPE_VLAN == rif_type)
    {
        /* Lookup mac-nbr db to find this mac */
        rv = _brcm_sai_mac_nbr_lookup(nbr_table.mac, &l3_intf,
                                      &l3_host,
                                      &nbr_info);
        if (rv == SAI_STATUS_FAILURE)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                              "Error in mac nbr table entry lookup.\n");
            return rv;
        }
        if (rv == SAI_STATUS_ITEM_NOT_FOUND)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Mac Nbr entry not found.\n");
            return rv;
        }
        if (-1 == pa) /* mac processing */
        {
            _pa = nbr_info.pa;
        }
        else /* pa processing */
        {
            if (nbr_info.pa == pa)
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                  "Vlan intf based nbr already in matching pa state.\n");
                goto _nbr_modify_exit;
            }
        }
    }
    else
    {
        /* For Port/LAG-based RIFs, egress object will always be there. */
        rv = bcm_l3_egress_get(0, if_id, &l3_eg);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 egress get", rv);
    }
    /* Common flag value setup */
    l3_host.l3a_flags |= BCM_L3_REPLACE;
    sal_memcpy(old_mac, nbr_table.mac, sizeof(bcm_mac_t));
    if (-1 == pa) /* mac address processing */
    {
        old_lag = new_lag = (l3_eg.flags & BCM_L3_TGID) ? TRUE : FALSE;
        old_port_lag = (l3_eg.flags & BCM_L3_TGID) ? l3_eg.trunk : l3_eg.port;
        /* Check if old and new mac are the same and skip processing if so */
        if (MATCH == memcmp(&old_mac, &new_mac, sizeof(bcm_mac_t)))
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                              "No change in mac address.\n");
            goto _nbr_modify_exit;
        }
        if (SAI_ROUTER_INTERFACE_TYPE_VLAN == rif_type)
        {
            bcm_if_t egr_if_id = -1;
            bool new_mac_known = TRUE;

            /* Need to find the port/lag on which this mac exists */
            DATA_CLEAR(l2_addr, bcm_l2_addr_t);
            rv = bcm_l2_addr_get(0, new_mac, l3_intf.l3a_vid, &l2_addr);
            if (BCM_E_NOT_FOUND == rv)
            {
                bcm_if_t _if_id;

                new_mac_known = FALSE;
                /* Set new mac and recreate entry */
                /* Update host entry -> DROP */
                _if_id = l3_host.l3a_intf;
                if (nbr_priv_egr_obj)
                {
                    l3_host.l3a_intf = _dropif;
                }
                nbr_table.if_id = _dropif;
                if (FALSE == nbr_table.noroute)
                {
                    rv = bcm_l3_host_add(0, &l3_host);
                    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host replace -> DROP", rv);
                }
                /* Add new DB entry */
                rv = _brcm_sai_nbr_mac_db_add(new_mac, &l3_intf, &l3_host,
                                              _pa, nbr_priv_egr_obj && _if_id ?
                                              _if_id : -1);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "mac nbr db add", rv);
                /* goto _nbr_modify_exit; Can't exit right now. */
                goto _remove_old_mac; /* FIXME: move the common code into a routine */
            }
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L2 address get", rv);
            if ((0 == (l2_addr.flags & BCM_L2_TRUNK_MEMBER)) && (0 == l2_addr.port))
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Incorrect port number.\n");
                return SAI_STATUS_FAILURE;
            }
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Got %s: %d\n",
                              (l2_addr.flags & BCM_L2_TRUNK_MEMBER) ?
                              "trunk" : "port",
                              (l2_addr.flags & BCM_L2_TRUNK_MEMBER) ?
                              l2_addr.tgid : l2_addr.port);
            new_lag = (l2_addr.flags & BCM_L2_TRUNK_MEMBER) ? TRUE : FALSE;
            if (SAI_PACKET_ACTION_FORWARD == _pa)/* Check pa and update host as required */
            {
                rv = _brcm_sai_update_host_entry(&l3_host, &l2_addr, &l3_intf, &egr_if_id);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "host entry update.", rv);
            }
            rv = _brcm_sai_nbr_mac_db_add(new_mac, &l3_intf, &l3_host,
                                          _pa, egr_if_id);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "mac nbr db add", rv);
            /* Set new mac and recreate entry */
            if (FALSE == nbr_priv_egr_obj)
            {
                sal_memcpy(l3_host.l3a_nexthop_mac, new_mac, sizeof(bcm_mac_t));
            }
            if (FALSE == nbr_table.noroute)
            {
                rv = bcm_l3_host_add(0, &l3_host);
                BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host add", rv);
            }
_remove_old_mac: /* FIXME: move this common code into a routine */
            /* If old MAC is known, remove old egress object. */
            DATA_CLEAR(l2_addr, bcm_l2_addr_t);
            rv = bcm_l2_addr_get(0, old_mac, l3_intf.l3a_vid, &l2_addr);
            if (BCM_E_NONE == rv)
            {
                rv = _brcm_sai_nbr_nh_intf_update(FALSE, FALSE, l3_intf.l3a_intf_id,
                                                  &l3_host);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "NH intf update", rv);
            }
            else if (BCM_E_NOT_FOUND != rv)
            {
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "l2 addr get", rv);

            }
            /* Remove old DB entry */
            rv = _brcm_sai_nbr_mac_db_delete(old_mac, &l3_intf, &l3_host);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "mac nbr db delete", rv);
            if (FALSE == new_mac_known)
            {
                if (update_db)
                {
                    sal_memcpy(&nbr_table.mac, &new_mac, sizeof(bcm_mac_t));
                }
                goto _nbr_update_db;
            }
            if (SAI_PACKET_ACTION_FORWARD == _pa)
            {
                /* Since old egress object was removed or never existed,
                 * need to call _brcm_sai_nbr_nh_intf_update() to have a
                 * new egress object created and to have all routes updated,
                 * etc.
                 */
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Updating NH intf.");
                rv = _brcm_sai_nbr_nh_intf_update(TRUE, FALSE, l3_intf.l3a_intf_id,
                                                  &l3_host);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nh intf update", rv);
            }
        }
        else /* port and lag */
        {
            _pa = (l3_host.l3a_intf == _dropif) ? SAI_PACKET_ACTION_DROP :
                SAI_PACKET_ACTION_FORWARD;
            if (nbr_priv_egr_obj)
            {
                old_port_lag = l3_eg.flags & BCM_L3_TGID ? l3_eg.trunk : l3_eg.port;
            }
            rv = _brcm_sai_nh_mac_update(tdata.nbr_table->if_id, old_lag, new_lag, old_port_lag,
                                         old_port_lag, l3_intf.l3a_vid,
                                         l3_intf.l3a_intf_id, old_mac, new_mac);
            if (SAI_STATUS_ITEM_NOT_FOUND != rv)
            {
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nh mac update", rv);
            }
            rv = SAI_STATUS_SUCCESS;
        }
        if (update_db)
        {
            sal_memcpy(&nbr_table.mac, &new_mac, sizeof(bcm_mac_t));
        }
    }
    else /* pa processing */
    {
        bool nbr_exists = TRUE;
        bool phy_intf = FALSE;

        if (update_db)
        {
            nbr_table.pa = pa;
        }

        /* update egress object in host table if pa state does not match */
        if (SAI_ROUTER_INTERFACE_TYPE_VLAN == rif_type)
        {
            /* Need to find the port/lag on which the mac exists */
            DATA_CLEAR(l2_addr, bcm_l2_addr_t);
            rv = bcm_l2_addr_get(0, old_mac, l3_intf.l3a_vid, &l2_addr);
            if (BCM_E_NOT_FOUND == rv)
            {
                nbr_exists = FALSE;
            }
            /* update state */
            rv = _brcm_sai_nbr_mac_db_add(nbr_priv_egr_obj ?
                                          old_mac : l3_host.l3a_nexthop_mac,
                                          &l3_intf, &l3_host, _pa,
                                          nbr_priv_egr_obj ? l3_host.l3a_intf : -1);
            if (SAI_STATUS_ERROR(rv))
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in mac nbr table entry update.");
            }
            if (FALSE == nbr_exists)
            {
                /* unkown neighbor, just update neighbor table entry with new pa configuration.*/
                goto _nbr_update_db;
            }
            if (SAI_PACKET_ACTION_FORWARD == _pa)
            {
                rv = _brcm_sai_update_host_entry(&l3_host, &l2_addr, &l3_intf, NULL);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "host entry update.", rv);
                nbr_table.if_id = l3_host.l3a_intf;
            }
        }
        else
        {
            phy_intf = TRUE;
        }
        if (SAI_PACKET_ACTION_DROP == _pa)
        {
            l3_host.l3a_intf = _dropif;
            if (SAI_ROUTER_INTERFACE_TYPE_VLAN == rif_type)
            {
                nbr_table.if_id = _dropif;
            }
        }
        else if ((SAI_PACKET_ACTION_FORWARD == _pa) &&
                 (SAI_ROUTER_INTERFACE_TYPE_VLAN != rif_type))
        {
            l3_host.l3a_intf = nbr_table.if_id;
        }
        if (FALSE == nbr_table.noroute)
        {
            rv = bcm_l3_host_add(0, &l3_host);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host add", rv);
        }
        if (SAI_PACKET_ACTION_FORWARD == pa)
        {
            rv = _brcm_sai_nbr_nh_intf_update(TRUE, phy_intf ? TRUE : FALSE,
                                              l3_intf.l3a_intf_id,
                                              &l3_host);
        }
        else
        {
            rv = _brcm_sai_nbr_nh_intf_update(FALSE, phy_intf ? TRUE : FALSE,
                                              l3_intf.l3a_intf_id,
                                              &l3_host);
        }
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nh intf update", rv);
    }
_nbr_update_db:
    if (update_db)
    {
        tdata.nbr_table->flags = l3_host.l3a_flags;
        tdata.nbr_table->port_tgid = l3_host.l3a_port_tgid;
        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info db table entry add", rv);
    }
_nbr_modify_exit:
    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

/*
* Routine Description:
*    Get neighbor attribute value
*
* Arguments:
*    [in] neighbor_entry - neighbor entry
*    [in] attr_count - number of attributes
*    [inout] attrs - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_neighbor_entry_attribute(_In_ const sai_neighbor_entry_t* neighbor_entry,
                                      _In_ uint32_t attr_count,
                                      _Inout_ sai_attribute_t *attr_list)
{
    bool noroute = FALSE;
    sai_status_t rv;
    bcm_l3_host_t l3_host;
    bcm_l3_intf_t l3_intf;
    bcm_if_t _dropif;
    bcm_l3_egress_t l3_eg;
    _brcm_sai_table_data_t data;
    _brcm_sai_nbr_table_info_t nbr_table;
    int i, pa = SAI_PACKET_ACTION_FORWARD;
    bcm_mac_t mac, bcast_mac = _BCAST_MAC_ADDR;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(neighbor_entry);

    bcm_l3_host_t_init(&l3_host);
    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    if (SAI_IP_ADDR_FAMILY_IPV4 == neighbor_entry->ip_address.addr_family)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        l3_host.l3a_ip_addr = ntohl(neighbor_entry->ip_address.addr.ip4);
        nbr_table.nbr.ip4 = l3_host.l3a_ip_addr;
    }
    else if (SAI_IP_ADDR_FAMILY_IPV6 == neighbor_entry->ip_address.addr_family)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(l3_host.l3a_ip6_addr, neighbor_entry->ip_address.addr.ip6,
                   sizeof(l3_host.l3a_ip6_addr));
        l3_host.l3a_flags |= BCM_L3_IP6;
        sal_memcpy(nbr_table.nbr.ip6, l3_host.l3a_ip6_addr,
                   sizeof(l3_host.l3a_ip6_addr));
    }
    else
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Invalid IP address family\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    bcm_l3_intf_t_init(&l3_intf);
    l3_intf.l3a_intf_id = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, neighbor_entry->rif_id);
    rv = bcm_l3_intf_get(0, &l3_intf);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 interface get", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Intf: %d, Got vr_id: %d vid: %d\n",
                      l3_intf.l3a_intf_id, l3_intf.l3a_vrf, l3_intf.l3a_vid);
    nbr_table.nbr.rif_id = l3_intf.l3a_intf_id;
    nbr_table.nbr.vr_id = l3_host.l3a_vrf = l3_intf.l3a_vrf;
    data.nbr_table = &nbr_table;
    /* First check the local DB */
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
    if (SAI_STATUS_ERROR(rv) && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "Neighbor table lookup", rv);
    }
    if (SAI_STATUS_SUCCESS == rv)
    {
        if (nbr_table.bcast_ip)
        {
            memcpy(&mac, &bcast_mac, sizeof(sai_mac_t));
        }
        else
        {
            memcpy(&mac, &nbr_table.mac, sizeof(sai_mac_t));
        }
        pa = nbr_table.pa;
        noroute = nbr_table.noroute;
    }
    else
    {
        /* Else fetch info from SDK */
        rv = bcm_l3_host_find(0, &l3_host);
        if (SAI_STATUS_ERROR(rv) && SAI_STATUS_ITEM_NOT_FOUND != rv)
        {
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 host find", rv);
        }
        if (SAI_STATUS_SUCCESS == rv)
        {
            rv = _brcm_sai_drop_if_get(&_dropif);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "getting global drop intf", rv);
            /* Check the egress object */
            rv = bcm_l3_egress_get(0, l3_host.l3a_intf, &l3_eg);
            if (SAI_STATUS_ERROR(rv) && SAI_STATUS_ITEM_NOT_FOUND != rv)
            {
                BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 egress get", rv);
            }
            if (SAI_STATUS_SUCCESS == rv)
            {
                sal_memcpy(&mac, l3_eg.mac_addr, sizeof(bcm_mac_t));
            }
            else
            {
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "L3 egress get", rv);
            }
            if (l3_host.l3a_intf == _dropif)
            {
                pa = SAI_PACKET_ACTION_DROP;
            }
        }
        else
        {
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "Host table lookup", rv);
        }
    }
    for (i = 0; i < attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS:
                sal_memcpy(&attr_list[i].value.mac, &mac,
                           sizeof(bcm_mac_t));
                break;
            case SAI_NEIGHBOR_ENTRY_ATTR_PACKET_ACTION:
                attr_list[i].value.u32 = pa;
                break;
            case SAI_NEIGHBOR_ENTRY_ATTR_NO_HOST_ROUTE:
                attr_list[i].value.booldata = noroute;
                break;
            default:
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_INFO,
                                  "Unknown or unsupported neighbor attribute %d passed\n",
                                  attr_list[i].id);
                _BRCM_SAI_ATTR_LIST_DEFAULT_ERR_CHECK(SAI_NEIGHBOR_ENTRY_ATTR_END);
        }
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Error processing NBR attributes", rv);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);

    return rv;
}

/*
* Routine Description:
*    Remove all neighbor entries
*
* Arguments:
*    None
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_all_neighbor_entries(_In_ sai_object_id_t switch_id)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);

    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
/* Routine to create nbr state */
sai_status_t
_brcm_sai_alloc_nbr()
{
    int max;
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    if (sem_init(&mac_nbr_mutex, 1, 1) < 0)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "sem init failed\n");
        return SAI_STATUS_FAILURE;
    }
    if (sem_init(&nbr_nh_mutex, 1, 1) < 0)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "sem init failed\n");
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_l3_config_get(8, &max);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 max host", rv);
    max /= 2;   /* FIXME:  For some reason, we are only able to populate 1/2
                 * the L3 Host entries returned by the SDK.
                 */
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_db_table_create(_BRCM_SAI_TABLE_MAC_NBR, max))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_CRITICAL,
                          "Error creating mac nbr table !!\n");
        return SAI_STATUS_FAILURE;
    }
    /* Now traverse the MAC NBR table and create NBR lists */
    rv = _brcm_sai_db_table_node_list_init_v2(_BRCM_SAI_TABLE_MAC_NBR,
                                              _BRCM_SAI_LIST_MAC_NBRS);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "creating mac nbr table node nbr lists", rv);

    _max_nbr = _BRCM_SAI_MAX_BCAST_IP+max;
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_db_table_create(_BRCM_SAI_TABLE_NBR_INFO, _max_nbr))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_CRITICAL,
                          "creating nbr info table !!\n");
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_NBR_ID,
                                     _max_nbr+1);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr id array init", rv);
    /* Now traverse the NBR table and create lists */
    rv = _brcm_sai_db_table_node_list_init(_BRCM_SAI_TABLE_NBR_INFO,
                                           _BRCM_SAI_LIST_NBR_NHS);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "creating nbr info table node nhs lists", rv);
    rv = _brcm_sai_db_table_node_list_init(_BRCM_SAI_TABLE_NBR_INFO,
                                           _BRCM_SAI_LIST_NBR_BCAST_EOBJS);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "creating nbr info table node eobjs lists", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return SAI_STATUS_SUCCESS;
}

/* Routine to free up nbr state */
sai_status_t
_brcm_sai_free_nbr()
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    /* Traverse the MAC NBR table and free NBR lists */
    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_MAC_NBR,
                                           _BRCM_SAI_LIST_MAC_NBRS);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "mac nbr table node nbr lists", rv);
    sem_destroy(&mac_nbr_mutex);
    /* Traverse the NBR table and free NH, EOBJ lists */
    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_NBR_INFO,
                                           _BRCM_SAI_LIST_NBR_NHS);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info table node nhs lists", rv);
    rv = _brcm_sai_db_table_node_list_free(_BRCM_SAI_TABLE_NBR_INFO,
                                           _BRCM_SAI_LIST_NBR_BCAST_EOBJS);
    sem_destroy(&nbr_nh_mutex);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info table node eobjs lists", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_NBR_ID, 1,
                                      _max_nbr+1, -1);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr id array free", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_update_host_entry(bcm_l3_host_t *l3_host, bcm_l2_addr_t *l2_addr,
                            bcm_l3_intf_t *l3_intf, bcm_if_t *egr_if_id)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_l3_egress_t l3_eg;
    bcm_if_t if_id;
    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    /*
    ** Always create egress object for VLAN-based neighbors
    ** (1) Create nextHop first.
    ** (2) Pass the nextHop index to use along with the L3 host entry.
    */
    bcm_l3_egress_t_init(&l3_eg);
    l3_eg.intf = l3_intf->l3a_intf_id;
    l3_eg.vlan = l3_intf->l3a_vid;
    memcpy(l3_eg.mac_addr, l2_addr->mac, sizeof(bcm_mac_t));
    if (l2_addr->flags & BCM_L2_TRUNK_MEMBER) /* LAG */
    {
        l3_eg.trunk = l2_addr->tgid;
        l3_eg.flags |= BCM_L3_TGID;
    }
    else
    {
        l3_eg.port = l2_addr->port;
    }
    rv = bcm_l3_egress_create(0, 0, &l3_eg, &if_id);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "L3 egress create", rv);
    l3_host->l3a_intf = if_id;
    if (egr_if_id)
    {
        *egr_if_id = if_id;
    }
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count increase", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

STATIC sai_status_t
_brcm_sai_nbr_nh_intf_update(bool add, bool phy_intf,
                             bcm_if_t l3_intf_id,
                             bcm_l3_host_t *l3_host)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nbr_table_info_t nbr_table;
    _brcm_sai_nh_list_t *old_nhs;

    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    nbr_table.nbr.vr_id = l3_host->l3a_vrf;
    if (l3_host->l3a_flags & BCM_L3_IP6)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(nbr_table.nbr.ip6, l3_host->l3a_ip6_addr, sizeof(sai_ip6_t));
    }
    else
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        nbr_table.nbr.ip4 = l3_host->l3a_ip_addr;
    }
    nbr_table.nbr.rif_id = l3_intf_id;

    /* Search for this host and get nh data */
    tdata.nbr_table = &nbr_table;
    sem_wait(&nbr_nh_mutex);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "No nh associated with this nbr.");
        sem_post(&nbr_nh_mutex);
        return SAI_STATUS_SUCCESS;
    }
    else if (0 == nbr_table.nh_count)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "No nh associated with this nbr.");
        sem_post(&nbr_nh_mutex);
        return SAI_STATUS_SUCCESS;
    }
    else if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in nbr nh table entry lookup.");
        sem_post(&nbr_nh_mutex);
        return rv;
    }
    /* For all associated nhs, invoke update routine */
    old_nhs = nbr_table.nhs;
    while (old_nhs)
    {
        rv = _brcm_sai_nh_intf_update(old_nhs->nhid, add,
                                      phy_intf, l3_host);
        BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "nh intf update", rv, nbr_nh_mutex);
        old_nhs = old_nhs->next;
    }
    sem_post(&nbr_nh_mutex);

    return rv;
}

sai_status_t
_brcm_sai_nbr_nh_ref_attach(bcm_l3_host_t *l3_host, int nhid, int pa)
{
    int index;
    sai_status_t rv;
    _brcm_sai_data_t data;
    _brcm_sai_nh_list_t *nh;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t idata;
    _brcm_sai_nbr_table_info_t nbr_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);
    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    nbr_table.nbr.vr_id = l3_host->l3a_vrf;
    if (l3_host->l3a_flags & BCM_L3_IP6)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(nbr_table.nbr.ip6, l3_host->l3a_ip6_addr, sizeof(sai_ip6_t));
    }
    else
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        nbr_table.nbr.ip4 = l3_host->l3a_ip_addr;
    }
    nbr_table.nbr.rif_id = l3_host->l3a_intf;
    /* Add an entry into this host's table DB */
    tdata.nbr_table = &nbr_table;
    sem_wait(&nbr_nh_mutex);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "error in nbr table lookup.\n");
        sem_post(&nbr_nh_mutex);
        return rv;
    }
    if (nhid != -1)
    {
        nh = ALLOC_CLEAR(1, sizeof(_brcm_sai_nh_list_t));
        if (IS_NULL(nh))
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                              sizeof(_brcm_sai_nh_list_t));
            sem_post(&nbr_nh_mutex);
            return SAI_STATUS_NO_MEMORY;
        }
        nh->nhid = nhid;
        if (0 == nbr_table.nh_count)
        {
            nbr_table.nhs = nh;
        }
        else
        {
            nbr_table.nhs_end->next = nh;
        }
        nbr_table.nhs_end = nh;
        nbr_table.nh_count++;
    }
    else
    {
        nbr_table.noroute = TRUE;
    }
    if (-1 != pa)
    {
        nbr_table.pa = pa;
    }

    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_NBR_ID, 1,
                                                  _max_nbr, &index);
        BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "nbr db table id get", rv, nbr_nh_mutex);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NBR_ID,
                                        &index, &idata);
        BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "nbr id index data get", rv, nbr_nh_mutex);
        idata.nbr_id.idx = index;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_NBR_ID,
                                        &index, &idata);
        BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "nbr id index data set", rv, nbr_nh_mutex);
        nbr_table.id = index;
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_NBR_COUNT, INC);
        BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "global host count increase", rv, nbr_nh_mutex);
        rv = _brcm_sai_global_data_get(_BRCM_SAI_NBR_COUNT, &data);
        BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "global host count get", rv, nbr_nh_mutex);
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Added new Nbr id: %d total count: %d",
                          index, data.u32);
    }
    nbr_table.flags = l3_host->l3a_flags;
    nbr_table.port_tgid = l3_host->l3a_port_tgid;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
    BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "nbr table entry add", rv, nbr_nh_mutex);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Nbr table %d nh %d entry added,"
                      " count %d flags 0x%x rif %d noroute %d",
                      nbr_table.id, nhid, nbr_table.nh_count,
                      nbr_table.flags, nbr_table.nbr.rif_id, nbr_table.noroute);
    sem_post(&nbr_nh_mutex);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

sai_status_t
_brcm_sai_nbr_nh_ref_detach(bcm_l3_host_t *l3_host, int nhid, bool lookup_host)
{
    sai_status_t rv;
    bool match = FALSE;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nh_list_t *ptr, *prev;
    _brcm_sai_nbr_table_info_t nbr_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);
    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    nbr_table.nbr.vr_id = l3_host->l3a_vrf;
    if (l3_host->l3a_flags & BCM_L3_IP6)
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(nbr_table.nbr.ip6, l3_host->l3a_ip6_addr, sizeof(sai_ip6_t));
    }
    else
    {
        nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        nbr_table.nbr.ip4 = l3_host->l3a_ip_addr;
    }
    nbr_table.nbr.rif_id = l3_host->l3a_intf;
    tdata.nbr_table = &nbr_table;
    sem_wait(&nbr_nh_mutex);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
    BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "NBR table lookup", rv, nbr_nh_mutex);
    if (0 == nbr_table.nh_count || IS_NULL(nbr_table.nhs))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                          "Zero associated nh count found for this nbr.");
        sem_post(&nbr_nh_mutex);
        return SAI_STATUS_FAILURE;
    }
    ptr = nbr_table.nhs;
    prev = ptr;
    if (1 == nbr_table.nh_count)
    {
        if (nhid != ptr->nhid)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "One and only nh (%d) "
                              "does not match (%d) for this nbr.", ptr->nhid, nhid);
            sem_post(&nbr_nh_mutex);
            return SAI_STATUS_FAILURE;
        }
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                          "Removing matched nhid (%d) for nbr id %d.",
                          nhid, nbr_table.id);
        nbr_table.nhs_end = nbr_table.nhs = NULL;
        match = TRUE;
    }
    else
    {
        while (ptr)
        {
            if (nhid == ptr->nhid)
            {
                if (ptr == nbr_table.nhs)
                {
                    nbr_table.nhs = ptr->next;
                }
                else
                {
                    prev->next = ptr->next;
                }
                if (IS_NULL(ptr->next))
                {
                    nbr_table.nhs_end = prev;
                }
                match = TRUE;
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                  "Removing matched nhid (%d) for nbr id %d.",
                                  nhid, nbr_table.id);
                break;
            }
            prev = ptr;
            ptr = ptr->next;
        }
    }
    if (FALSE == match)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Nh (%d) "
                          "did not match for this nbr.", nhid);
        sem_post(&nbr_nh_mutex);
        return SAI_STATUS_FAILURE;
    }
    else
    {
        nbr_table.nh_count--;
        FREE_CLEAR(ptr);
    }
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
    BRCM_SAI_RV_CHK_SEM_POST(SAI_API_NEIGHBOR, "nbr table entry add", rv, nbr_nh_mutex);
    sem_post(&nbr_nh_mutex);
    return rv;
}

STATIC sai_status_t
_brcm_sai_nbr_bcast_ip_create(_brcm_sai_nbr_table_info_t *nbr_table,
                              bcm_l3_host_t *l3_host, bcm_l3_intf_t *l3_intf,
                              int pa)
{
    sai_status_t rv;
    int count, index;
    bcm_gport_t gport = -1;
    bcm_multicast_t mcg;
    _brcm_sai_data_t gdata;
    bcm_field_entry_t entry;
    bcm_l3_egress_t egr_obj;
    bcm_if_t egr_oid, encap;
    _brcm_sai_table_data_t data;
    _brcm_sai_indexed_data_t idata;
    _brcm_sai_egress_objects_t *eobj = NULL, *prev = NULL;
    _brcm_sai_vlan_membr_list_t *ptr, *list;
    bcm_mac_t dst_mac = _BCAST_MAC_ADDR;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);


    rv = _brcm_sai_global_data_get(_BRCM_SAI_BCAST_IP_COUNT, &gdata);
    if (_BRCM_SAI_MAX_BCAST_IP <= gdata.u32)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Max bcast ip limit reached: %d",
                          gdata.u32);
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }

    rv = _brcm_sai_vlan_obj_members_get(l3_intf->l3a_vid, &count, &list);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "vlan members list get", rv);
    rv = bcm_multicast_create(0, BCM_MULTICAST_TYPE_L3, &mcg);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "bcast ip mcast group create", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "bcast ip mcast group %x created", mcg);
    if (count)
    {
        ptr = list;
        while(ptr)
        {
            bcm_l3_egress_t_init(&egr_obj);
            egr_obj.flags = BCM_L3_IPMC;
            egr_obj.intf = l3_intf->l3a_intf_id;
            /* bcast mac as destination mac */
            sal_memcpy(&egr_obj.mac_addr, &dst_mac, sizeof(bcm_mac_t));
            eobj = ALLOC_CLEAR(1, sizeof(_brcm_sai_egress_objects_t));
            if (IS_NULL(eobj))
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                                  sizeof(_brcm_sai_egress_objects_t));
                return SAI_STATUS_NO_MEMORY;
            }
            if (SAI_OBJECT_TYPE_PORT == ptr->membr.type)
            {
                egr_obj.port = ptr->membr.val;
                eobj->type = SAI_OBJECT_TYPE_PORT;
                BCM_GPORT_LOCAL_SET(gport, ptr->membr.val);
            }
            else
            {
                egr_obj.trunk = ptr->membr.val;
                egr_obj.flags |= BCM_L3_TGID;
                eobj->type = SAI_OBJECT_TYPE_LAG;
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                                &ptr->membr.val, &idata);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data get", rv);
                idata.lag_info.bcast_ip = TRUE;
                idata.lag_info.nbr = nbr_table->nbr;
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                                &ptr->membr.val, &idata);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data set", rv);
            }
            rv = bcm_l3_egress_create(0, 0, &egr_obj, &egr_oid);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress create", rv);
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count increase", rv);

            eobj->eoid = egr_oid;
            eobj->ptid = ptr->membr.val;
            eobj->next = NULL;
            if (IS_NULL(nbr_table->eobjs))
            {
                nbr_table->eobjs = eobj;
            }
            else
            {
                prev->next = eobj;
            }
            prev = eobj;
            rv = bcm_multicast_egress_object_encap_get(0, mcg, egr_oid, &encap);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress obj encap get", rv);
            if (SAI_OBJECT_TYPE_PORT == ptr->membr.type)
            {
                rv = bcm_multicast_egress_add(0, mcg, gport, encap);
                BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "mcast egress add", rv);
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                 "Added mcast member port[%d]\n", ptr->membr.val);
            }
            else
            {
                bcm_port_t port;
                int p, max, actual;
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
                rv = bcm_trunk_get(0, ptr->membr.val, &trunk_info, max, members, &actual);
                BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
                for (p=0; p<actual; p++)
                {
                    rv = bcm_port_local_get(0, members[p].gport, &port);
                    BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                                          members);
                    if (!(members[p].flags & BCM_TRUNK_MEMBER_EGRESS_DISABLE))
                    {
                        BCM_GPORT_LOCAL_SET(gport, port);
                        rv = bcm_multicast_egress_add(0, mcg, gport, encap);
                        BRCM_SAI_API_CHK_FREE(SAI_API_NEIGHBOR, "mcast egress add",
                                              rv, members);
                        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                         "Added mcast member lag[%d].port[%d]\n",
                                         ptr->membr.val, port);
                        if (DEV_IS_THX())
                        {
                            /* Just add one port */
                            break;
                        }
                    }
                    else
                    {
                        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                         "Skip adding disabled mcast member lag[%d].port[%d]\n",
                                         ptr->membr.val, port);
                    }
                }
                CHECK_FREE(members);
            }
            ptr = ptr->next;
        }
    }
    rv = bcm_field_entry_create(0, _brcm_sai_global_trap_group_get(0),
                                &entry);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field entry create", rv);
    /* FIXME: temporary fix to elevate priority for BCAST IP rules. */
    rv = bcm_field_entry_prio_set(0, entry, 100);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "entry priority set", rv);
    rv = bcm_field_qualify_DstIp(0, entry, nbr_table->nbr.ip4,
                                 _BRCM_SAI_MASK_32);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field qualify dip", rv);
    /* Rif mac as incoming mac */
    rv = bcm_field_qualify_DstMac(0, entry, l3_intf->l3a_mac_addr,
                                  dst_mac);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field qualify dmac", rv);
    /* Routed packet? */
    rv = bcm_field_qualify_L3DestRouteHit(0, entry, 1, 1);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field qualify dest route hit", rv);
    rv = bcm_field_action_add(0, entry, bcmFieldActionRedirectIpmc, mcg, 0);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field action add", rv);
    rv = bcm_field_entry_install(0, entry);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "bcast ip entry %d created", entry);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field entry install", rv);
    if (SAI_PACKET_ACTION_DROP == pa)
    {
        rv = bcm_field_entry_enable_set(0, entry, 0);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field entry disable", rv);
    }
    nbr_table->bcast_ip = TRUE;
    nbr_table->pa = pa;
    nbr_table->entry = entry;
    nbr_table->vid = l3_intf->l3a_vid;
    nbr_table->mcg = mcg;
    nbr_table->eobj_count = count;
    nbr_table->eobjs_end = eobj;
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_NBR_ID, 1,
                                              _max_nbr, &index);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr db table id get", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NBR_ID,
                                    &index, &idata);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr id index data get", rv);
    idata.nbr_id.idx = index;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_NBR_ID,
                                    &index, &idata);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr id index data set", rv);
    nbr_table->id = index;
    data.nbr_table = nbr_table;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info db table entry add", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "bcast ip nbr [%d][%x] id %d created with %d members in vid %d",
                      nbr_table->nbr.vr_id, nbr_table->nbr.ip4, nbr_table->id,
                      nbr_table->eobj_count, nbr_table->vid);
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_BCAST_IP_COUNT, INC);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global bcast ip count increase", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

STATIC sai_status_t
_brcm_sai_nbr_bcast_ip_remove(_brcm_sai_nbr_table_info_t *nbr_table,
                              bcm_l3_host_t *l3_host, bcm_vlan_t vid)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t idata;
    _brcm_sai_egress_objects_t *ptr, *prev;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Removing bcast ip [%d][%x] entry %d with "
                      "%d eobjs.", nbr_table->nbr.vr_id,nbr_table->nbr.ip4,
                      nbr_table->entry, nbr_table->eobj_count);
    rv = bcm_field_entry_destroy(0, nbr_table->entry);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "field entry destroy", rv);
    rv = bcm_multicast_destroy(0, nbr_table->mcg);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "bcast ip mcast group destroy", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Removed bcast ip mcast grp: %x",
                      nbr_table->mcg);
    if (nbr_table->eobj_count)
    {
        ptr = nbr_table->eobjs;
        while (ptr)
        {
            rv = bcm_l3_egress_destroy(0, ptr->eoid);
            BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress object destroy", rv);
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Removed egress object: %d\n",
                              ptr->eoid);
            rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, DEC);
            BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count decrease", rv);
            if (SAI_OBJECT_TYPE_LAG == ptr->type)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                                &ptr->ptid, &idata);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data get", rv);
                idata.lag_info.bcast_ip = FALSE;
                DATA_CLEAR(idata.lag_info.nbr, _brcm_sai_nbr_t);
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                                &ptr->ptid, &idata);
                BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data set", rv);
            }
            prev = ptr;
            ptr = ptr->next;
            FREE_CLEAR(prev);
        }
    }
    if (0 == nbr_table->nh_count)
    {
        (void)_brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_NBR_ID,
                                                nbr_table->id);
        tdata.nbr_table = nbr_table;
        rv = _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_NBR_INFO, &tdata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr table entry delete", rv);
        rv = _brcm_sai_global_data_bump(_BRCM_SAI_BCAST_IP_COUNT, DEC);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global bcast ip count increase", rv);
    }
    else
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Neighbor NH %d references found.",
                          nbr_table->nh_count);
        return SAI_STATUS_OBJECT_IN_USE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

sai_status_t
_brcm_sai_nbr_bcast_member_add(_brcm_sai_nbr_t *nbr, bcm_if_t intf,
                               uint8_t type, int port_tid)
{
    sai_status_t rv;
    bcm_gport_t gport;
    bcm_l3_egress_t egr_obj;
    bcm_if_t egr_oid, encap;
    _brcm_sai_table_data_t data;
    _brcm_sai_egress_objects_t *eobj;
    bcm_mac_t dst_mac = _BCAST_MAC_ADDR;
    _brcm_sai_nbr_table_info_t nbr_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    nbr_table.nbr = *nbr;
    data.nbr_table = &nbr_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Neighbor table lookup", rv);
    }
    bcm_l3_egress_t_init(&egr_obj);
    egr_obj.flags = BCM_L3_IPMC;
    egr_obj.intf = intf;
    if (DEV_IS_HX4())
    {
        /* Helix4 does not allow to set dst_mac for IPMC entry since
         * it does not support ipmc_use_configured_dest_mac feature.
         * Set the dst mac to all zero, and need to figure out another
         * way to set the dst mac. */
        sal_memset(&egr_obj.mac_addr, 0, 6);
    }
    else
    {
        sal_memcpy(&egr_obj.mac_addr, &dst_mac, sizeof(bcm_mac_t));
    }
    if (SAI_OBJECT_TYPE_PORT == type)
    {
        egr_obj.port = port_tid;
        BCM_GPORT_LOCAL_SET(gport, port_tid);
    }
    else
    {
        _brcm_sai_indexed_data_t idata;

        egr_obj.trunk = port_tid;
        egr_obj.flags |= BCM_L3_TGID;
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &port_tid, &idata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data get", rv);
        idata.lag_info.bcast_ip = TRUE;
        idata.lag_info.nbr = *nbr;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &port_tid, &idata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data set", rv);
    }
    rv = bcm_l3_egress_create(0, 0, &egr_obj, &egr_oid);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress create", rv);
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count increase", rv);
    eobj = ALLOC_CLEAR(1, sizeof(_brcm_sai_egress_objects_t));
    if (IS_NULL(eobj))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                          sizeof(_brcm_sai_egress_objects_t));
        return SAI_STATUS_NO_MEMORY;
    }
    eobj->eoid = egr_oid;
    eobj->type = type;
    eobj->ptid = port_tid;
    if (IS_NULL(nbr_table.eobjs))
    {
        nbr_table.eobjs = eobj;
    }
    else
    {
        nbr_table.eobjs_end->next = eobj;
    }
    nbr_table.eobjs_end = eobj;
    rv = bcm_multicast_egress_object_encap_get(0, nbr_table.mcg, egr_oid, &encap);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress obj encap get", rv);
    if (SAI_OBJECT_TYPE_PORT == type)
    {
        rv = bcm_multicast_egress_add(0, nbr_table.mcg, gport, encap);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "mcast egress add", rv);
        BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                         "Added mcast member port[%d]\n", port_tid);
    }
    else
    {
        bcm_port_t port;
        int p, max, actual;
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
        rv = bcm_trunk_get(0, port_tid, &trunk_info, max, members, &actual);
        BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
        for (p=0; p<actual; p++)
        {
            rv = bcm_port_local_get(0, members[p].gport, &port);
            BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                                  members);
            if (!(members[p].flags & BCM_TRUNK_MEMBER_EGRESS_DISABLE))
            {
                BCM_GPORT_LOCAL_SET(gport, port);
                rv = bcm_multicast_egress_add(0, nbr_table.mcg, gport, encap);
                BRCM_SAI_API_CHK_FREE(SAI_API_NEIGHBOR, "mcast egress add",
                                      rv, members);
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                 "Added mcast member lag[%d].port[%d]\n",
                                 port_tid, port);
                if (DEV_IS_THX())
                {
                    /* Just add one port */
                    break;
                }
            }
            else
            {
                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                 "Skip adding disabled mcast member lag[%d].port[%d]\n",
                                 port_tid, port);
            }
        }
        CHECK_FREE(members);
    }
    nbr_table.eobj_count++;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info db table entry add", rv);
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "bcast ip [%d][%x] added %s member %d on vid %d mcast %x "
                      "eobj %d. new count = %d", nbr_table.nbr.vr_id,
                      nbr_table.nbr.ip4,
                      SAI_OBJECT_TYPE_PORT == type ? "port" : "lag",
                      port_tid, nbr_table.vid, nbr_table.mcg, egr_oid,
                      nbr_table.eobj_count);


    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

sai_status_t
_brcm_sai_nbr_bcast_member_remove(_brcm_sai_nbr_t *nbr,
                                  uint8_t type, int port_tid)
{
    bcm_if_t encap;
    sai_status_t rv;
    bcm_gport_t gport;
    bool match = FALSE;
    _brcm_sai_table_data_t data;
    _brcm_sai_nbr_table_info_t nbr_table;
    _brcm_sai_egress_objects_t *ptr, *prev;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    nbr_table.nbr = *nbr;
    data.nbr_table = &nbr_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Neighbor table lookup", rv);
    }
    if (0 == nbr_table.eobj_count)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                          "Zero associated egress objs found for this nbr [%d][%x].",
                          nbr_table.nbr.vr_id, nbr_table.nbr.ip4);
        return SAI_STATUS_FAILURE;
    }
    ptr = nbr_table.eobjs;
    if (1 == nbr_table.eobj_count)
    {
        if (type != ptr->type || port_tid != ptr->ptid)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                              "One and only egress obj type value (%d %d) "
                              "does not match incoming type value (%d %d) "
                              "for this nbr [%d][%x].", ptr->type, ptr->ptid,
                              type, port_tid, nbr_table.nbr.vr_id, nbr_table.nbr.ip4);
            return SAI_STATUS_FAILURE;
        }
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                          "Removing matched (%d %d) eobj (%d) for nbr [%d][%d].",
                          type, port_tid, ptr->eoid, nbr_table.nbr.vr_id,
                          nbr_table.nbr.ip4);
        nbr_table.nhs_end = nbr_table.nhs = NULL;
        match = TRUE;
        nbr_table.eobjs = NULL;
        nbr_table.eobjs_end = NULL;
    }
    else
    {
        prev = NULL;
        while (ptr)
        {
            if (type == ptr->type && port_tid == ptr->ptid)
            {
                if (ptr == nbr_table.eobjs)
                {
                    nbr_table.eobjs = ptr->next;
                }
                else
                {
                    prev->next = ptr->next;
                }
                if (IS_NULL(ptr->next))
                {
                    nbr_table.eobjs_end = prev;
                }
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                  "Removing matched (%d %d) eobj (%d) for nbr [%d][%d].",
                                  type, port_tid, ptr->eoid,  nbr_table.nbr.vr_id,
                                  nbr_table.nbr.ip4);
                match = TRUE;
                break;
            }
            prev = ptr;
            ptr = ptr->next;
        }
    }
    if (FALSE == match)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "egress obj type value (%d %d) "
                          "did not match for this nbr [%d][%x].", type, port_tid,
                          nbr_table.nbr.vr_id, nbr_table.nbr.ip4);
        return SAI_STATUS_FAILURE;
    }
    rv = bcm_multicast_egress_object_encap_get(0, nbr_table.mcg, ptr->eoid, &encap);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress obj encap get", rv);
    if (SAI_OBJECT_TYPE_PORT == type)
    {
        BCM_GPORT_LOCAL_SET(gport, port_tid);
        rv = bcm_multicast_egress_delete(0, nbr_table.mcg, gport, encap);
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "mcast egress remove", rv);
    }
    else
    {
        bcm_port_t port;
        int p, max, actual;
        bcm_trunk_member_t *members;
        bcm_trunk_info_t trunk_info;
        _brcm_sai_indexed_data_t idata;
        bcm_trunk_chip_info_t trunk_chip_info;

        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &port_tid, &idata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data get", rv);
        idata.lag_info.bcast_ip = FALSE;
        DATA_CLEAR(idata.lag_info.nbr, _brcm_sai_nbr_t);
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &port_tid, &idata);
        BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "lag rif data set", rv);
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
        rv = bcm_trunk_get(0, port_tid, &trunk_info, max, members, &actual);
        BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "trunk get", rv, members);
        if (DEV_IS_THX())
        {
            int i, act;
            bool match = FALSE;
            bcm_gport_t *port_array = NULL;
            bcm_if_t *encap_id_array = NULL;

            port_array = ALLOC(sizeof(bcm_gport_t) * nbr_table.eobj_count);
            encap_id_array = ALLOC(sizeof(bcm_gport_t) * nbr_table.eobj_count);
            rv = bcm_multicast_egress_get(0, nbr_table.mcg, nbr_table.eobj_count,
                                          port_array, encap_id_array, &act);
            BRCM_SAI_API_CHK_FREE2(SAI_API_NEIGHBOR, "mcast egress get", rv,
                                   port_array, encap_id_array);
            for (i=0; i<act; i++)
            {
                /* Find the matching lag port object.
                 * We may not find any as the lag may not have had any active members.
                 */
                if (encap_id_array[i] == encap)
                {
                    for (p=0; p<actual; p++)
                    {
                        rv = bcm_port_local_get(0, members[p].gport, &port);
                        BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                                              members);
                        BCM_GPORT_LOCAL_SET(gport, port);
                        if (((port_array[i] & 0xfffff) == (gport & 0xfffff))
                            && (!(members[p].flags & BCM_TRUNK_MEMBER_EGRESS_DISABLE)))
                        {
                            rv = bcm_multicast_egress_delete(0, nbr_table.mcg, gport, encap);
                            if (BCM_E_NONE != rv)
                            {
                                BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_ERROR,
                                                 "multicast egress delete.\n");

                                CHECK_FREE(members);
                                CHECK_FREE(port_array);
                                CHECK_FREE(encap_id_array);
                            }
                            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                             "Removed mcast member lag[%d].port[%d]\n",
                                             port_tid, port);
                            match = TRUE;
                            break;
                        }
                    }
                    if (match)
                    {
                        break;
                    }
                }
            }
            CHECK_FREE(port_array);
            CHECK_FREE(encap_id_array);
        }
        else
        {
            for (p=0; p<actual; p++)
            {
                rv = bcm_port_local_get(0, members[p].gport, &port);
                BRCM_SAI_API_CHK_FREE(SAI_API_LAG, "port local get", rv,
                                      members);
                if (!(members[p].flags & BCM_TRUNK_MEMBER_EGRESS_DISABLE))
                {
                    BCM_GPORT_LOCAL_SET(gport, port);
                    rv = bcm_multicast_egress_delete(0, nbr_table.mcg, gport, encap);
                    BRCM_SAI_API_CHK_FREE(SAI_API_NEIGHBOR, "mcast egress remove",
                                          rv, members);
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                     "Removed mcast member lag[%d].port[%d]\n",
                                     port_tid, port);
                }
                else
                {
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                     "Skip Removing disabled mcast member lag[%d].port[%d]\n",
                                     port_tid, port);
                }
            }
        }
        CHECK_FREE(members);
    }
    rv = bcm_l3_egress_destroy(0, ptr->eoid);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress object destroy", rv);
    nbr_table.eobj_count--;
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Removed egress object: %d. "
                      "new count %d", ptr->eoid, nbr_table.eobj_count);
    rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, DEC);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "global egress inuse count decrease", rv);
    FREE_CLEAR(ptr);
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &data);
    BRCM_SAI_RV_CHK(SAI_API_NEIGHBOR, "nbr info db table entry add", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

/* used only for vlan member lag member port updates */
sai_status_t
_brcm_sai_nbr_bcast_update(_brcm_sai_nbr_t *nbr, bool add, int tid, int port)
{
    int act;
    bcm_if_t encap;
    sai_status_t rv;
    bcm_gport_t gport;
    bool match = FALSE;
    _brcm_sai_table_data_t data;
    bcm_gport_t *port_array = NULL;
    bcm_if_t *encap_id_array = NULL;
    _brcm_sai_egress_objects_t *ptr;
    _brcm_sai_nbr_table_info_t nbr_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
    nbr_table.nbr = *nbr;
    data.nbr_table = &nbr_table;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "Neighbor table lookup", rv);
    }
    if (0 == nbr_table.eobj_count)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                          "Zero associated egress objs found for this nbr [%d][%x].",
                          nbr_table.nbr.vr_id, nbr_table.nbr.ip4);
        return SAI_STATUS_FAILURE;
    }
    ptr = nbr_table.eobjs;
    if (1 == nbr_table.eobj_count)
    {
        if (SAI_OBJECT_TYPE_LAG != ptr->type || tid != ptr->ptid)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                              "One and only egress obj type value (%d %d) "
                              "does not match incoming lag value (%d) "
                              "for this nbr [%d][%x].", ptr->type, ptr->ptid,
                              tid, nbr_table.nbr.vr_id, nbr_table.nbr.ip4);
            return SAI_STATUS_FAILURE;
        }
        match = TRUE;
    }
    else
    {
        while (ptr)
        {
            if (SAI_OBJECT_TYPE_LAG == ptr->type && tid == ptr->ptid)
            {
                match = TRUE;
                break;
            }
            ptr = ptr->next;
        }
    }
    if (FALSE == match)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "egress obj lag value (%d) "
                          "did not match for this nbr [%d][%x].", tid,
                          nbr_table.nbr.vr_id, nbr_table.nbr.ip4);
        return SAI_STATUS_FAILURE;
    }
    BCM_GPORT_LOCAL_SET(gport, port);
    rv = bcm_multicast_egress_object_encap_get(0, nbr_table.mcg, ptr->eoid, &encap);
    BRCM_SAI_API_CHK(SAI_API_NEIGHBOR, "egress obj encap get", rv);
    if (DEV_IS_THX())
    {
        port_array = ALLOC(sizeof(bcm_gport_t) * nbr_table.eobj_count);
        encap_id_array = ALLOC(sizeof(bcm_gport_t) * nbr_table.eobj_count);
        rv = bcm_multicast_egress_get(0, nbr_table.mcg, nbr_table.eobj_count,
                                      port_array, encap_id_array, &act);
        BRCM_SAI_API_CHK_FREE2(SAI_API_NEIGHBOR, "mcast egress get", rv,
                               port_array, encap_id_array);
#if 0
        /* May be used in the future for some error checks */
        if (act != nbr_table.eobj_count)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                              "Entry count mismatch: expected %d, found %d\n",
                              nbr_table.eobj_count, act);
            CHECK_FREE(port_array);
            CHECK_FREE(encap_id_array);
            return SAI_STATUS_FAILURE;
        }
#endif
    }
    if (add)
    {
        match = FALSE;
        if (DEV_IS_THX())
        {
            int i;

            for (i=0; i<act; i++)
            {
                /* check if it already has a port member for the lag eobj */
                if (encap_id_array[i] == encap)
                {
                    match = TRUE;
                    break;
                }
            }
        }
        if (!DEV_IS_THX() || (DEV_IS_THX() && FALSE == match))
        {
            rv = bcm_multicast_egress_add(0, nbr_table.mcg, gport, encap);
            BRCM_SAI_API_CHK_FREE2(SAI_API_NEIGHBOR, "mcast egress add", rv,
                                   port_array, encap_id_array);
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                             "Added mcast member lag[%d].port[%d]\n",
                             tid, port);
        }
        if (DEV_IS_THX())
        {
            CHECK_FREE(port_array);
            CHECK_FREE(encap_id_array);
        }
    }
    else /* del */
    {
        match = FALSE;
        if (DEV_IS_THX())
        {
            int i;

            for (i=0; i<act; i++)
            {
                /* check if it has this one lag port member */
                if ((encap_id_array[i] == encap) &&
                    (port_array[i] & 0xfffff) == (gport & 0xfffff))
                {
                    match = TRUE;
                    break;
                }
            }
        }
        if (!DEV_IS_THX() || (DEV_IS_THX() && match))
        {
            rv = bcm_multicast_egress_delete(0, nbr_table.mcg, gport, encap);
            BRCM_SAI_API_CHK_FREE2(SAI_API_NEIGHBOR, "mcast egress remove", rv,
                                   port_array, encap_id_array);
            BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                             "Removed mcast member lag[%d].port[%d]\n",
                             tid, port);
        }
        if (DEV_IS_THX())
        {
            CHECK_FREE(port_array);
            CHECK_FREE(encap_id_array);
        }
        if (DEV_IS_THX() && match) /* need to add another lag member if available */
        {
            bcm_port_t _port;
            int p, max, actual;
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
                if ((_port != port) && /* current port still part of trunk */
                    (!(members[p].flags & BCM_TRUNK_MEMBER_EGRESS_DISABLE)))
                {
                    BCM_GPORT_LOCAL_SET(gport, _port);
                    rv = bcm_multicast_egress_add(0, nbr_table.mcg, gport, encap);
                    BRCM_SAI_API_CHK_FREE(SAI_API_NEIGHBOR, "mcast egress add",
                                          rv, members);
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                     "Added mcast member lag[%d].port[%d]\n",
                                     tid, _port);
                    /* Just add one port */
                    break;
                }
                else
                {
                    BRCM_SAI_LOG_LAG(SAI_LOG_LEVEL_DEBUG,
                                     "Skip adding same or disabled mcast member lag[%d].port[%d]\n",
                                     tid, _port);
                }
            }
            CHECK_FREE(members);
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

STATIC sai_status_t
_brcm_sai_nbr_mac_db_add(bcm_mac_t mac, bcm_l3_intf_t* l3_intf,
                         bcm_l3_host_t *l3_host, int pa, bcm_if_t egr_if_id)
{
    sai_status_t rv;
    //bcm_l3_egress_t l3_eg;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nbr_info_t *nbr_inf, *ptr;
    bool tchange = FALSE, match = FALSE;
    _brcm_sai_mac_nbr_info_table_t mac_nbr;
    bcm_vlan_t vid = l3_intf->l3a_vid;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    DATA_CLEAR(mac_nbr, _brcm_sai_mac_nbr_info_table_t);
    /* Add entry to DB */
    sal_memcpy(mac_nbr.mac_vid.mac, mac, sizeof(sai_mac_t));
    mac_nbr.mac_vid.vid = vid;
    tdata.mac_nbr = &mac_nbr;

    sem_wait(&mac_nbr_mutex);
    /* Lookup mac-vid to see if the table entry exists.
     * If table entry exists then search in the list
     *     If found in the list then replace
     *     Else add to the list
     * Else add new entry list to the table node */
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_MAC_NBR, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "error in mac nbr table lookup.\n");
        sem_post(&mac_nbr_mutex);
        return rv;
    }
    nbr_inf = ALLOC_CLEAR(1, sizeof(_brcm_sai_nbr_info_t));
    if (IS_NULL(nbr_inf))
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                          sizeof(_brcm_sai_nbr_info_t));
        sem_post(&mac_nbr_mutex);
        return SAI_STATUS_NO_MEMORY;
    }
    nbr_inf->nbr.vr_id = l3_host->l3a_vrf;
    if (l3_host->l3a_flags & BCM_L3_IP6)
    {
        nbr_inf->nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(&nbr_inf->nbr.ip6, l3_host->l3a_ip6_addr, sizeof(sai_ip6_t));
    }
    else
    {
        nbr_inf->nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        nbr_inf->nbr.ip4 = l3_host->l3a_ip_addr;
    }
    nbr_inf->nbr.rif_id = nbr_inf->l3a_intf = l3_intf->l3a_intf_id;
    nbr_inf->pa = pa;
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Add/update NBR with pa: %d, intf: %d\n",
                      nbr_inf->pa, nbr_inf->l3a_intf);

    if (0 == mac_nbr.nbrs_count) /* Add */
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Add new MAC NBR table node.\n");
        mac_nbr.nbrs_end = nbr_inf;
        mac_nbr.nbrs = nbr_inf;
        mac_nbr.nbrs_count++;
        tchange = TRUE;
    }
    else /* Update or append */
    {
        ldata.nbrs_list = ptr = mac_nbr.nbrs;
        do
        {
            ptr = ldata.nbrs_list;
            if (MATCH == memcmp(&ptr->nbr, &nbr_inf->nbr, sizeof(_brcm_sai_nbr_t)))
            {
                ptr->l3a_intf = nbr_inf->l3a_intf;
                ptr->pa = nbr_inf->pa;
                match = TRUE;
                break;
            }
        } while (SAI_STATUS_SUCCESS ==
                 (rv = _brcm_sai_list_traverse(_BRCM_SAI_LIST_MAC_NBRS, &ldata, &ldata)));
        if (FALSE == match)
        {
            mac_nbr.nbrs_end->next = nbr_inf;
            mac_nbr.nbrs_end = nbr_inf;
            mac_nbr.nbrs_count++;
            tchange = TRUE;
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Add new NBR to MAC NBR node.\n");
        }
    }
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "Adding/updating nbr entry to mac nbr list with count = %d\n",
                      mac_nbr.nbrs_count);
    if (tchange)
    {
        /* Add/Update table node */
        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_MAC_NBR, &tdata);
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in mac nbr table entry add.");
            sem_post(&mac_nbr_mutex);
            return rv;
        }
    }
    sem_post(&mac_nbr_mutex);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

STATIC sai_status_t
_brcm_sai_nbr_mac_db_delete(bcm_mac_t mac, bcm_l3_intf_t* l3_intf,
                            bcm_l3_host_t *l3_host)
{
    sai_status_t rv;
    bool match = FALSE;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nbr_info_t *ptr, *prev = NULL;
    _brcm_sai_mac_nbr_info_table_t mac_nbr;
    bcm_vlan_t vid = l3_intf->l3a_vid;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    DATA_CLEAR(mac_nbr, _brcm_sai_mac_nbr_info_table_t);
    sal_memcpy(mac_nbr.mac_vid.mac, mac, sizeof(sai_mac_t));
    mac_nbr.mac_vid.vid = vid;
    tdata.mac_nbr = &mac_nbr;

    sem_wait(&mac_nbr_mutex);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_MAC_NBR, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "error in MAC NBR table lookup.\n");
        sem_post(&mac_nbr_mutex);
        return rv;
    }
    if (0 == mac_nbr.nbrs_count)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "MAC NBR table node with 0 entries found !!\n");
        sem_post(&mac_nbr_mutex);
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "Removing NBR entry from MAC NBR list with count = %d\n",
                      mac_nbr.nbrs_count);
    /* Traverse NBR List, remove entry, decrement count, free memory */
    ldata.nbrs_list = ptr = mac_nbr.nbrs;
    do
    {
        ptr = ldata.nbrs_list;
        if (ptr->nbr.vr_id == l3_host->l3a_vrf &&
            ptr->nbr.rif_id == l3_intf->l3a_intf_id)
        {
            if (SAI_IP_ADDR_FAMILY_IPV4 == ptr->nbr.addr_family)
            {
                if (ptr->nbr.ip4 == l3_host->l3a_ip_addr)
                {
                    match = TRUE;
                }
            }
            else
            {
                if (MATCH == memcmp(ptr->nbr.ip6, l3_host->l3a_ip6_addr, sizeof(sai_ip6_t)))
                {
                    match = TRUE;
                }
            }
            if (match)
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Removing NBR from MAC NBR list, "
                                  "ptr = %p, ptr->next = %p\n", ptr, ptr->next);

                if (1 == mac_nbr.nbrs_count)
                {
                    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                      "nbrs = %p, nbrs_end = %p\n",
                                      mac_nbr.nbrs, mac_nbr.nbrs_end);
                    mac_nbr.nbrs = mac_nbr.nbrs_end = NULL;
                }
                else
                {
                    if (ptr == mac_nbr.nbrs)
                    {
                        mac_nbr.nbrs = ptr->next;
                    }
                    else
                    {
                        prev->next = ptr->next;
                    }
                    if (IS_NULL(ptr->next))
                    {
                        mac_nbr.nbrs_end = prev;
                        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                                          "nbrs = %p, nbrs_end = %p\n",
                                          mac_nbr.nbrs, mac_nbr.nbrs_end);
                    }
                }
                mac_nbr.nbrs_count--;
                FREE(ptr);
                ptr = NULL;
                break;
            }
        }
        prev = ptr;
    } while (SAI_STATUS_SUCCESS ==
             (rv = _brcm_sai_list_traverse(_BRCM_SAI_LIST_MAC_NBRS, &ldata, &ldata)));
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "Deleted NBR entry from MAC NBR list with count = %d\n",
                      mac_nbr.nbrs_count);
    /* If no list then remove table node too */
    if (0 == mac_nbr.nbrs_count)
    {
        /* Delete entry from DB */
        rv = _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_MAC_NBR, &tdata);
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in MAC NBR table entry delete.");
        }
    }
    else if (match)
    {
        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_MAC_NBR, &tdata);
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "error in MAC NBR table add");
        }
    }
    sem_post(&mac_nbr_mutex);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

sai_status_t
_brcm_sai_mac_nbr_lookup(bcm_mac_t mac, bcm_l3_intf_t* l3_intf, bcm_l3_host_t *l3_host,
                         _brcm_sai_nbr_info_t *nbr_info)
{
    sai_status_t rv;
    bool match = FALSE;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_nbr_info_t nbr_inf, *ptr;
    _brcm_sai_mac_nbr_info_table_t mac_nbr;
    bcm_vlan_t vid = l3_intf->l3a_vid;


    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    DATA_CLEAR(mac_nbr, _brcm_sai_mac_nbr_info_table_t);
    /* Add entry to DB */
    sal_memcpy(mac_nbr.mac_vid.mac, mac, sizeof(sai_mac_t));
    mac_nbr.mac_vid.vid = vid;
    tdata.mac_nbr = &mac_nbr;
    sem_wait(&mac_nbr_mutex);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_MAC_NBR, &tdata);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in MAC NBR table lookup.\n");
        sem_post(&mac_nbr_mutex);
        return rv;
    }
    if (0 == mac_nbr.nbrs_count)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "MAC NBR table node with 0 entries found !!\n");
        sem_post(&mac_nbr_mutex);
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                      "Looking up NBR entry from MAC NBR list with count = %d\n",
                      mac_nbr.nbrs_count);
    DATA_CLEAR(nbr_inf, _brcm_sai_nbr_info_t);
    nbr_inf.nbr.vr_id = l3_host->l3a_vrf;
    if (l3_host->l3a_flags & BCM_L3_IP6)
    {
        nbr_inf.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        sal_memcpy(&nbr_inf.nbr.ip6, l3_host->l3a_ip6_addr, sizeof(sai_ip6_t));
    }
    else
    {
        nbr_inf.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        nbr_inf.nbr.ip4 = l3_host->l3a_ip_addr;
    }
    nbr_inf.nbr.rif_id = l3_intf->l3a_intf_id;
    ldata.nbrs_list = ptr = mac_nbr.nbrs;
    do
    {
        ptr = ldata.nbrs_list;
        if (MATCH == memcmp(&ptr->nbr, &nbr_inf.nbr, sizeof(_brcm_sai_nbr_t)))
        {
            nbr_inf.l3a_intf = ptr->l3a_intf;
            nbr_inf.pa = ptr->pa;
            match = TRUE;
            break;
        }
    } while (SAI_STATUS_SUCCESS ==
                 (rv = _brcm_sai_list_traverse(_BRCM_SAI_LIST_MAC_NBRS, &ldata, &ldata)));
    sem_post(&mac_nbr_mutex);

    if (FALSE == match)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "NBR not found in MAC NBR list !\n");
        rv = SAI_STATUS_ITEM_NOT_FOUND;
    }
    else
    {
        sal_memcpy(nbr_info, &nbr_inf, sizeof(_brcm_sai_nbr_info_t));
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "NBR found in MAC NBR list !\n");
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
    return rv;
}

/* FDB event based processing */
void
_brcm_sai_nbr_mac_db_update(bool add, bcm_l2_addr_t *l2addr)
{
    int port_tid;
    sai_status_t rv;
    bool trunk = FALSE;
    bcm_l3_host_t l3_host;
    bcm_if_t _dropif;
    _brcm_sai_nbr_info_t *ptr;
    _brcm_sai_list_data_t ldata;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_mac_nbr_info_table_t mac_nbr;
    bool noroute = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_NEIGHBOR);

    DATA_CLEAR(mac_nbr, _brcm_sai_mac_nbr_info_table_t);
    /* Lookup mac-nbr db to find this mac */
    sal_memcpy(&mac_nbr.mac_vid.mac, &l2addr->mac, sizeof(sai_mac_t));
    mac_nbr.mac_vid.vid = l2addr->vid;
    tdata.mac_nbr = &mac_nbr;
    sem_wait(&mac_nbr_mutex);
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_MAC_NBR, &tdata);
    if (rv == SAI_STATUS_FAILURE)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in mac nbr table entry lookup.");
        sem_post(&mac_nbr_mutex);
        return;
    }
    if (rv == SAI_STATUS_ITEM_NOT_FOUND)
    {
        /* Note: This table and processing exists only for vlan intfs case */
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Mac entry not found - skip nbr processing.");
        sem_post(&mac_nbr_mutex);
        return;
    }
    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Process fdb nbr update for "
                      "[%d].[%02x:%02x:%02x:%02x:%02x:%02x].\n", l2addr->vid,
                      l2addr->mac[0], l2addr->mac[1], l2addr->mac[2],
                      l2addr->mac[3], l2addr->mac[4], l2addr->mac[5]);
    if (0 == mac_nbr.nbrs_count)
    {
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "mac nbr table node with 0 entries found !!\n");
        sem_post(&mac_nbr_mutex);
        return;
    }
    rv = _brcm_sai_drop_if_get(&_dropif);
    if (rv != SAI_STATUS_SUCCESS)
    {
        sem_post(&mac_nbr_mutex);
        return;
    }
    if (l2addr->flags & BCM_L2_TRUNK_MEMBER)
    {
        trunk = TRUE;
        port_tid = l2addr->tgid;
    }
    else
    {
        port_tid = l2addr->port;
    }
    /* Traverse the list in the node and process all nbrs */
    ldata.nbrs_list = ptr = mac_nbr.nbrs;
    do
    {
        _brcm_sai_table_data_t data;
        _brcm_sai_nbr_table_info_t nbr_table;

        ptr = ldata.nbrs_list;
        bcm_l3_host_t_init(&l3_host);
        if (SAI_IP_ADDR_FAMILY_IPV6 == ptr->nbr.addr_family)
        {
            sal_memcpy(l3_host.l3a_ip6_addr, ptr->nbr.ip6,
                       sizeof(l3_host.l3a_ip6_addr));
            l3_host.l3a_flags |= BCM_L3_IP6;
        }
        else
        {
            l3_host.l3a_ip_addr = ptr->nbr.ip4;
        }
        l3_host.l3a_vrf = ptr->nbr.vr_id;
        rv = bcm_l3_host_find(0, &l3_host);
        if (BCM_FAILURE(rv))
        {
            BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG,
                              "L3 host entry not found in HW.\n");
            DATA_CLEAR(nbr_table, _brcm_sai_nbr_table_info_t);
            nbr_table.nbr.vr_id = l3_host.l3a_vrf;
            if (l3_host.l3a_flags & BCM_L3_IP6)
            {
                nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
                sal_memcpy(nbr_table.nbr.ip6,
                           l3_host.l3a_ip6_addr, sizeof(sai_ip6_t));
            }
            else
            {
                nbr_table.nbr.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
                nbr_table.nbr.ip4 = l3_host.l3a_ip_addr;
            }
            nbr_table.nbr.rif_id = ptr->l3a_intf;
            data.nbr_table = &nbr_table;
            rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_NBR_INFO, &data);
            if (SAI_STATUS_SUCCESS != rv)
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR,
                                  "L3 host entry not found.\n");
                sem_post(&mac_nbr_mutex);
                return;
            }
            l3_host.l3a_flags |= nbr_table.flags;
            noroute = nbr_table.noroute;
            l3_host.l3a_intf = nbr_table.if_id;
        }
        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "Got intf from DB: %d\n", ptr->l3a_intf);
        /* If add then notify to create the NH and update dependencies - begin pkt forwarding */
        if (add)
        {
            if (SAI_PACKET_ACTION_FORWARD == ptr->pa) /* check pa from state */
            {
                bcm_l3_egress_t l3_eg;
                bcm_if_t if_id;

                if (l3_host.l3a_intf != _dropif)
                {
                    /* We must already have an active host entry. */
                    continue;
                }
                /* Create neighbor entry */
                l3_host.l3a_flags |= (_BCM_L3_HOST_AS_ROUTE | BCM_L3_REPLACE);
                bcm_l3_egress_t_init(&l3_eg);
                if (trunk)
                {
                    l3_eg.trunk = port_tid;
                    l3_eg.flags |= BCM_L3_TGID;
                }
                else
                {
                    l3_eg.port = port_tid;
                }
                l3_eg.intf = ptr->l3a_intf;
                l3_eg.vlan = mac_nbr.mac_vid.vid;
                memcpy(&l3_eg.mac_addr, &mac_nbr.mac_vid.mac, sizeof(bcm_mac_t));
                rv = bcm_l3_egress_create(0, 0, &l3_eg, &if_id);
                if (BCM_FAILURE(rv))
                {
                    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in L3 egress create: %s.",
                                      bcm_errmsg(rv));
                    sem_post(&mac_nbr_mutex);
                    return;
                }
                l3_host.l3a_intf = nbr_table.if_id = if_id;
                rv = _brcm_sai_global_data_bump(_BRCM_SAI_EGRESS_INUSE_COUNT, INC);
                if (SAI_STATUS_SUCCESS != rv)
                {
                    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in egress inuse count inc");
                    sem_post(&mac_nbr_mutex);
                    return;
                }
                if (FALSE == noroute)
                {
                    rv = bcm_l3_host_add(0, &l3_host);
                    if (BCM_FAILURE(rv))
                    {
                        BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in host add: %s (%d)",
                                          bcm_errmsg(rv), rv);
                        sem_post(&mac_nbr_mutex);
                        return;
                    }
                    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "fdb nbr -> FWD.\n");
                }
            }
            else
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "skip fdb nbr -> FWD, due to pa.\n");
            }
        }
        else /* If delete then remove NH entry and update dependencies - begin pkt dropping */
        {
            /* Update host entry -> DROP */
            l3_host.l3a_flags |= (_BCM_L3_HOST_AS_ROUTE | BCM_L3_REPLACE);
            l3_host.l3a_intf = nbr_table.if_id = _dropif;
            if (FALSE == noroute)
            {
                rv = bcm_l3_host_add(0, &l3_host);
                if (BCM_FAILURE(rv))
                {
                    BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in host add: %s (%d)",
                                      bcm_errmsg(rv), rv);
                    sem_post(&mac_nbr_mutex);
                    return;
                }
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_DEBUG, "fdb nbr -> DROP.\n");
            }
        }
        if (TRUE == noroute)
        {
            rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_NBR_INFO, &data);
            if (SAI_STATUS_SUCCESS != rv)
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "NBR Table entry add error.\n");
                sem_post(&mac_nbr_mutex);
                return;
            }
        }
        if ((TRUE == add && SAI_PACKET_ACTION_FORWARD == ptr->pa) || (FALSE == add))
        {
            rv = _brcm_sai_nbr_nh_intf_update(add, FALSE, ptr->l3a_intf, &l3_host);
            if (SAI_STATUS_ERROR(rv))
            {
                BRCM_SAI_LOG_NBOR(SAI_LOG_LEVEL_ERROR, "Error in NH intf update.");
                sem_post(&mac_nbr_mutex);
                return;
            }
        }
    } while (SAI_STATUS_SUCCESS ==
             (rv = _brcm_sai_list_traverse(_BRCM_SAI_LIST_MAC_NBRS, &ldata, &ldata)));
    sem_post(&mac_nbr_mutex);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_NEIGHBOR);
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_neighbor_api_t neighbor_apis = {
    brcm_sai_create_neighbor_entry,
    brcm_sai_remove_neighbor_entry,
    brcm_sai_set_neighbor_entry_attribute,
    brcm_sai_get_neighbor_entry_attribute,
    brcm_sai_remove_all_neighbor_entries
};
