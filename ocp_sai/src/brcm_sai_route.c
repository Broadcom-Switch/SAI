/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

#ifdef L3_PERF
#include <sys/time.h>
long long create_route_usecs, remove_route_usecs, sdk_route_add, 
    sdk_route_delete, list_add, list_delete, set_route_usecs;
#endif

/*
################################################################################
#                     Local state - non-persistent across WB                   #
################################################################################
*/

/*
################################################################################
#                                  Route functions                             #
################################################################################
*/

/*
* Routine Description:
*    Create Route
*
* Arguments:
*    [in] route_entry - route entry
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/

STATIC sai_status_t
brcm_sai_create_route_entry(_In_ const sai_route_entry_t* route_entry,
                            _In_ sai_uint32_t attr_count,
                            _In_ const sai_attribute_t *attr_list)
{
    bool ip2me = FALSE;
    sai_uint32_t vr_id;
    bcm_l3_route_t l3_rt;
    bcm_l3_host_t l3_host;
    bcm_l3_egress_t l3_egr;
    sai_object_id_t nh_obj = 0;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t data;
    bcm_if_t l3_if_id = -1, _dintf;
    _brcm_sai_route_table_t route_table;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bool trap = FALSE, drop = FALSE, copy_to_cpu = FALSE;
    bool ecmp = FALSE, add_v6_host = FALSE, v6_128 = FALSE;
    int pa = SAI_PACKET_ACTION_FORWARD, i, cid = 0, nhid = 0;
    static sai_ip6_t v6_126_mask = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC };
    static sai_ip6_t v6_128_mask = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                                     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
#ifdef L3_PERF
    struct timeval cur_time1, cur_time2, cur_time3, cur_time4; 
    gettimeofday(&cur_time1, NULL);
#endif

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(route_entry))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "NULL route entry passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    bcm_l3_route_t_init(&l3_rt);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID:
                nh_obj = BRCM_SAI_ATTR_LIST_OBJ(i);
                l3_if_id = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_if_t, i);
                if (SAI_OBJECT_TYPE_NEXT_HOP ==
                        BRCM_SAI_ATTR_LIST_OBJ_TYPE(i))
                {
                    nhid = l3_if_id;
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                                   (int *) &nhid, &data);
                    BRCM_SAI_RV_CHK(SAI_API_ROUTE, "nh info data get", rv);
                    nhid = l3_if_id;
                    l3_if_id = data.nh_info.if_id;
                }
                else if (SAI_OBJECT_TYPE_NEXT_HOP_GROUP ==
                             BRCM_SAI_ATTR_LIST_OBJ_TYPE(i))
                {
                    nhid = l3_if_id;
                    l3_rt.l3a_flags |= BCM_L3_MULTIPATH;
                    ecmp = TRUE;
                }
                else if (SAI_OBJECT_TYPE_ROUTER_INTERFACE ==
                             BRCM_SAI_ATTR_LIST_OBJ_TYPE(i))
                {
                    int nb_mis_pa;

                    _brcm_sai_rif_info_get(l3_if_id, NULL, NULL, NULL, &nb_mis_pa);
                    if (SAI_PACKET_ACTION_TRAP == nb_mis_pa)
                    {
                        trap = TRUE;
                        ip2me = TRUE;
                    }
                    else if (SAI_PACKET_ACTION_DROP == nb_mis_pa)
                    {
                        drop = TRUE;
                    }
                }
                else if ((SAI_OBJECT_TYPE_PORT == BRCM_SAI_ATTR_LIST_OBJ_TYPE(i)) &&
                         (0 == BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i)))
                {
                    trap = TRUE;
                    ip2me = TRUE;
                }
                else
                {
                   drop = TRUE;
                }
                break;
            case SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION:
                if (SAI_PACKET_ACTION_LOG == attr_list[i].value.s32)
                {
                    copy_to_cpu = TRUE;
                }
                else if (SAI_PACKET_ACTION_TRAP == attr_list[i].value.s32)
                {
                    trap = TRUE;
                }
                else if (SAI_PACKET_ACTION_DROP == attr_list[i].value.s32)
                {
                    drop = TRUE;
                }
                else if (SAI_PACKET_ACTION_FORWARD != attr_list[i].value.s32)
                {
                    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "Bad attribute passed\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                pa = attr_list[i].value.s32;
                break;
            default:
                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "Unimplemented attribute passed\n");
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                               "Error processing route attributes\n");
            return rv;
        }
    }
    rv = _brcm_sai_drop_if_get(&_dintf);
    BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                    "getting system drop intf global data", rv);
    if (-1 == l3_if_id && ((FALSE == trap) && (FALSE == drop)))
    {
        /* If we've gotten here, no attributes have been passed in.
         * Need to create the route w/ defaults of PA = FWD and
         * NULL NH which means we have to use the Global DROP
         * interface so the packets will get dropped.
         */
        l3_if_id = _dintf;
    }
    if (SAI_IP_ADDR_FAMILY_IPV4 == route_entry->destination.addr_family)
    {
        l3_rt.l3a_ip_mask = ntohl(route_entry->destination.mask.ip4);
        l3_rt.l3a_subnet = ntohl(route_entry->destination.addr.ip4 &
                                 route_entry->destination.mask.ip4);
    }
    else if (SAI_IP_ADDR_FAMILY_IPV6 ==
             route_entry->destination.addr_family)
    {
        int enable_alpm, enable_128;
        
        rv = driverPropertyCheck(0, &enable_alpm);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                        "getting alpm enable status", rv);
        rv = driverPropertyCheck(1, &enable_128);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                        "getting v6 128 enable status", rv);
        if (0 != enable_alpm && 0 == enable_128)
        {
            if (0 == memcmp(v6_126_mask, route_entry->destination.mask.ip6,
                            sizeof(sai_ip6_t)))
            {
#ifdef MSFT_V6_HOST
                add_v6_host = TRUE;
#endif
            }
            else if (0 == memcmp(v6_128_mask, route_entry->destination.mask.ip6,
                                 sizeof(sai_ip6_t)))
            {
#ifdef MSFT_V6_HOST
                add_v6_host = v6_128 = TRUE;
#endif
            }
        }
        if (add_v6_host)
        {
            bcm_l3_host_t_init(&l3_host);
            sal_memcpy(l3_host.l3a_ip6_addr, route_entry->destination.addr.ip6,
                       sizeof(l3_host.l3a_ip6_addr));
            l3_host.l3a_flags |= BCM_L3_IP6;
        }
        else
        {
            sal_memcpy(l3_rt.l3a_ip6_net, route_entry->destination.addr.ip6,
                       sizeof(l3_rt.l3a_ip6_net));
            sal_memcpy(l3_rt.l3a_ip6_mask, route_entry->destination.mask.ip6,
                       sizeof(l3_rt.l3a_ip6_mask));
            l3_rt.l3a_flags |= BCM_L3_IP6;
        }
    }
    else
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "Bad address family passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    vr_id = BRCM_SAI_GET_OBJ_VAL(sai_uint32_t, route_entry->vr_id);
    if (FALSE == _brcm_sai_vr_id_valid(vr_id))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                           "Invalid VR id passed during route create %d\n",
                           vr_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    l3_rt.l3a_vrf = vr_id;
    if (trap)
    {
        /* Use global intf */
        rv = _brcm_sai_trap_if_get(&l3_rt.l3a_intf);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                         "getting system trap intf global data", rv);
        if (ip2me)
        {
            cid = _BRCM_SAI_IP2ME_CLASS;
        }
        /* Handle ECMP + trap */
        l3_rt.l3a_flags &= ~BCM_L3_MULTIPATH;
    }
    else if (drop)
    {
        /* Use global intf */
        l3_rt.l3a_intf = _dintf;
        /* Handle ECMP + drop */
        l3_rt.l3a_flags &= ~BCM_L3_MULTIPATH;
    }
    else
    {
        l3_rt.l3a_intf = l3_if_id;

        if (TRUE == copy_to_cpu || add_v6_host)
        {
            uint32 flags = BCM_L3_REPLACE | BCM_L3_WITH_ID;
            
            rv = bcm_l3_egress_get(0, l3_if_id, &l3_egr);
            BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 egress get", rv);
            if (TRUE == copy_to_cpu)
            {
                l3_egr.flags |= BCM_L3_COPY_TO_CPU;
                rv = bcm_l3_egress_create(0, flags, &l3_egr, &l3_if_id);
                BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 egress create w/ replace", rv);
            }
        }
    }
    if (SAI_IP_ADDR_FAMILY_IPV4 == route_entry->destination.addr_family)
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG,
                           "Add v4 route vrf: %d, egr %s id: %d mask: 0x%x, subnet: 0x%x\n",
                           l3_rt.l3a_vrf,
                           !(l3_rt.l3a_flags & BCM_L3_MULTIPATH) ? "nh" : "nhg",
                           l3_rt.l3a_intf, l3_rt.l3a_ip_mask, l3_rt.l3a_subnet);
    }
    else if (add_v6_host)
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG,
                           "Add v6 route host vrf: %d, egr %s id: %d \n"
                           "    subnet: 0x%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, \n",
                           l3_rt.l3a_vrf, !(l3_rt.l3a_flags & BCM_L3_MULTIPATH) ? "nh" : "nhg",
                           l3_rt.l3a_intf, l3_host.l3a_ip6_addr[0], l3_host.l3a_ip6_addr[1], l3_host.l3a_ip6_addr[2], l3_host.l3a_ip6_addr[3], 
                           l3_host.l3a_ip6_addr[4], l3_host.l3a_ip6_addr[5], l3_host.l3a_ip6_addr[6], l3_host.l3a_ip6_addr[7], 
                           l3_host.l3a_ip6_addr[8], l3_host.l3a_ip6_addr[9], l3_host.l3a_ip6_addr[10], l3_host.l3a_ip6_addr[11], 
                           l3_host.l3a_ip6_addr[12], l3_host.l3a_ip6_addr[13], l3_host.l3a_ip6_addr[14], l3_host.l3a_ip6_addr[15]);
    }
    else
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG,
                           "Add v6 route vrf: %d, egr %s id: %d \n"
                           "      mask: 0x%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, \n"
                           "    subnet: 0x%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                           l3_rt.l3a_vrf, !(l3_rt.l3a_flags & BCM_L3_MULTIPATH) ? "nh" : "nhg",
                           l3_rt.l3a_intf, l3_rt.l3a_ip6_mask[0], l3_rt.l3a_ip6_mask[1], l3_rt.l3a_ip6_mask[2], l3_rt.l3a_ip6_mask[3], 
                           l3_rt.l3a_ip6_mask[4], l3_rt.l3a_ip6_mask[5], l3_rt.l3a_ip6_mask[6], l3_rt.l3a_ip6_mask[7], 
                           l3_rt.l3a_ip6_mask[8], l3_rt.l3a_ip6_mask[9], l3_rt.l3a_ip6_mask[10], l3_rt.l3a_ip6_mask[11], 
                           l3_rt.l3a_ip6_mask[12], l3_rt.l3a_ip6_mask[13], l3_rt.l3a_ip6_mask[14], l3_rt.l3a_ip6_mask[15], 
                           l3_rt.l3a_ip6_net[0], l3_rt.l3a_ip6_net[1], l3_rt.l3a_ip6_net[2], l3_rt.l3a_ip6_net[3], 
                           l3_rt.l3a_ip6_net[4], l3_rt.l3a_ip6_net[5], l3_rt.l3a_ip6_net[6], l3_rt.l3a_ip6_net[7], 
                           l3_rt.l3a_ip6_net[8], l3_rt.l3a_ip6_net[9], l3_rt.l3a_ip6_net[10], l3_rt.l3a_ip6_net[11], 
                           l3_rt.l3a_ip6_net[12], l3_rt.l3a_ip6_net[13], l3_rt.l3a_ip6_net[14], l3_rt.l3a_ip6_net[15]);
    }
    if (add_v6_host)
    {
        l3_host.l3a_vrf = l3_rt.l3a_vrf;
        l3_host.l3a_intf = l3_rt.l3a_intf;
        rv = _brcm_sai_l3_host_config(v6_128, cid, &l3_host, &l3_egr);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "L3 v6 route hosts add", rv);
    }
    else
    {
#ifdef L3_PERF
        gettimeofday(&cur_time3, NULL);
#endif
        rv = _brcm_sai_l3_route_config(TRUE, cid, &l3_rt);
#ifdef L3_PERF
        gettimeofday(&cur_time4, NULL);
        sdk_route_add += (cur_time4.tv_usec >= cur_time3.tv_usec) ?
            (cur_time4.tv_usec - cur_time3.tv_usec) :
            (1000000 - cur_time3.tv_usec) + cur_time4.tv_usec;
#endif
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "L3 route add", rv);
    }
    if (!add_v6_host)
    {
        _brcm_sai_route_list_t *route_node;

        route_node = ALLOC_CLEAR(1, sizeof(_brcm_sai_route_list_t));
        if (IS_NULL(route_node))
        {
            BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "Error with alloc %ld\n",
                               sizeof(_brcm_sai_route_list_t));
            return SAI_STATUS_NO_MEMORY;
        }
        route_node->route.vr_id = l3_rt.l3a_vrf;
        if (l3_rt.l3a_flags & BCM_L3_IP6)
        {
            sal_memcpy(route_node->route.ip6, l3_rt.l3a_ip6_net, sizeof(sai_ip6_t));
            sal_memcpy(route_node->route.ip6m, l3_rt.l3a_ip6_mask, sizeof(sai_ip6_t));
            route_node->route.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        }
        else
        {
            route_node->route.ip4 = l3_rt.l3a_subnet;
            route_node->route.ip4m = l3_rt.l3a_ip_mask;
            route_node->route.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        }
        route_node->state = _ROUTE_STATE_FORWARD;
        if (ecmp)
        {
            route_node->multipath = TRUE;
        }
        if (trap)
        {
            route_node->state = _ROUTE_STATE_TRAP;
        }
        else if (drop)
        {
            route_node->state = _ROUTE_STATE_DROP;
        }
        if ((0 != nhid) && (FALSE == route_node->multipath) &&
            (_ROUTE_STATE_FORWARD == route_node->state))
        {
#ifdef L3_PERF
            gettimeofday(&cur_time3, NULL);
#endif
            route_node->valid = TRUE;
            rv = _brcm_sai_nh_table_route_list_route_add(nhid, route_node);
            BRCM_SAI_RV_CHK(SAI_API_ROUTE, "adding route node to route list", rv);
#ifdef L3_PERF
            gettimeofday(&cur_time4, NULL);
            list_add += (cur_time4.tv_usec >= cur_time3.tv_usec) ?
                (cur_time4.tv_usec - cur_time3.tv_usec) :
                (1000000 - cur_time3.tv_usec) + cur_time4.tv_usec;
#endif
        }

        /* Add Route to table */
        DATA_CLEAR(route_table, _brcm_sai_route_table_t);
        sal_memcpy(&route_table.route, &route_node->route, sizeof(_brcm_sai_route_info_t));
        route_table.nhid = nhid;
        route_table.nh_obj = nh_obj;
        route_table.pa = pa;
        route_table.ptr = route_node;
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG,
                           "Allocing route_table.ptr %p\n",
                           route_table.ptr);
        tdata.route_table = &route_table;
        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ROUTE, &tdata);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry add", rv);

        /* Increment count */
        if (SAI_STATUS_SUCCESS != 
            _brcm_sai_global_data_bump(_BRCM_SAI_ROUTE_NH_COUNT, INC))
        {
            BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                               "Error incrementing route nh count global data.\n");
            return SAI_STATUS_FAILURE;
        }
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTE);
#ifdef L3_PERF
    gettimeofday(&cur_time2, NULL);
    create_route_usecs += (cur_time2.tv_usec >= cur_time1.tv_usec) ?
        (cur_time2.tv_usec - cur_time1.tv_usec) :
        (1000000 - cur_time1.tv_usec) + cur_time2.tv_usec;
#endif
    return rv;
}

/*
* Routine Description:
*    Remove Route
*
* Arguments:
*    [in] route_entry - route entry
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_route_entry(_In_ const sai_route_entry_t* route_entry)
{
    sai_status_t rv;
    sai_uint32_t vr_id;
    bool del_v6_host = FALSE;
    bcm_l3_route_t l3_rt;
    _brcm_sai_list_key_t list_key;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_route_table_t route_table;
#ifdef L3_PERF
    struct timeval cur_time1, cur_time2, cur_time3, cur_time4; 
    gettimeofday(&cur_time1, NULL);
#endif

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(route_entry))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "NULL route passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    bcm_l3_route_t_init(&l3_rt);
    sal_memset(&list_key.route, 0, sizeof(_brcm_sai_route_info_t));
    DATA_CLEAR(route_table, _brcm_sai_route_table_t);
    tdata.route_table = &route_table;
    vr_id = BRCM_SAI_GET_OBJ_VAL(sai_uint32_t, route_entry->vr_id);
    list_key.route.vr_id = l3_rt.l3a_vrf = vr_id;
    route_table.route.vr_id = l3_rt.l3a_vrf = vr_id;
    if (SAI_IP_ADDR_FAMILY_IPV4 == route_entry->destination.addr_family)
    {
        l3_rt.l3a_ip_mask = ntohl(route_entry->destination.mask.ip4);
        l3_rt.l3a_subnet = ntohl(route_entry->destination.addr.ip4 &
                                 route_entry->destination.mask.ip4);
        route_table.route.ip4 = l3_rt.l3a_subnet;
        route_table.route.ip4m = l3_rt.l3a_ip_mask;
        route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    }
    else if (SAI_IP_ADDR_FAMILY_IPV6 ==
             route_entry->destination.addr_family)
    {
        uint8 v6_15;
        bool v6_128 = FALSE;
        bcm_l3_host_t l3_host;
        int enable_alpm, enable_128;
        static sai_ip6_t v6_126_mask = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                                         0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC };
        static sai_ip6_t v6_128_mask = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
                                         0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
            
        rv = driverPropertyCheck(0, &enable_alpm);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                        "getting alpm enable status", rv);
        rv = driverPropertyCheck(1, &enable_128);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                        "getting v6 128 enable status", rv);
        if (0 != enable_alpm && 0 == enable_128)
        {
            if (0 == memcmp(v6_126_mask, route_entry->destination.mask.ip6,
                            sizeof(sai_ip6_t)))
            {
#ifdef MSFT_V6_HOST
                del_v6_host = TRUE;
#endif
            }
            else if (0 == memcmp(v6_128_mask, route_entry->destination.mask.ip6,
                                 sizeof(sai_ip6_t)))
            {
#ifdef MSFT_V6_HOST
                del_v6_host = v6_128 = TRUE;
#endif
            }
        }
        if (del_v6_host)
        {
            int i, count = v6_128 ? 0 : 3;
            
            bcm_l3_host_t_init(&l3_host);
            l3_host.l3a_vrf = l3_rt.l3a_vrf;
            sal_memcpy(l3_host.l3a_ip6_addr, route_entry->destination.addr.ip6,
                       sizeof(l3_host.l3a_ip6_addr));
            v6_15 = l3_host.l3a_ip6_addr[15];
            l3_host.l3a_flags |= BCM_L3_IP6;
            for (i=0; i<=count; i++)
            {
                l3_host.l3a_ip6_addr[15] = v6_15 | i;
                rv = bcm_l3_host_delete(0, &l3_host);
                BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 v6 host delete", rv);
            }
        }
        else
        {
            sal_memcpy(l3_rt.l3a_ip6_net, route_entry->destination.addr.ip6,
                       sizeof(l3_rt.l3a_ip6_net));
            sal_memcpy(l3_rt.l3a_ip6_mask, route_entry->destination.mask.ip6,
                       sizeof(l3_rt.l3a_ip6_mask));
            l3_rt.l3a_flags |= BCM_L3_IP6;
            sal_memcpy(route_table.route.ip6, l3_rt.l3a_ip6_net, sizeof(sai_ip6_t));
            sal_memcpy(route_table.route.ip6m, l3_rt.l3a_ip6_mask, sizeof(sai_ip6_t));
            route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        }
    }
    if (!del_v6_host)
    {
#ifdef L3_PERF
        gettimeofday(&cur_time3, NULL);
#endif
        rv = bcm_l3_route_delete(0, &l3_rt);
#ifdef L3_PERF
        gettimeofday(&cur_time4, NULL);
        sdk_route_delete += (cur_time4.tv_usec >= cur_time3.tv_usec) ?
            (cur_time4.tv_usec - cur_time3.tv_usec) :
            (1000000 - cur_time3.tv_usec) + cur_time4.tv_usec;
#endif
        BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 route delete", rv);
        /* Remove Route from table */
#ifdef L3_PERF
        gettimeofday(&cur_time3, NULL);
#endif
        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ROUTE, &tdata);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry lookup.", rv);
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "removing route 0x%08x with nhid = %d\n", 
                           route_table.route.ip4, route_table.nhid);
        if ((0 != route_table.nhid) && (FALSE == route_table.ptr->multipath))
        {
            _brcm_sai_nh_table_route_list_lock(route_table.nhid);
            route_table.ptr->valid = FALSE;
            route_table.ptr->discard = FALSE;
            /* Remove route node from route list. */
            rv = _brcm_sai_nh_table_route_list_route_del(route_table.nhid, route_table.ptr);
            if (SAI_STATUS_ERROR(rv))
            {
                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "removing route node from route list");
                _brcm_sai_nh_table_route_list_unlock(route_table.nhid);
                return rv;
            }
            _brcm_sai_nh_table_route_list_unlock(route_table.nhid);
        }

        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG,
                           "Freeing route_table.ptr %p and deleting table\n",
                           route_table.ptr);

        memset(route_table.ptr, 0, sizeof(_brcm_sai_route_list_t));
        CHECK_FREE_CLEAR(route_table.ptr);

        rv = _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_ROUTE, &tdata);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry delete.", rv);
#ifdef L3_PERF
        gettimeofday(&cur_time4, NULL);
        list_delete += (cur_time4.tv_usec >= cur_time3.tv_usec) ?
            (cur_time4.tv_usec - cur_time3.tv_usec) :
            (1000000 - cur_time3.tv_usec) + cur_time4.tv_usec;
#endif
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTE);
#ifdef L3_PERF
    gettimeofday(&cur_time2, NULL);
    remove_route_usecs += (cur_time2.tv_usec >= cur_time1.tv_usec) ?
        (cur_time2.tv_usec - cur_time1.tv_usec) :
        (1000000 - cur_time1.tv_usec) + cur_time2.tv_usec;
#endif
    return rv;
}

/*
* Routine Description:
*    Set route attribute value
*
* Arguments:
*    [in] route_entry - route entry
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_route_entry_attribute(_In_ const sai_route_entry_t
                                   *route_entry,
                                   _In_ const sai_attribute_t *attr)
{
    sai_uint32_t vr_id;
    bcm_l3_route_t l3_rt;
    sai_object_id_t nh_obj;
    int pa, _tintf, _dintf;
    bcm_if_t l3_if_id = -1;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_route_list_t *route_node;
    _brcm_sai_route_table_t route_table;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bool ip2me = FALSE, pa_update = FALSE;
    int cid = 0, nhid = 0, state = -1, _nhid;
    bool update_hw = TRUE, lock_taken = FALSE;
    bool trap = FALSE, drop = FALSE, ecmp = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(route_entry) || IS_NULL(attr))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    bcm_l3_route_t_init(&l3_rt);
    switch (attr->id)
    {
        case SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID:
            nh_obj = BRCM_SAI_ATTR_PTR_OBJ();
            l3_if_id = BRCM_SAI_ATTR_PTR_OBJ_VAL(bcm_if_t);
            /* FIXME: pending handling of update to tunnel */
            if (SAI_OBJECT_TYPE_NEXT_HOP ==
                    BRCM_SAI_ATTR_PTR_OBJ_TYPE())
            {
                nhid = l3_if_id;
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                               (int *) &nhid, &data);
                BRCM_SAI_RV_CHK(SAI_API_ROUTE, "nh info data get", rv);
                nhid = l3_if_id;
                l3_if_id = data.nh_info.if_id;
            }
            else if (SAI_OBJECT_TYPE_NEXT_HOP_GROUP ==
                    BRCM_SAI_ATTR_PTR_OBJ_TYPE())
            {
                nhid = l3_if_id;
                l3_rt.l3a_flags |= BCM_L3_MULTIPATH;
                ecmp = TRUE;
            }
            else if (SAI_OBJECT_TYPE_ROUTER_INTERFACE ==
                         BRCM_SAI_ATTR_PTR_OBJ_TYPE())
            {
                int nb_mis_pa;

                _brcm_sai_rif_info_get(l3_if_id, NULL, NULL, NULL, &nb_mis_pa);
                if (SAI_PACKET_ACTION_TRAP == nb_mis_pa)
                {
                    trap = TRUE;
                }
                else if (SAI_PACKET_ACTION_DROP == nb_mis_pa)
                {
                    drop = TRUE;
                }
            }
            else if ((SAI_OBJECT_TYPE_PORT == BRCM_SAI_ATTR_PTR_OBJ_TYPE()) &&
                     (0 == BRCM_SAI_ATTR_PTR_OBJ_VAL(int)))
            {
                trap = TRUE;
                ip2me = TRUE;
            }
            else
            {
                drop = TRUE;
            }
            break;
        case SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION:
            pa_update = TRUE;
            pa = attr->value.s32;
            if (SAI_PACKET_ACTION_LOG == attr->value.s32)
            {
                l3_rt.l3a_flags |= BCM_L3_COPY_TO_CPU;
                break;
            }
            else if (SAI_PACKET_ACTION_TRAP == attr->value.s32)
            {
                trap = TRUE;
                break;
            }
            else if (SAI_PACKET_ACTION_DROP == attr->value.s32)
            {
                drop = TRUE;
                break;
            }
            else if (SAI_PACKET_ACTION_FORWARD != attr->value.s32)
            {
                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "Bad attribute passed\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        default:
            BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "Un-supported attribute %d passed\n",
                               attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
            break;
    }
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                           "Error processing route attributes\n");
        return rv;
    }
    if (!pa_update && -1 == l3_if_id && FALSE == trap && FALSE == drop)
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "Missing routing info.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    vr_id = BRCM_SAI_GET_OBJ_VAL(sai_uint32_t, route_entry->vr_id);
    if (FALSE == _brcm_sai_vr_id_valid(vr_id))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                           "Invalid VR id passed during route create %d\n",
                           vr_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (SAI_IP_ADDR_FAMILY_IPV4 == route_entry->destination.addr_family)
    {
        l3_rt.l3a_ip_mask = ntohl(route_entry->destination.mask.ip4);
        l3_rt.l3a_subnet = ntohl(route_entry->destination.addr.ip4 &
                                 route_entry->destination.mask.ip4);
    }
    else if (SAI_IP_ADDR_FAMILY_IPV6 ==
             route_entry->destination.addr_family)
    {
        sal_memcpy(l3_rt.l3a_ip6_net, route_entry->destination.addr.ip6,
                   sizeof(l3_rt.l3a_ip6_net));
        sal_memcpy(l3_rt.l3a_ip6_mask, route_entry->destination.mask.ip6,
                   sizeof(l3_rt.l3a_ip6_mask));
        l3_rt.l3a_flags |= BCM_L3_IP6;
    }
    else
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    l3_rt.l3a_vrf = vr_id;
    rv = _brcm_sai_trap_if_get(&_tintf);
    BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                    "getting system trap intf global data", rv);
    rv = _brcm_sai_drop_if_get(&_dintf);
    BRCM_SAI_RV_CHK(SAI_API_ROUTE, 
                    "getting system drop intf global data", rv);
    if (pa_update)
    {
        bcm_l3_route_t _l3_rt;
        
        /* Get current route/intf info */
        sal_memcpy(&_l3_rt, &l3_rt, sizeof(bcm_l3_route_t));
        rv = bcm_l3_route_get(0, &_l3_rt);
        BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 route get", rv);
            
        /* Get old entry */
        DATA_CLEAR(route_table, _brcm_sai_route_table_t);
        tdata.route_table = &route_table;
        route_table.route.vr_id = l3_rt.l3a_vrf;
        if (SAI_IP_ADDR_FAMILY_IPV4 == route_entry->destination.addr_family)
        {
            route_table.route.ip4 = l3_rt.l3a_subnet;
            route_table.route.ip4m = l3_rt.l3a_ip_mask;
            route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
        }
        else
        {
            sal_memcpy(route_table.route.ip6, l3_rt.l3a_ip6_net, sizeof(sai_ip6_t));
            sal_memcpy(route_table.route.ip6m, l3_rt.l3a_ip6_mask, sizeof(sai_ip6_t));
            route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
        }
        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ROUTE, &tdata);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry lookup", rv);
        route_node = tdata.route_table->ptr;
        if (_l3_rt.l3a_flags & BCM_L3_MULTIPATH || route_node->multipath)
        {
            if ((FALSE == trap) && (FALSE == drop))
            {
                l3_rt.l3a_flags |= BCM_L3_MULTIPATH;
            }
        }
        if ((0 != tdata.route_table->nhid) && (FALSE == route_node->multipath))
        {
            _brcm_sai_nh_table_route_list_lock(tdata.route_table->nhid);
        }
        if (drop)
        {
            l3_rt.l3a_intf = _dintf;
            if ((0 != tdata.route_table->nhid) && (FALSE == route_node->multipath) &&
                (TRUE == route_node->valid))
            {
                /* Remove node from FORWARD list */
                route_node->valid = FALSE;
                route_node->discard = FALSE;
                _brcm_sai_nh_table_route_list_route_del(tdata.route_table->nhid, route_node);
                if (SAI_STATUS_ERROR(rv))
                {
                    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "removing route node from route list");
                    _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                    return rv;
                }
            }
            route_node->state = _ROUTE_STATE_DROP;
        }
        else if (trap)
        {
            l3_rt.l3a_intf = _tintf;
            route_node->state = _ROUTE_STATE_TRAP;
        }
        else
        {
            int _nhid = tdata.route_table->nhid;

            if (FALSE == route_node->multipath)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NH_INFO,
                                               (int *) &_nhid, &data);
                if (SAI_STATUS_ERROR(rv))
                {
                    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "nh info data get");
                    _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                    return rv;
                }
                _nhid = data.nh_info.if_id;
            }
            if (0 == _nhid)
            {
                l3_rt.l3a_intf = _nhid = _dintf;
            }
            /* If going from trap to forward then we can use the NH value from 
             * the soft state if its valid */
            if ((_nhid != _tintf && _nhid != _dintf) && 
                (_ROUTE_STATE_TRAP == route_node->state))
            {
                if (FALSE == route_node->multipath)
                {
                    _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                }
                l3_rt.l3a_intf = _nhid;
            }
            /* If going from drop to forward then use the NH value from soft state
             * if its valid */
            else if ((_nhid != _tintf && _nhid != _dintf) && 
                     (_ROUTE_STATE_DROP == route_node->state))
            {
                l3_rt.l3a_intf = _nhid;
            }
            /* If going from drop to forward but the NH is drop interface
             * then assign NH */
            else if ((_nhid == _dintf) && 
                     (_ROUTE_STATE_DROP == route_node->state))
            {
                l3_rt.l3a_intf = _nhid;
            }
            /* If going from trap to forward but the NH is drop interface
             * then assign NH */
            else if ((_nhid == _dintf) && 
                     (_ROUTE_STATE_TRAP == route_node->state))
            {
                l3_rt.l3a_intf = _nhid;
            }
            /* If going from forward to forward state then also use NH from 
             * route entry */
            else if (_ROUTE_STATE_FORWARD == route_node->state)
            {
                l3_rt.l3a_intf = _l3_rt.l3a_intf;
            }
            route_node->state = _ROUTE_STATE_FORWARD;
            if ((0 != tdata.route_table->nhid) && (FALSE == route_node->multipath) &&
                (FALSE == route_node->valid))
            {
                /* Add node to FORWARD list */
                route_node->valid = TRUE;
                _brcm_sai_nh_table_route_list_route_add(tdata.route_table->nhid, route_node);
                if (SAI_STATUS_ERROR(rv))
                {
                    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "nh route list node add");
                    _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                    return rv;
                }
            }
        }
        if ((0 != tdata.route_table->nhid) && (FALSE == route_node->multipath))
        {
            _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
        }
        state = route_node->state;
        route_table.pa = pa;
        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ROUTE, &tdata);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry add", rv);
    }
    else
    {
        if (trap)
        {
            l3_rt.l3a_intf = _tintf;
            if (ip2me)
            {
                cid = _BRCM_SAI_IP2ME_CLASS;
            }
            state = _ROUTE_STATE_TRAP;
        }
        else
        {
            l3_rt.l3a_intf = l3_if_id;
            /* Get old entry */
            DATA_CLEAR(route_table, _brcm_sai_route_table_t);
            tdata.route_table = &route_table;
            route_table.route.vr_id = l3_rt.l3a_vrf;
            if (SAI_IP_ADDR_FAMILY_IPV4 == route_entry->destination.addr_family)
            {
                route_table.route.ip4 = l3_rt.l3a_subnet;
                route_table.route.ip4m = l3_rt.l3a_ip_mask;
                route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
            }
            else
            {
                sal_memcpy(route_table.route.ip6, l3_rt.l3a_ip6_net, sizeof(sai_ip6_t));
                sal_memcpy(route_table.route.ip6m, l3_rt.l3a_ip6_mask, sizeof(sai_ip6_t));
                route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
            }
            rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ROUTE, &tdata);
            BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry lookup", rv);
            route_node = tdata.route_table->ptr;
            if ((0 != tdata.route_table->nhid) && (FALSE == route_node->multipath))
            {
                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "taking lock for nhid %d", tdata.route_table->nhid);
                _brcm_sai_nh_table_route_list_lock(tdata.route_table->nhid);
                lock_taken = TRUE;
            }
            if (drop)
            {
                /* Current state FWD, DROP, or TRAP and setting NULL NHOP */
                _nhid = tdata.route_table->nhid;
                l3_rt.l3a_intf = _dintf;
                if ((0 != tdata.route_table->nhid) && (FALSE == route_node->multipath) &&
                    (TRUE == route_node->valid))
                {
                    /* Remove node from FORWARD list */
                    route_node->valid = FALSE;
                    route_node->discard = FALSE;
                    if (FALSE == lock_taken)
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "deleting route from nh %d list without a lock",
                                           tdata.route_table->nhid);
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "taking lock for nhid %d", tdata.route_table->nhid);
                        _brcm_sai_nh_table_route_list_lock(tdata.route_table->nhid);
                        lock_taken = TRUE;
                    }
                    else
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "deleting route from nh %d list",
                                           tdata.route_table->nhid);
                    }
                    _brcm_sai_nh_table_route_list_route_del(tdata.route_table->nhid, 
                                                            route_node);
                    if (SAI_STATUS_ERROR(rv))
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "removing route node from route list");
                        if (TRUE == lock_taken)
                        {
                            _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                            lock_taken = FALSE;
                        }
                        return rv;
                    }
                }
                route_node->multipath = FALSE;
                route_node->valid = FALSE;
                if (TRUE == lock_taken)
                {
                   _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                   lock_taken = FALSE;
                }
                tdata.route_table->nhid = 0;
            }
            else
            {
                bool tmp_multipath = route_node->multipath;

                _nhid = tdata.route_table->nhid;
                tdata.route_table->nhid = nhid;
                if (_ROUTE_STATE_FORWARD != route_node->state)
                {
                    update_hw = FALSE;
                }
                if (ecmp)
                {
                    route_node->multipath = TRUE;
                }
                else
                {
                    route_node->multipath = FALSE;
                }
                if (_nhid == nhid)
                {
                    /* If the app is trying to change to the same NH, then nothing to do. */
                    if (TRUE == lock_taken)
                    {
                        _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                        lock_taken = FALSE;
                    }
                    return SAI_STATUS_SUCCESS;
                }
                else
                {
                    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "changing route from nh %d to nh %d, valid = %s",
                                       _nhid, tdata.route_table->nhid, route_node->valid ? "TRUE" : "FALSE");
                }
                if (route_node->multipath != tmp_multipath)
                {
                    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "changing route from %s to %s",
                                       tmp_multipath ? "ECMP" : "non-ECMP", route_node->multipath ? "ECMP" : "non-ECMP");
                    if ((TRUE == route_node->multipath) && (TRUE == route_node->valid))
                    {
                        /* Changing route to a NHG... need to remove the route
                         * from the original NH route list.
                         */
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "deleting route from nh %d list", _nhid);
                        route_node->valid = FALSE;
                        if (_nhid != 0)
                        {
                            route_node->discard = FALSE;
                            rv = _brcm_sai_nh_table_route_list_route_del(_nhid, route_node);
                            if (SAI_STATUS_ERROR(rv))
                            {
                                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "removing route node from route list");
                                if (TRUE == lock_taken)
                                {
                                _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                                lock_taken = FALSE;
                                }                                
                                return rv;
                            }
                        }
                    }
                }
                if ((FALSE == route_node->valid) && (FALSE == route_node->multipath) &&
                    (_ROUTE_STATE_FORWARD == route_node->state))
                {
                    /* Change to a non-ECMP NH and route isn't in the list yet. */
                    route_node->valid = TRUE;
                    if (_nhid != nhid)
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "changing route from nh %d to nh %d",
                                           _nhid, tdata.route_table->nhid);
                    }
                    if (FALSE == lock_taken)
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "adding route to nh %d list without a lock",
                                           tdata.route_table->nhid);
                        _nhid = tdata.route_table->nhid;
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "taking lock for nhid %d", tdata.route_table->nhid);
                        _brcm_sai_nh_table_route_list_lock(tdata.route_table->nhid);
                        lock_taken = TRUE;
                    }
                    else
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "adding route to nh %d list",
                                           tdata.route_table->nhid);
                    }
                    rv = _brcm_sai_nh_table_route_list_route_add(tdata.route_table->nhid, route_node);
                    if (SAI_STATUS_ERROR(rv))
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "nh route list node add");
                        if (TRUE == lock_taken)
                        {
                            _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                        }
                        return rv;
                    }
                }
                else if ((TRUE == route_node->valid) && (FALSE == route_node->multipath) &&
                         (_ROUTE_STATE_FORWARD == route_node->state))
                {
                    /* Change to a non-ECMP NH and route is currently in the "old" list.
                     * Need to remove from the "old" list and add to the "new" list.
                     */
                    if (_nhid != nhid)
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "changing route from nh %d to nh %d",
                                           _nhid, tdata.route_table->nhid);
                    }
                    if (FALSE == lock_taken && tmp_multipath == FALSE)
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "changing route to nh %d list without a lock",
                                           tdata.route_table->nhid);
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "error: should never get here");
                        return SAI_STATUS_FAILURE;
                    }
                    else 
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "adding route to nh %d list",
                                           tdata.route_table->nhid);
                    }
                    if (tmp_multipath == FALSE)
                    {
                        /* First, remove the route from the "old" list. */
                        route_node->valid = FALSE;
                        route_node->discard = FALSE;
                        rv = _brcm_sai_nh_table_route_list_route_del(_nhid, route_node);
                        if (SAI_STATUS_ERROR(rv))
                        {
                            BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "removing route node from route list");
                            _brcm_sai_nh_table_route_list_unlock(_nhid);
                            return rv;
                        }
                    }
                    /* Now, add the route to the "new" list. Have to take the lock for the new NH. */
                    route_node->valid = TRUE;
                    _brcm_sai_nh_table_route_list_lock(tdata.route_table->nhid);
                    rv = _brcm_sai_nh_table_route_list_route_add(tdata.route_table->nhid, route_node);
                    _brcm_sai_nh_table_route_list_unlock(tdata.route_table->nhid);
                    if (SAI_STATUS_ERROR(rv))
                    {
                        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR, "nh route list node add");
                        return rv;
                    }
                }
                else if ((FALSE == route_node->valid) &&
                         (TRUE == route_node->multipath))
                {
                    route_node->valid = TRUE;
                }
            }
            if (TRUE == lock_taken)
            {
                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "freeing lock for nhid %d", _nhid);
               _brcm_sai_nh_table_route_list_unlock(_nhid);
               lock_taken = FALSE;
            }
            route_table.nh_obj = nh_obj;
            rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ROUTE, &tdata);
            BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry add", rv);
            state = route_node->state;
        }
    }
    if (update_hw)
    {
        /* Update existing route */
        l3_rt.l3a_flags |= BCM_L3_REPLACE;

        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "Update route vrf: %d, egr %s id: %d, state: %d\n",
                           l3_rt.l3a_vrf,
                           !(l3_rt.l3a_flags & BCM_L3_MULTIPATH) ? "nh" : "nhg",
                           l3_rt.l3a_intf, state);
        rv = _brcm_sai_l3_route_config(TRUE, cid, &l3_rt);
        BRCM_SAI_RV_CHK(SAI_API_ROUTE, "L3 route add", rv);
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTE);

    return rv;
}

/*
* Routine Description:
*    Get route attribute value
*
* Arguments:
*    [in] route_entry - route entry
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_route_entry_attribute(_In_ const sai_route_entry_t*
                                   route_entry,
                                   _In_ sai_uint32_t attr_count,
                                   _Inout_ sai_attribute_t *attr_list)
{
    int i;
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_route_table_t route_table;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(route_entry);
    DATA_CLEAR(route_table, _brcm_sai_route_table_t);
    tdata.route_table = &route_table;
    route_table.route.vr_id = BRCM_SAI_GET_OBJ_VAL(int, route_entry->vr_id);
    if (SAI_IP_ADDR_FAMILY_IPV4 == route_entry->destination.addr_family)
    {
        route_table.route.ip4 = ntohl(route_entry->destination.addr.ip4 &
                                      route_entry->destination.mask.ip4);
        route_table.route.ip4m = ntohl(route_entry->destination.mask.ip4);
        route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV4;
    }
    else
    {
        sal_memcpy(route_table.route.ip6, route_entry->destination.addr.ip6,
                   sizeof(sai_ip6_t));
        sal_memcpy(route_table.route.ip6m, route_entry->destination.mask.ip6,
                   sizeof(sai_ip6_t));
        route_table.route.addr_family = SAI_IP_ADDR_FAMILY_IPV6;
    }
    
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_ROUTE, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry lookup.", rv);
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_ROUTE_ENTRY_ATTR_NEXT_HOP_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = route_table.nh_obj;
                break;
            case SAI_ROUTE_ENTRY_ATTR_PACKET_ACTION:
                attr_list[i].value.s32 = route_table.pa;
                break;
            default:
                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                                   "Unknown or unsupported route attribute %d passed\n",
                                   attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                               "Error processing route attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTE);

    return rv;
}

/*
################################################################################
#                               Internal functions                             #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_route_info()
{
    sai_status_t rv;
    _brcm_sai_data_t gdata;
    int max;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_route_table_t route_table;
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTE);
    rv = _brcm_sai_l3_config_get(6, &max);
    BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 info", rv);
    if (SAI_STATUS_SUCCESS != 
        _brcm_sai_db_table_create(_BRCM_SAI_TABLE_ROUTE, max))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_CRITICAL,
                           "Error creating route table !!\n");
        return SAI_STATUS_FAILURE;
    }
    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG, "Created %d route table entries.\n", max);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_ROUTE_NH_COUNT, &gdata))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                           "Error getting route nh count global data.\n");
        return SAI_STATUS_FAILURE;
    }
 

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_ROUTE_NH_COUNT, &gdata))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                           "Error getting route nh count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    
    /* Restore the route node */
    DATA_CLEAR(route_table, _brcm_sai_route_table_t);
    tdata.route_table = &route_table;
    do        
    {
        rv = _brcm_sai_db_table_entry_getnext(_BRCM_SAI_TABLE_ROUTE, &tdata);
        if (SAI_STATUS_SUCCESS == rv)
        {
            if ((route_table.nhid == 0) ||
                (route_table.state != _ROUTE_STATE_FORWARD) ||
                (route_table.multipath == TRUE))
            {
                /* copy over the data from route table */
                _brcm_sai_route_list_t *route_node;
                route_node = ALLOC_CLEAR(1, sizeof(_brcm_sai_route_list_t));
                route_node->valid = route_table.valid;
                route_node->multipath = route_table.multipath;
                route_node->discard = route_table.discard;
                route_node->state = route_table.state;

                /* update the ptr and add it back */
                route_table.ptr = route_node;
                BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG,
                                   "Allocing route_table.ptr %p\n",
                                   route_table.ptr);
                rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ROUTE, &tdata);
                BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry add", rv);
            }
        }
    } while (rv == SAI_STATUS_SUCCESS);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTE);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_route_info()
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_data_t gdata;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_route_table_t route_table;
     _brcm_sai_route_list_t *route_node;
    BRCM_SAI_FUNCTION_ENTER(SAI_API_ROUTE);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_ROUTE_NH_COUNT, &gdata))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                           "Error getting route nh count global data.\n");
        return SAI_STATUS_FAILURE;
    }
    
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_get(_BRCM_SAI_WARM_SHUT, &gdata))
    {
        BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_ERROR,
                           "Error getting Warmshut state.\n");
        return SAI_STATUS_FAILURE;
    }

    DATA_CLEAR(route_table, _brcm_sai_route_table_t);
    tdata.route_table = &route_table;  
    do
    {
        rv = _brcm_sai_db_table_entry_getnext(_BRCM_SAI_TABLE_ROUTE,
                                              &tdata);
        if (SAI_STATUS_SUCCESS == rv)
        {
            if ((route_table.nhid == 0) ||
                (route_table.ptr->state != _ROUTE_STATE_FORWARD) ||
                (route_table.ptr->multipath == TRUE))
            {
                route_node = route_table.ptr;

                /* only on graceful warmshut */
                if (gdata.bool_data == TRUE)
                {
                    
                    /* copy over the data from route node */
                    route_table.valid = route_node->valid;
                    route_table.multipath = route_node->multipath;
                    route_table.discard = route_node->discard;
                    route_table.state = route_node->state;
                    route_table.ptr = NULL;
                    
                    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_ROUTE,
                                                      &tdata);
                    BRCM_SAI_RV_CHK(SAI_API_ROUTE, "route DB table entry add", rv);
                }

                if (route_node) {
                    BRCM_SAI_LOG_ROUTE(SAI_LOG_LEVEL_DEBUG,
                                       "Freeing route_node %p\n",
                                       route_node);
                }

                /* No route list to free, so we free here */
                CHECK_FREE_CLEAR(route_node);
                
            }
        }
    } while (rv == SAI_STATUS_SUCCESS);
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_ROUTE);
    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_route_api_t route_apis = {
    brcm_sai_create_route_entry,
    brcm_sai_remove_route_entry,
    brcm_sai_set_route_entry_attribute,
    brcm_sai_get_route_entry_attribute
};
