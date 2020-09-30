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
#                          Local non persistent state                          #
################################################################################
*/
static int max_tunnel_rif = 0;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t
_brcm_sai_tunnel_map_entry_data_set(_brcm_sai_tunnel_map_entry_t *tunnel_map_entry);
STATIC sai_status_t
_brcm_sai_tunnel_map_entry_key_get(int type, int value,
                                   _brcm_sai_tunnel_map_entry_t *tunnel_map_entry);
STATIC sai_status_t
_brcm_sai_tunnel_net_vpn_entry_key_set(_brcm_sai_tunnel_net_vpn_entry_t *entry);
STATIC sai_status_t
_brcm_sai_tunnel_net_vpn_entry_key_get(int type, bcm_ip_t src_ip, bcm_ip_t dst_ip, uint16 udp_dp,
                                       _brcm_sai_tunnel_net_vpn_entry_t *entry);
STATIC sai_status_t
_brcm_sai_tunnel_net_vpn_entry_key_delete(_brcm_sai_tunnel_net_vpn_entry_t *entry);
/*
################################################################################
#                        VxLAN Tunnel functions                                #
################################################################################
*/

STATIC int
_brcm_sai_vxlan_create_vxlan_vpn(int unit, uint32 vni, bcm_vpn_t vpn, bcm_multicast_t mcg_id)
{
    sai_status_t rv;
    bcm_vxlan_vpn_config_t vpn_config;

    bcm_vxlan_vpn_config_t_init(&vpn_config);
    vpn_config.flags = BCM_VXLAN_VPN_ELAN | BCM_VXLAN_VPN_WITH_VPNID | BCM_VXLAN_VPN_WITH_ID;
    vpn_config.vpn = vpn;
    vpn_config.vnid = vni;
    vpn_config.broadcast_group = mcg_id;
    vpn_config.unknown_unicast_group = mcg_id;
    vpn_config.unknown_multicast_group = mcg_id;
    rv = bcm_vxlan_vpn_create(unit, &vpn_config);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan vpn create", rv);
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG,
                        "Created vpn: %d(0x%x) for vni: %d\n", vpn, vpn, vni);

    return rv;
}

STATIC int
_brcm_sai_vxlan_net_port_settings(int unit, bcm_port_t n_port)
{
    sai_status_t rv;

    /* Enable VXLAN Processing on network port */
    rv = bcm_port_control_set(unit, n_port, bcmPortControlVxlanEnable, TRUE);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_net_port_settings bcmPortControlVxlanEnable", rv);

    /* Allow tunnel based VXLAN-VNID lookup */
    rv = bcm_port_control_set(unit, n_port, bcmPortControlVxlanTunnelbasedVnId, FALSE);
                              /* Only BCM_VXLAN_PORT_MATCH_VN_ID at network port */
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_net_port_settings bcmPortControlVxlanTunnelbasedVnId", rv);

    /* Enable Default SVP on network side */
    rv = bcm_port_control_set(unit, n_port, bcmPortControlVxlanDefaultTunnelEnable, 0);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_net_port_settings bcmPortControlVxlanDefaultTunnelEnable", rv);

    return BCM_E_NONE;
}

STATIC int
_brcm_sai_vxlan_access_port_settings(int unit, bcm_port_t a_port)
{
    sai_status_t rv;

    rv = bcm_vlan_control_port_set(unit, a_port, bcmVlanTranslateIngressEnable, 1);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_access_port_settings bcmVlanTranslateIngressEnable", rv);

    /* Should disable Vxlan Processing on access port */
    rv = bcm_port_control_set(unit, a_port, bcmPortControlVxlanEnable, FALSE);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_access_port_settings bcmPortControlVxlanEnable", rv);

    /* Should disable Tunnel Based Vxlan-VnId lookup */
    rv = bcm_port_control_set(unit, a_port, bcmPortControlVxlanTunnelbasedVnId, FALSE);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_access_port_settings bcmPortControlVxlanTunnelbasedVnId", rv);

    return BCM_E_NONE;
}

STATIC int
_brcm_sai_vxlan_get_lag_members(int* trunk_members, int* count)
{
    bcm_trunk_member_t *members = NULL;
    bcm_trunk_info_t trunk_info;
    bcm_trunk_chip_info_t trunk_chip_info;
    int max_members;
    int tid;
    int tid_fp_min;
    int tid_fp_max;
    int found = 0;
    int i;
    int counter = 0;
    int rv = BCM_E_NONE;

    rv = bcm_trunk_chip_info_get(0, &trunk_chip_info);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "trunk chip info get", rv);
    max_members = trunk_chip_info.trunk_group_count;
    members = ALLOC(sizeof(bcm_trunk_member_t) * max_members);
    if (IS_NULL(members))
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_CRITICAL,
                                "Error allocating memory for lag members.\n");
        return BCM_E_MEMORY;
    }

    tid_fp_min = trunk_chip_info.trunk_id_min;
    tid_fp_max = trunk_chip_info.trunk_id_max;

    for (tid = tid_fp_min; tid < tid_fp_max; tid++)
    {
        rv = bcm_trunk_get(0, tid, &trunk_info, max_members, members, &found);
        if (rv == BCM_E_NOT_FOUND)
        {
            rv = BCM_E_NONE;
            continue;
        }
        BRCM_SAI_API_CHK_FREE(SAI_API_VLAN, "trunk get", rv, members);

        for (i = 0; i < found; i++)
        {
            if (*count == counter)
            {
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Not enough cells in the return array.\n");
                rv = BCM_E_FULL;
            }
            trunk_members[counter] = BCM_GPORT_MODPORT_PORT_GET(members[i].gport);
            counter++;
        }
    }
    FREE(members);

    *count = counter;

    return rv;
}

STATIC int
_brcm_sai_vxlan_create_egr_obj(int unit, uint32 flag, int l3_if, const bcm_mac_t nh_mac, bcm_gport_t gport,
                               int vid, bcm_if_t *egr_obj_id)
{
    bcm_l3_egress_t l3_egress;

    bcm_l3_egress_t_init(&l3_egress);
    l3_egress.flags = BCM_L3_VXLAN_ONLY | flag;
    l3_egress.intf  = l3_if;
    sal_memcpy(l3_egress.mac_addr,  nh_mac, sizeof(l3_egress.mac_addr));
    l3_egress.vlan = vid;
    l3_egress.port = gport;
    return bcm_l3_egress_create(unit, flag, &l3_egress, egr_obj_id);
}

typedef struct _brcm_sai_vxlan_l3_egress_search_s
{
    uint32 flags;
    int intf;
    bcm_mac_t mac_addr;
    bcm_gport_t port;
    int vlan;
    bcm_if_t egr_obj_id;
} _brcm_sai_vxlan_l3_egress_search_t;

static int
_brcm_sai_vxlan_search_egr_obj_func(int unit, int index, bcm_l3_egress_t* info, void* data)
{
    _brcm_sai_vxlan_l3_egress_search_t* l3_egress_search = (_brcm_sai_vxlan_l3_egress_search_t*)data;
/*
 * disabling the original algorithm from the customer patch, the original does not accurately match
 * multicast egress objects causing a resource leak, the alternate implemenation appears to work
 * correctly in reusing the multicast l3 egress objects
 *
 * keeping original for reference for meantime
 */
#if 0
    int mc_flag = BCM_L3_IPMC & l3_egress_search->flags ? 0x7 : 0x0;

    if(info->multicast_flags == mc_flag &&
       info->intf == l3_egress_search->intf &&
       info->port == BCM_GPORT_MODPORT_PORT_GET(l3_egress_search->port) &&
       sal_memcmp(info->mac_addr, l3_egress_search->mac_addr, sizeof(info->mac_addr)) == 0 &&
       ((!mc_flag && info->vlan == l3_egress_search->vlan) || mc_flag))
    {
        l3_egress_search->egr_obj_id = index;
    }
#else
    if(info->intf == l3_egress_search->intf &&
       info->port == BCM_GPORT_MODPORT_PORT_GET(l3_egress_search->port) &&
       sal_memcmp(info->mac_addr, l3_egress_search->mac_addr, sizeof(info->mac_addr)) == 0)
    {
        l3_egress_search->egr_obj_id = index;
    }
#endif

    return BCM_E_NONE;
}

static int
_brcm_sai_vxlan_search_egr_obj(int unit, uint32 flag, int l3_if, const bcm_mac_t nh_mac, bcm_gport_t gport,
              int vid, bcm_if_t *egr_obj_id)
{
    _brcm_sai_vxlan_l3_egress_search_t l3_egress;
    int rv = BCM_E_NONE;

    /* ToDo: It's better to store the state internally. Don't use brcm traverse function, which is tricky
       l3_egress_traverse works not as expected. it returns multicast egress objects without ipmc flag,
       and it has different vlan from what expected */

    l3_egress.flags = BCM_L3_VXLAN_ONLY | flag;
    l3_egress.intf  = l3_if;
    sal_memcpy(l3_egress.mac_addr,  nh_mac, sizeof(l3_egress.mac_addr));
    l3_egress.vlan = vid;
    l3_egress.port = gport;
    l3_egress.egr_obj_id = -1;

    rv = bcm_l3_egress_traverse(unit, _brcm_sai_vxlan_search_egr_obj_func, (void*)&l3_egress);
    if (rv != BCM_E_NONE)
    {
        return rv;
    }

    *egr_obj_id = l3_egress.egr_obj_id;

    return rv;
}

STATIC int
_brcm_sai_vxlan_create_vxlan_acc_vp(int unit, bcm_vpn_t vpn, uint32 flags, bcm_gport_t port,
                                    bcm_vxlan_port_match_t criteria,
                                    bcm_if_t egr_obj, bcm_vlan_t vid, bcm_gport_t *vp)
{
    bcm_vxlan_port_t vxlan_port;
    bcm_error_t rv = BCM_E_NONE;

    bcm_vxlan_port_t_init(&vxlan_port);
    vxlan_port.flags      = BCM_VXLAN_PORT_SERVICE_TAGGED | flags;
    vxlan_port.match_port = port;
    vxlan_port.criteria   = criteria;
    vxlan_port.egress_if  = egr_obj;
    vxlan_port.match_vlan = vid;
    rv = bcm_vxlan_port_add(unit, vpn, &vxlan_port);
    *vp = vxlan_port.vxlan_port_id;

    return rv;
}

STATIC int
_brcm_sai_vxlan_create_vxlan_net_vp(int unit, bcm_vpn_t vpn, uint32 flags, bcm_gport_t port,
                                    bcm_vxlan_port_match_t criteria, bcm_if_t egr_obj,
                                    bcm_gport_t tun_init, bcm_gport_t tun_term, bcm_gport_t *vp)
{
    bcm_vxlan_port_t vxlan_port;
    bcm_error_t rv = BCM_E_NONE;

    bcm_vxlan_port_t_init(&vxlan_port);
    vxlan_port.flags = BCM_VXLAN_PORT_NETWORK | BCM_VXLAN_PORT_EGRESS_TUNNEL |
                       BCM_VXLAN_PORT_SERVICE_TAGGED | flags;
    vxlan_port.match_port =        port;
    vxlan_port.criteria =          criteria;
    vxlan_port.egress_if =         egr_obj;
    vxlan_port.egress_tunnel_id =  tun_init;
    vxlan_port.match_tunnel_id =   tun_term;
    /* vpn_id parameter is not care for net VP */
    rv = bcm_vxlan_port_add(unit, vpn, &vxlan_port);
    *vp = vxlan_port.vxlan_port_id;
    return rv;
}

STATIC int
_brcm_sai_vxlan_tunnel_initiator_setup(bcm_ip_t lip, bcm_ip_t rip, uint16 dp, uint16 sp, int ttl, bcm_gport_t *tid)
{
    const int unit = 0;
    bcm_tunnel_initiator_t tnl_init;
    bcm_error_t rv = BCM_E_NONE;

    bcm_tunnel_initiator_t_init(&tnl_init);
    tnl_init.type  = bcmTunnelTypeVxlan;
    tnl_init.ttl = ttl;
    tnl_init.sip = lip;
    tnl_init.dip = rip;
    tnl_init.udp_dst_port = dp;
    tnl_init.udp_src_port = sp;
    rv = bcm_vxlan_tunnel_initiator_create(unit, &tnl_init);

    *tid = tnl_init.tunnel_id;

    return rv;
}

STATIC int
_brcm_sai_vxlan_tunnel_terminator_setup(bcm_ip_t rip, bcm_ip_t lip, bcm_vlan_t net_vid,
                                        int tunnel_init_id, int *term_id)
{
    const int unit = 0;
    bcm_tunnel_terminator_t tnl_term;
    bcm_error_t rv = BCM_E_NONE;

    bcm_tunnel_terminator_t_init(&tnl_term);
    tnl_term.type = bcmTunnelTypeVxlan;
    tnl_term.sip = rip;    /* For MC tunnel, Don't care */
    tnl_term.dip = lip;
    tnl_term.tunnel_id = tunnel_init_id;
    tnl_term.flags = BCM_TUNNEL_TERM_TUNNEL_WITH_ID;
    if (net_vid != (bcm_vlan_t) -1) {
        tnl_term.vlan = net_vid;  /* MC tunnel only - for Bud check */
    }
    rv = bcm_vxlan_tunnel_terminator_create(unit, &tnl_term);
    *term_id = tnl_term.tunnel_id;

    return rv;
}

STATIC int
_brcm_sai_vxlan_add_to_l2_station(int unit, const bcm_mac_t mac, bcm_vlan_t vid, int flags, int *station_id)
{
    bcm_l2_station_t l2_station;
    bcm_error_t rv = BCM_E_NONE;
    char broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    bcm_l2_station_t_init(&l2_station);
    sal_memcpy(l2_station.dst_mac, mac, sizeof(l2_station.dst_mac));
    l2_station.flags = flags;
    sal_memcpy(l2_station.dst_mac_mask, broadcast_mac, sizeof(broadcast_mac));
    l2_station.vlan_mask = 0;
    if (vid != 0)
    {
        l2_station.vlan = vid;
        l2_station.vlan_mask = 0xfff;
    }
    rv = bcm_l2_station_add(unit, station_id, &l2_station);

    return rv;
}

/*
 * control whether the implementation expects the caller to create an L3 interface
 * for the access VLAN:
 * 0 - implementation creates 'dummy' L3 interface as suggested in app note
 * 1 - implementation searches for L3 interface using access VID and switch MAC
 */
#define _BRCM_SAI_VXLAN_CALLER_CREATES_ACCESS_L3_IF 1

STATIC sai_status_t
_brcm_sai_vxlan_create_vxlan_acc_port(bcm_port_t acc_port, bcm_vpn_t vpn_id, uint16_t acc_vid, int bc_group,
                                      bcm_if_t *acc_intf_id, bcm_if_t *acc_egr_obj, bcm_gport_t *acc_vxlan_port, bcm_if_t *acc_encap_id)
{
    const int unit = 0;
    sai_status_t rv;
    bcm_l3_intf_t intf;
    bcm_mac_t acc_dummy_mac = {0x00, 0x00, 0x01, 0x00, 0x00, 0x01};
    bcm_gport_t acc_gport = BCM_GPORT_INVALID;
    bcm_mac_t local_mac_address;

    rv = _brcm_sai_get_local_mac(local_mac_address);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get local mac address", rv);

    rv = _brcm_sai_vxlan_access_port_settings(unit, acc_port);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "set control for an access port", rv);

    rv = bcm_port_gport_get(unit, acc_port, &acc_gport);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get gport for access port", rv);

    bcm_l3_intf_t_init(&intf);
    intf.l3a_vid = acc_vid;
    intf.l3a_vrf = 0;
#if _BRCM_SAI_VXLAN_CALLER_CREATES_ACCESS_L3_IF
    sal_memcpy(intf.l3a_mac_addr, local_mac_address, sizeof(intf.l3a_mac_addr));
    rv = bcm_l3_intf_find(unit, &intf);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "getting acc intf", rv);
#else
    sal_memcpy(intf.l3a_mac_addr, acc_dummy_mac, sizeof(intf.l3a_mac_addr));
    rv = bcm_l3_intf_create(unit, &intf);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "creating acc intf", rv);
#endif

    *acc_intf_id = intf.l3a_intf_id;

    rv = _brcm_sai_vxlan_create_egr_obj(unit, 0, *acc_intf_id, acc_dummy_mac, acc_gport, acc_vid, acc_egr_obj);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan create access vp egress obj", rv);

    rv = _brcm_sai_vxlan_create_vxlan_acc_vp(unit, vpn_id, 0, acc_gport, BCM_VXLAN_PORT_MATCH_PORT_VLAN, *acc_egr_obj, acc_vid, acc_vxlan_port);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan create access vp", rv);

    rv = bcm_port_learn_set(unit, *acc_vxlan_port, BCM_PORT_LEARN_FWD);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan set learning to access vp", rv);

    rv = bcm_multicast_vxlan_encap_get(unit, bc_group, acc_gport, *acc_vxlan_port, acc_encap_id);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "bcm_multicast_vxlan_encap_get acc port", rv);

    rv = bcm_multicast_egress_add(unit, bc_group, *acc_vxlan_port, *acc_encap_id);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "bcm_multicast_egress_add acc port", rv);

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vxlan_delete_vxlan_acc_port(bcm_port_t acc_port, bcm_vpn_t vpn_id, uint16_t acc_vid, int bc_group,
                                      bcm_if_t acc_intf_id, bcm_if_t acc_egr_obj, bcm_gport_t acc_vxlan_port, bcm_if_t acc_encap_id)
{
    const int unit = 0;
    sai_status_t rv;

    rv = bcm_multicast_egress_delete(unit, bc_group, acc_vxlan_port, acc_encap_id);
    if ((rv != BCM_E_NONE) && (rv != BCM_E_NOT_FOUND))
    {
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "bcm_multicast_egress_delete acc port", rv);
    }

    rv = bcm_vxlan_port_delete(unit, vpn_id, acc_vxlan_port);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan delete access vp", rv);

    rv = bcm_l3_egress_destroy(unit,acc_egr_obj);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan acc port egress destroy", rv);

#if _BRCM_SAI_VXLAN_CALLER_CREATES_ACCESS_L3_IF
#else
    {
        bcm_l3_intf_t l3_intf;

        bcm_l3_intf_t_init(&l3_intf);
        l3_intf.l3a_intf_id = acc_intf_id;

        rv = bcm_l3_intf_delete(unit, &l3_intf);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "deleting acc intf", rv);
    }
#endif

    /* revert any settings on port from _brcm_sai_vxlan_access_port_settings() */
    rv = bcm_vlan_control_port_set(unit, acc_port, bcmVlanTranslateIngressEnable, 0);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_access_port_settings bcmVlanTranslateIngressEnable", rv);

    return SAI_STATUS_SUCCESS;
}

STATIC int
_brcm_sai_vxlan_get_vlan_by_port(int port, bcm_vlan_t* vlan_id)
{
    const int unit = 0;
    bcm_vlan_data_t *list;
    int count, i;
    sai_status_t rv;

    rv = bcm_vlan_list(unit, &list, &count);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "getting vlan list", rv);

    for (i = 0; i < count; i++)
    {
        if (BCM_PBMP_MEMBER(list[i].port_bitmap, port))
        {
            *vlan_id = list[i].vlan_tag;
            break;
        }
    }
    bcm_vlan_list_destroy(unit, list, count);

    return BCM_E_NONE;
}

STATIC sai_status_t
_brcm_sai_vxlan_create_net_vpn_id(int tunnel_type, bcm_ip_t tnl_local_ip, bcm_ip_t tnl_remote_ip, uint16 udp_dp, bcm_multicast_t bc_group,
                                  _brcm_sai_tunnel_net_vpn_entry_t *net_vpn_entry)
{
    sai_status_t rv;
    const int unit = 0;
    int vni_vlan_idx;
    _brcm_sai_indexed_data_t idata;
    bcm_vpn_t vpn_id;
    uint32 rsvd_vni;

    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_VNI_VLAN, 1, _BRCM_SAI_MAX_VNI_VLAN, &vni_vlan_idx);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "net vpn id reserve", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VNI_VLAN, &vni_vlan_idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "net vpn id index data get", rv);
    idata.vni_vlan.idx = vni_vlan_idx;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_VNI_VLAN, &vni_vlan_idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "net vpn id index data set", rv);

    vpn_id   = vni_vlan_idx + _BRCM_SAI_VNI_VLAN_BASE;
    rsvd_vni = vni_vlan_idx + _BRCM_RSVD_NET_VNI_BASE;

    rv = _brcm_sai_vxlan_create_vxlan_vpn(unit, rsvd_vni, vpn_id, bc_group);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "create vxlan rsvd_vpn", rv);

    PTR_CLEAR(net_vpn_entry, _brcm_sai_tunnel_net_vpn_entry_t);
    net_vpn_entry->type     = tunnel_type;
    net_vpn_entry->src_ip   = tnl_local_ip;
    net_vpn_entry->dst_ip   = tnl_remote_ip;
    net_vpn_entry->udp_dp   = udp_dp;
    net_vpn_entry->bc_group = bc_group;
    net_vpn_entry->vpn_id   = vpn_id;
    net_vpn_entry->vni      = rsvd_vni;

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vxlan_create_net_vport(int net_port, int tunnel_type, bcm_ip_t tnl_local_ip, bcm_ip_t tnl_remote_ip, uint16 udp_dp,
                                 _brcm_sai_tunnel_net_vpn_entry_t *net_vpn_entry, bcm_gport_t *vport_mc)
{
    const int unit = 0;
    sai_status_t rv;
    const bcm_mac_t net_dummy_mac = {0x00, 0x00, 0x01, 0x00, 0x00, 0x01};
    // FIXME: hardcoded mcast group for all VNIs? will sai have an attribute for this someday?
    const bcm_mac_t dlf_mac = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x0A}; /* 224.0.0.10 */
    bcm_if_t net_egr_obj;
    int l2_station_id;
    int l2_station_id_mc;
    bcm_gport_t net_vxlan_port;
    bcm_gport_t vxlan_port_mc;
    bcm_vlan_t net_vid;
    bcm_if_t net_intf_id;
    bcm_if_t egr_obj_mc;
    bcm_l3_intf_t intf;
    bcm_mac_t local_mac_address;
    bcm_gport_t net_gport;

    rv = _brcm_sai_vxlan_net_port_settings(unit, net_port);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan net port control set", rv);

    rv = _brcm_sai_get_local_mac(local_mac_address);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get local mac address", rv);

    rv = bcm_port_gport_get(unit, net_port, &net_gport);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get gport for network port", rv);

    /* NOTE: design change: use underlay_rif attribute of tunnel to get l3a_intf_id for network-facing interface rather than finding it? */
    rv = _brcm_sai_vxlan_get_vlan_by_port(net_port, &net_vid);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get vlan by port", rv);

    intf.l3a_vid = net_vid;
    intf.l3a_vrf = 0;
    sal_memcpy(intf.l3a_mac_addr, local_mac_address, sizeof(intf.l3a_mac_addr));
    rv = bcm_l3_intf_find(unit, &intf);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get l3 intf for net port", rv);
    net_intf_id = intf.l3a_intf_id;

    rv = _brcm_sai_vxlan_search_egr_obj(unit, 0, net_intf_id, net_dummy_mac, net_gport, net_vid, &net_egr_obj);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "search l3 egress for net port", rv);

    if (net_egr_obj == -1)
    {
        rv = _brcm_sai_vxlan_create_egr_obj(unit, 0, net_intf_id, net_dummy_mac, net_gport, net_vid, &net_egr_obj);
       BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create l3 egress for net port", rv);
    }

    rv = _brcm_sai_vxlan_create_vxlan_net_vp(unit, net_vpn_entry->vpn_id, 0, net_gport, BCM_VXLAN_PORT_MATCH_VN_ID,
                                             net_egr_obj, net_vpn_entry->init_id, net_vpn_entry->term_id, &net_vxlan_port);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create vxlan net vp", rv);

    rv = bcm_port_learn_set(unit, net_vxlan_port, BCM_PORT_LEARN_FWD);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "set vxlan learning for net port", rv);

    rv = _brcm_sai_vxlan_add_to_l2_station(unit, local_mac_address, net_vid, 0, &l2_station_id);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "add to l2 station for net port", rv);

    /*
     * DLF/BC network port set up
     */

    /* Egress object for non-UC VXLAN VP, use same interface as UC VXLAN network */
    rv = _brcm_sai_vxlan_search_egr_obj(unit, BCM_L3_IPMC, net_intf_id, dlf_mac, net_gport, net_vid, &egr_obj_mc);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "search l3 egress for net port", rv);

    if (egr_obj_mc == -1)
    {
        rv = _brcm_sai_vxlan_create_egr_obj(unit, BCM_L3_IPMC, net_intf_id, dlf_mac, net_gport, net_vid, &egr_obj_mc);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create mc egress object for net port", rv);
    }

    /* Create non-UC VXLAN VP for network port */
    rv = _brcm_sai_vxlan_create_vxlan_net_vp(unit, net_vpn_entry->vpn_id, BCM_VXLAN_PORT_MULTICAST, net_gport, BCM_VXLAN_PORT_MATCH_NONE,
                                             egr_obj_mc, net_vpn_entry->init_id_mc, net_vpn_entry->term_id_mc, &vxlan_port_mc);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create mc vxlan net vp", rv);

    rv = bcm_port_learn_set(unit, vxlan_port_mc, BCM_PORT_LEARN_FWD);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "set vxlan learning for net mc port", rv);

    /* Station MAC set up */
    rv = _brcm_sai_vxlan_add_to_l2_station(unit, dlf_mac, net_vid, 0, &l2_station_id_mc);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "add to l2 station for mc", rv);

    net_vpn_entry->net_ports[net_vpn_entry->nr_net_ports] = net_port;
    net_vpn_entry->net_egr_obj[net_vpn_entry->nr_net_ports] = net_egr_obj;
    net_vpn_entry->vxlan_port[net_vpn_entry->nr_net_ports] = net_vxlan_port;
    net_vpn_entry->l2_station_id[net_vpn_entry->nr_net_ports] = l2_station_id;
    net_vpn_entry->net_egr_obj_mc[net_vpn_entry->nr_net_ports] = egr_obj_mc;
    net_vpn_entry->vxlan_port_mc[net_vpn_entry->nr_net_ports] = vxlan_port_mc;
    net_vpn_entry->l2_station_id_mc[net_vpn_entry->nr_net_ports] = l2_station_id_mc;
    net_vpn_entry->nr_net_ports++;

    *vport_mc = vxlan_port_mc;

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vxlan_delete_net_vport(int index, _brcm_sai_tunnel_net_vpn_entry_t *net_vpn_entry)
{
    const int unit = 0;
    sai_status_t rv;

    /* DLF/BC network port set up */
    rv = bcm_l2_station_delete(unit, net_vpn_entry->l2_station_id_mc[index]);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "delete l2 station for mc", rv);
    net_vpn_entry->l2_station_id_mc[index] = 0;

    rv = bcm_vxlan_port_delete(unit, net_vpn_entry->vpn_id, net_vpn_entry->vxlan_port_mc[index]);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "delete mc vxlan net vp", rv);
    net_vpn_entry->vxlan_port_mc[index] = 0;

/* NOTE: VxLAN egress objects may be used on multiple vports, cannot delete since may still be in use */
#if 0
    rv = bcm_l3_egress_destroy(unit, net_vpn_entry->net_egr_obj_mc[index]);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "destroy mc egress object for net port", rv);
    net_vpn_entry->net_egr_obj_mc[index] = 0;
#endif

    rv = bcm_l2_station_delete(unit, net_vpn_entry->l2_station_id[index]);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "delete l2 station", rv);
    net_vpn_entry->l2_station_id[index] = 0;

    rv = bcm_vxlan_port_delete(unit, net_vpn_entry->vpn_id, net_vpn_entry->vxlan_port[index]);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "delete vxlan net vp", rv);
    net_vpn_entry->vxlan_port[index] = 0;

/* NOTE: VxLAN egress objects may be used on multiple vports, cannot delete since may still be in use */
#if 0
    rv = bcm_l3_egress_destroy(unit, net_vpn_entry->net_egr_obj[index]);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "destroy egress object for net port", rv);
    net_vpn_entry->net_egr_obj[index] = 0;
#endif

    /* undo port settings from _brcm_sai_vxlan_net_port_settings() */
    /* Disable VXLAN Processing on network port */
    rv = bcm_port_control_set(unit, net_vpn_entry->net_ports[index], bcmPortControlVxlanEnable, FALSE);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vxlan_net_port_settings bcmPortControlVxlanEnable", rv);

    net_vpn_entry->net_ports[index] = 0;

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vxlan_create_net_port(int net_port, int tunnel_type, bcm_ip_t tnl_local_ip, bcm_ip_t tnl_remote_ip, uint16 udp_dp,
                                bcm_multicast_t bc_group, _brcm_sai_tunnel_net_vpn_entry_t *net_vpn_entry)
{
    const int unit = 0;
    int i;
    sai_status_t rv;
    bcm_gport_t net_gport;
    bcm_gport_t vxlan_port_mc;
    bcm_if_t encap_id_mc;
    bool vport_created = FALSE;
    int vport_idx = -1;

    //search if we already created the vport
    for (i = 0; i < net_vpn_entry->nr_net_ports; i++)
    {
        if (net_vpn_entry->net_ports[i] == net_port)
        {
            vport_created = TRUE;
            vport_idx = i;
            break;
        }
    }

    if (!vport_created)
    {
        rv = _brcm_sai_vxlan_create_net_vport(net_port, tunnel_type, tnl_local_ip, tnl_remote_ip, udp_dp, net_vpn_entry, &vxlan_port_mc);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create net vport", rv);
    }
    else
    {
        vxlan_port_mc = net_vpn_entry->vxlan_port_mc[vport_idx];
    }

    //rearrange functions
    rv = bcm_port_gport_get(unit, net_port, &net_gport);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get gport for network port", rv);

    rv = bcm_multicast_vxlan_encap_get(unit, bc_group, net_gport, vxlan_port_mc, &encap_id_mc);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "bcm_multicast_vxlan_encap_get net port", rv);

    rv = bcm_multicast_egress_add(unit, bc_group, vxlan_port_mc, encap_id_mc);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "bcm_multicast_egress_add net port", rv);

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vxlan_create_vpn(bcm_vpn_t vpn_id, uint32_t vni, uint16_t vlan_id, int tunnel_type, bcm_ip_t tnl_local_ip, bcm_ip_t tnl_remote_ip, uint16 vxlan_udp_port)
{
    const int unit = 0;
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_vlan_membr_info_t vlan_members;
    _brcm_sai_vlan_membr_list_t *membr;
    _brcm_sai_tunnel_net_vpn_entry_t net_vpn_entry;
    // FIXME: hardcoded mcast group for all VNIs? will sai have an attribute for this someday?
    const bcm_ip_t tnl_mc_dip = 0xe000000A; /* 224.0.0.10 */
    const uint16 udp_sp = 0xffff;
    const uint8 ttl = 255;
    int net_vid;

    int saved_bcmSwitchGportAnyDefaultL2Learn;
    int saved_bcmSwitchGportAnyDefaultL2Move;

    bcm_gport_t tunnel_init_id, tunnel_term_id;
    bcm_if_t acc_egr_obj;
    bcm_if_t acc_intf_id;
    bcm_gport_t acc_vxlan_port;
    bcm_if_t acc_encap_id;
    bcm_multicast_t bc_group;

    int i;

    bool e;

    int trunk_members[_BRCM_SAI_MAX_NR_TRUNK_MEMBERS];
    int nr_trunk_members;

    /* Check if access VLAN exists */
    rv = _brcm_sai_vlan_exists(vlan_id, &e);
    BRCM_SAI_RV_CHK(SAI_API_VLAN, "vlan exists", rv);
    if (FALSE == e)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "Invalid vlan id %d\n", vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    /* Create multicast group for segment BC/DLF/MC */
    rv = bcm_multicast_create(unit, BCM_MULTICAST_TYPE_VXLAN, &bc_group);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create multicast group", rv);

    /* create vpn */
    rv = _brcm_sai_vxlan_create_vxlan_vpn(unit, vni, vpn_id, bc_group);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "create vxlan vpn", rv);

    /* create vpn for assigning network-vp */
    DATA_CLEAR(net_vpn_entry, _brcm_sai_tunnel_net_vpn_entry_t);
    rv = _brcm_sai_tunnel_net_vpn_entry_key_get(tunnel_type, tnl_local_ip, tnl_remote_ip, vxlan_udp_port, &net_vpn_entry);
    if (rv == SAI_STATUS_ITEM_NOT_FOUND)
    {
        rv = _brcm_sai_vxlan_create_net_vpn_id(tunnel_type, tnl_local_ip, tnl_remote_ip, vxlan_udp_port, bc_group, &net_vpn_entry);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create net vpn", rv);

        rv = _brcm_sai_vxlan_tunnel_initiator_setup(tnl_local_ip, tnl_remote_ip, vxlan_udp_port, udp_sp, ttl, &tunnel_init_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create tunnel initiator setup for net port", rv);
        net_vpn_entry.init_id = tunnel_init_id;

        rv = _brcm_sai_vxlan_tunnel_terminator_setup(tnl_remote_ip, tnl_local_ip, -1, tunnel_init_id, &tunnel_term_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create tunnel terminator setup for net port", rv);
        net_vpn_entry.term_id = tunnel_term_id;

        /* Tunnel Setup (Initiator & Terminator - non-UC) */
        rv = _brcm_sai_vxlan_tunnel_initiator_setup(tnl_local_ip, tnl_mc_dip, vxlan_udp_port, udp_sp, ttl, &tunnel_init_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create mc tunnel initiator setup for net port", rv);
        net_vpn_entry.init_id_mc = tunnel_init_id;

        /* NOTE: design change: the net_vid could come from the underlay_rif? only needed for BUD-mode VxLAN operation? */
        net_vid = -1;
        rv = _brcm_sai_vxlan_tunnel_terminator_setup(tnl_remote_ip, tnl_mc_dip, net_vid, tunnel_init_id, &tunnel_term_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create mc tunnel terminator setup for net port", rv);
        net_vpn_entry.term_id_mc = tunnel_term_id;
    }
    else if (rv != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get net vpn entry", rv);
    }

    /* Disable VP learning globally */
    rv = bcm_switch_control_get(unit, bcmSwitchGportAnyDefaultL2Learn, &saved_bcmSwitchGportAnyDefaultL2Learn);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get bcmSwitchGportAnyDefaultL2Learn", rv);

    rv = bcm_switch_control_get(unit, bcmSwitchGportAnyDefaultL2Move, &saved_bcmSwitchGportAnyDefaultL2Move);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get bcmSwitchGportAnyDefaultL2Move", rv);

    rv = bcm_switch_control_set(unit, bcmSwitchGportAnyDefaultL2Learn, BCM_PORT_LEARN_FWD);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "set bcmSwitchGportAnyDefaultL2Learn", rv);

    rv = bcm_switch_control_set(unit, bcmSwitchGportAnyDefaultL2Move, BCM_PORT_LEARN_FWD);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "set bcmSwitchGportAnyDefaultL2Move", rv);

    /* get all ports configured to participate in the access VLAN */
    DATA_CLEAR(vlan_members, _brcm_sai_vlan_membr_info_t);
    vlan_members.vid = vlan_id;
    tdata.vlan_membrs = &vlan_members;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "error in vlan member table lookup.\n");
        return rv;
    }

    /* configure all ports in access VLAN as access ports */
    membr = vlan_members.membrs;
    while (membr != NULL)
    {
        // membr->membr.type
        // we consider that all vlan members are phy ports
        // FIXME: will future use-cases require that LAGs be VLAN members?
        rv = _brcm_sai_vxlan_create_vxlan_acc_port(membr->membr.val, vpn_id, vlan_id, bc_group,
                                                   &acc_intf_id, &acc_egr_obj, &acc_vxlan_port, &acc_encap_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "create vxlan acc port", rv);

        membr->membr.acc_intf_id = acc_intf_id;
        membr->membr.acc_egr_obj = acc_egr_obj;
        membr->membr.acc_vxlan_port = acc_vxlan_port;
        membr->membr.acc_encap_id = acc_encap_id;

        membr = membr->next;
    }

    /* implementation assumes all ports configured in an trunk should be network ports */
    nr_trunk_members = COUNTOF(trunk_members);
    rv = _brcm_sai_vxlan_get_lag_members(trunk_members, &nr_trunk_members);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get_lag_members", rv);

    for (i = 0; i < nr_trunk_members; i++)
    {
        rv = _brcm_sai_vxlan_create_net_port(trunk_members[i], tunnel_type, tnl_local_ip, tnl_remote_ip,
                                             vxlan_udp_port, bc_group, &net_vpn_entry);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "vxlan create net port", rv);
    }

    rv = bcm_switch_control_set(unit, bcmSwitchGportAnyDefaultL2Learn, saved_bcmSwitchGportAnyDefaultL2Learn);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "restore bcmSwitchGportAnyDefaultL2Learn", rv);

    rv = bcm_switch_control_set(unit, bcmSwitchGportAnyDefaultL2Move, saved_bcmSwitchGportAnyDefaultL2Move);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "restore bcmSwitchGportAnyDefaultL2Move", rv);

    // save into the database
    net_vpn_entry.ref_count++;
    rv = _brcm_sai_tunnel_net_vpn_entry_key_set(&net_vpn_entry);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel net vpn entry DB table entry add", rv);

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_vxlan_delete_vpn(bcm_vpn_t vpn_id, uint32_t vni, uint16_t vlan_id, int tunnel_type, bcm_ip_t tnl_local_ip, bcm_ip_t tnl_remote_ip, uint16 udp_dp)
{
    const int unit = 0;
    sai_status_t rv;
    _brcm_sai_tunnel_net_vpn_entry_t net_vpn_entry;
    int rsvd_vni_vlan_index;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_vlan_membr_info_t vlan_members;
    _brcm_sai_vlan_membr_list_t *membr;
    int i;

    rv = _brcm_sai_tunnel_net_vpn_entry_key_get(tunnel_type, tnl_local_ip, tnl_remote_ip, udp_dp, &net_vpn_entry);
    BRCM_SAI_API_CHK(SAI_API_TUNNEL, "get net vpn entry", rv);

    // get all vlan ports
    DATA_CLEAR(vlan_members, _brcm_sai_vlan_membr_info_t);
    vlan_members.vid = vlan_id;
    tdata.vlan_membrs = &vlan_members;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_VLAN_MEMBRS, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR, "error in vlan member table lookup.\n");
        return rv;
    }

    membr = vlan_members.membrs;
    while (membr != NULL)
    {
        // membr->membr.type
        // we consider that all vlan members are phy ports
        // FIXME: will future use-cases require that LAGs be VLAN members?
        rv = _brcm_sai_vxlan_delete_vxlan_acc_port(membr->membr.val, vpn_id, vlan_id, net_vpn_entry.bc_group,
                                                   membr->membr.acc_intf_id, membr->membr.acc_egr_obj, membr->membr.acc_vxlan_port, membr->membr.acc_encap_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "delete vxlan acc port", rv);

        membr->membr.acc_intf_id = 0;
        membr->membr.acc_egr_obj = 0;
        membr->membr.acc_vxlan_port = 0;
        membr->membr.acc_encap_id = 0;

        membr = membr->next;
    }

    /* delete vpn */
    rv = bcm_vxlan_vpn_destroy(unit, vpn_id);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "destroy vxlan vpn", rv);

    if (net_vpn_entry.ref_count > 0)
    {
        net_vpn_entry.ref_count--;
    }
    if (net_vpn_entry.ref_count != 0)
    {
        /* store updated ref_count data */
        rv = _brcm_sai_tunnel_net_vpn_entry_key_set(&net_vpn_entry);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "set net vpn entry", rv);
    }
    else
    {
        for (i = (net_vpn_entry.nr_net_ports - 1); i >= 0 ; i--)
        {
            rv = _brcm_sai_vxlan_delete_net_vport(i, &net_vpn_entry);
            if (SAI_STATUS_SUCCESS != rv)
            {
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Delete net vport failed: net_ports[%d] = %d",
                                    i, net_vpn_entry.net_ports[i]);
                return rv;
            }
            net_vpn_entry.nr_net_ports--;
        }

        /* destroy tunnel terminators and initiators */
/* NOTE: VxLAN MC terminators and initiators may be used on multiple VPNs,
         cannot delete since may still be in use                         */
#if 0
        rv = bcm_vxlan_tunnel_terminator_destroy(unit, net_vpn_entry.term_id_mc);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "destroy tunnel mc terminator", rv);

        rv = bcm_vxlan_tunnel_initiator_destroy(unit, net_vpn_entry.init_id_mc);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "destroy tunnel mc initiator", rv);
#endif

        rv = bcm_vxlan_tunnel_terminator_destroy(unit, net_vpn_entry.term_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "destroy tunnel terminator", rv);

        rv = bcm_vxlan_tunnel_initiator_destroy(unit, net_vpn_entry.init_id);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "destroy tunnel initiator", rv);

        /* delete network-VP vpn */
        rv = bcm_vxlan_vpn_destroy(unit, net_vpn_entry.vpn_id);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "destroy vxlan vpn", rv);

        /* Delete multicast group for segement BC/DLF/MC */
        rv = bcm_multicast_destroy(unit, net_vpn_entry.bc_group);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "destroy multicast group", rv);

        /* free net vpn index */
        rsvd_vni_vlan_index = net_vpn_entry.vni - _BRCM_RSVD_NET_VNI_BASE;
        if (rsvd_vni_vlan_index >= 0)
        {
            _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_VNI_VLAN, rsvd_vni_vlan_index);
        }
        else
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "free rsvd vni index out of range (%d)", net_vpn_entry.vni);
        }

        rv = _brcm_sai_tunnel_net_vpn_entry_key_delete(&net_vpn_entry);
        BRCM_SAI_API_CHK(SAI_API_TUNNEL, "delete net vpn entry", rv);
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_vxlan_enable(_brcm_sai_tunnel_map_entry_t *tunnel_map_entry)
{
    sai_status_t rv;

    uint32_t vni = tunnel_map_entry->key;
    bcm_vpn_t vpn_id = tunnel_map_entry->vpn;
    uint16_t vlan_id = tunnel_map_entry->val;
    uint16 vxlan_udp_port;
    int tunnel_type;
    int tunnel_idx;
    int term_idx;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idx_data;
    bcm_ip_t dst_ip;
    bcm_ip_t src_ip;

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_MAP, &tunnel_map_entry->tunnel_map, &idx_data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "can't get tunnel_map", rv);
    tunnel_idx = idx_data.tunnel_map.tunnel_idx;

    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "tunnel_idx = %d",
                        tunnel_idx);

    if (tunnel_idx == -1)
    {
        /* tunnel has not been created yet */
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "tunnel_idx not valid in tunnel map entry, create "
                                                 "tunnel before creating tunnel map entry (tunnel map index = %d)\n",
                            tunnel_map_entry->tunnel_map);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tunnel_idx, &idx_data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "can't get tunnel_info", rv);

    if (BRCM_SAI_CHK_OBJ_MISMATCH(idx_data.tunnel_info.term_obj, SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY))
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "term_obj not valid in tunnel info entry, create tunnel "
                                                 "term table entry before creating tunnel map entry "
                                                 "(tunnel map index = 0x%016"PRIx64")\n",
                            idx_data.tunnel_info.term_obj);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    term_idx = BRCM_SAI_GET_OBJ_VAL(int, idx_data.tunnel_info.term_obj);
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "term_idx = %d",
                        term_idx);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_TABLE, &term_idx, &idx_data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel term table data get", rv);
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "term_idx = %d, tid = %d, valid = %d ",
                        term_idx, idx_data.tunnel_table.tid, idx_data.tunnel_table.valid);

    /* FIXME: VxLAN tunnels using IPv6 addresses? */
    src_ip = ntohl(idx_data.tunnel_table.dip.addr.ip4);
    dst_ip = ntohl(idx_data.tunnel_table.sip.addr.ip4);
    tunnel_type = idx_data.tunnel_table.tunnel_type;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_VXLAN_UDP_PORT, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "getting vxlan udp port global data", rv);
    vxlan_udp_port = (uint16)data.u16;

    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_INFO,
                        "create decap tunnel vpn_id=0x%08x vni=%d vlan_id=%u tt=%d src=0x%08x dst=0x%08x vxlan_port=%u \n",
                        vpn_id, vni, vlan_id, tunnel_type, src_ip, dst_ip, vxlan_udp_port);

    rv = _brcm_sai_vxlan_create_vpn(vpn_id, vni, vlan_id, tunnel_type, src_ip, dst_ip, vxlan_udp_port);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "create a vxlan decap tunnel", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_vxlan_disable(_brcm_sai_tunnel_map_entry_t *tunnel_map_entry)
{
    sai_status_t rv;
    uint32_t vni = tunnel_map_entry->key;
    bcm_vpn_t vpn_id = tunnel_map_entry->vpn;
    uint16_t vlan_id = tunnel_map_entry->val;
    uint16 vxlan_udp_port;
    int tunnel_type;
    int tunnel_idx;
    int term_idx;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idx_data;
    bcm_ip_t dst_ip;
    bcm_ip_t src_ip;

    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "tunnel_map_entry->tunnel_map = %d",
                        tunnel_map_entry->tunnel_map);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_MAP, &tunnel_map_entry->tunnel_map, &idx_data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "can't get tunnel_map", rv);
    tunnel_idx = idx_data.tunnel_map.tunnel_idx;

    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "tunnel_idx = %d",
                        tunnel_idx);
    if (tunnel_idx == -1)
    {
        /* tunnel has not been created yet */
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                            "tunnel_idx not valid in tunnel map entry, delete tunnel after deleting tunnel map entry (tunnel map index = %d)",
                            tunnel_map_entry->tunnel_map);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tunnel_idx, &idx_data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "can't get tunnel_info", rv);
    term_idx = BRCM_SAI_GET_OBJ_VAL(int, idx_data.tunnel_info.term_obj);
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "term_idx = %d",
                        term_idx);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_TABLE, &term_idx, &idx_data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel term table data get", rv);
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "term_idx = %d, tid = %d, valid = %d ",
                        term_idx, idx_data.tunnel_table.tid, idx_data.tunnel_table.valid);

    /* FIXME: VxLAN tunnels using IPv6 addresses? */
    src_ip = ntohl(idx_data.tunnel_table.dip.addr.ip4);
    dst_ip = ntohl(idx_data.tunnel_table.sip.addr.ip4);
    tunnel_type = idx_data.tunnel_table.tunnel_type;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_VXLAN_UDP_PORT, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "getting vxlan udp port global data", rv);
    vxlan_udp_port = (uint16)data.u16;

    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_INFO, "delete decap tunnel vpn_id=0x%08x vni=%d vlan_id=%u tt=%d src=0x%08x dst=0x%08x vxlan_port=%u",
                        vpn_id, vni, vlan_id, tunnel_type, src_ip, dst_ip, vxlan_udp_port);

    rv = _brcm_sai_vxlan_delete_vpn(vpn_id, vni, vlan_id, tunnel_type, src_ip, dst_ip, vxlan_udp_port);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "delete a vxlan decap tunnel", rv);

    return SAI_STATUS_SUCCESS;
}

/*
################################################################################
#                              Tunnel functions                                #
################################################################################
*/
/**
 * @brief Create tunnel Map
 *
 * @param[out] tunnel_map_id tunnel Map Id
 * @param[in] attr_count number of attributes
 * @param[in] attr_list array of attributes
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_tunnel_map(_Out_ sai_object_id_t* tunnel_map_id,
                           _In_ sai_object_id_t switch_id,
                           _In_ uint32_t attr_count,
                           _In_ const sai_attribute_t *attr_list)
{
    int i, type;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_indexed_data_t data;
    int idx;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_OBJ_CREATE_PARAM_CHK(tunnel_map_id);

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_TUNNEL_MAP_ATTR_TYPE:
                type = attr_list[i].value.s32;
                break;
            default:
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Unknown/unsupported tunnel map attribute %d passed\n",
                                    attr_list[i].id);
                if (attr_list[i].id >= SAI_TUNNEL_MAP_ATTR_END)
                {
                    return SAI_STATUS_UNKNOWN_ATTRIBUTE_0+i;
                }
                else
                {
                    return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0+i;
                }
        }
    }
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_TUNNEL_MAP, 1, _BRCM_SAI_MAX_TUNNEL_MAP, &idx);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map id reserve", rv);

    data.tunnel_map.idx = idx;
    data.tunnel_map.valid = TRUE;
    data.tunnel_map.type = type;
    data.tunnel_map.tunnel_idx = -1;

    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_MAP, &idx, &data);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unable to save tunnel map data.\n");
        return rv;
    }

    *tunnel_map_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_TUNNEL_MAP, idx);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * @brief Remove tunnel Map
 *
 *  @param[in] tunnel_map_id tunnel Map id to be removed.
 *
 *  @return  SAI_STATUS_SUCCESS on success
 *           Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_tunnel_map(_In_ sai_object_id_t tunnel_map_id)
{
    int idx;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(tunnel_map_id, SAI_OBJECT_TYPE_TUNNEL_MAP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    idx = BRCM_SAI_GET_OBJ_VAL(int, tunnel_map_id);

    if (idx > _BRCM_SAI_MAX_TUNNEL_MAP)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_TUNNEL_MAP, idx);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Set attributes for tunnel map
 *
 * @param[in] tunnel_map_id tunnel Map Id
 * @param[in] attr attribute to set
 *
 * @return  SAI_STATUS_SUCCESS on success
 *          Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_tunnel_map_attribute(_In_ sai_object_id_t tunnel_map_id,
                                  _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * @brief  Get attrbutes of tunnel map
 *
 * @param[in] tunnel_map_id  tunnel map id
 * @param[in] attr_count  number of attributes
 * @param[inout] attr_list  array of attributes
 *
 * @return SAI_STATUS_SUCCESS on success
 *        Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_tunnel_map_attribute(_In_ sai_object_id_t tunnel_map_id,
                                  _In_ uint32_t attr_count,
                                  _Inout_ sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Create tunnel
 *
 * Arguments:
 *    @param[out] tunnel_id - tunnel id
 *    @param[in] attr_count - number of attributes
 *    @param[in] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 *
 * Note: IP address expected in Network Byte Order.
 */
STATIC sai_status_t
brcm_sai_create_tunnel(_Out_ sai_object_id_t* tunnel_id,
                       _In_ sai_object_id_t switch_id,
                       _In_ uint32_t attr_count,
                       _In_ const sai_attribute_t *attr_list)
{
    int i, t, type;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_tunnel_info_t tunnel_info;
    _brcm_sai_indexed_data_t data;
    int tunnel_map_idx = -1;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(tunnel_id);

    /* Reserve an unused tunnel table index */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_TUNNEL_INFO, 1,
                                              max_tunnel_rif, &t);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unexpected tunnel resource issue.\n");
        return rv;
    }
    sal_memset(&tunnel_info, 0, sizeof(_brcm_sai_tunnel_info_t));
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_TUNNEL_ATTR_TYPE:
                type = attr_list[i].value.s32;
                if (!(SAI_TUNNEL_TYPE_IPINIP == type || SAI_TUNNEL_TYPE_VXLAN == type))
                {
                    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                        "Tunnel type %d not implemented.\n",
                                        type);
                    return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
                }
                tunnel_info.type = attr_list[i].value.s32;
                break;
            case SAI_TUNNEL_ATTR_DECAP_MAPPERS: /* FIXME: For now just getting one map object */
                tunnel_info.decap_map = BRCM_SAI_ATTR_LIST_OBJ_LIST(i, 0);
                break;
            case SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE:
                tunnel_info.underlay_if = BRCM_SAI_ATTR_LIST_OBJ(i);
                break;
            case SAI_TUNNEL_ATTR_OVERLAY_INTERFACE:
                tunnel_info.overlay_if = BRCM_SAI_ATTR_LIST_OBJ(i);
                break;
            case SAI_TUNNEL_ATTR_ENCAP_SRC_IP:
                tunnel_info.ip_addr = attr_list[i].value.ipaddr;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_TTL_MODE:
                tunnel_info.encap_ttl_mode = attr_list[i].value.u32;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_TTL_VAL:
                tunnel_info.encap_ttl = attr_list[i].value.u8;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_DSCP_MODE:
                tunnel_info.encap_dscp_mode = attr_list[i].value.u32;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_DSCP_VAL:
                tunnel_info.encap_dscp = attr_list[i].value.u8;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_GRE_KEY_VALID:
            case SAI_TUNNEL_ATTR_ENCAP_GRE_KEY:
            case SAI_TUNNEL_ATTR_ENCAP_ECN_MODE:
            case SAI_TUNNEL_ATTR_ENCAP_MAPPERS:
                return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
            case SAI_TUNNEL_ATTR_DECAP_ECN_MODE:
                if (attr_list[i].value.s32 != SAI_TUNNEL_DECAP_ECN_MODE_COPY_FROM_OUTER)
                {
                    return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
                }
                tunnel_info.ecn = attr_list[i].value.s32;
                break;
            case SAI_TUNNEL_ATTR_DECAP_TTL_MODE:
                tunnel_info.decap_ttl_mode = attr_list[i].value.u32;
                break;
            case SAI_TUNNEL_ATTR_DECAP_DSCP_MODE:
                tunnel_info.decap_dscp_mode = attr_list[i].value.u32;
                break;
            default:
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Unknown/unsupported tunnel attribute %d passed\n",
                                    attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    if (SAI_TUNNEL_TYPE_VXLAN == type)
    {
        tunnel_map_idx = BRCM_SAI_GET_OBJ_VAL(int, tunnel_info.decap_map);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_MAP, &tunnel_map_idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data get", rv);
        data.tunnel_map.tunnel_idx = t;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_MAP, &tunnel_map_idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data set", rv);
    }

    tunnel_info.idx = t;
    tunnel_info.valid = TRUE;
    data.tunnel_info = tunnel_info;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_INFO, &t, &data);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unable to save tunnel info data.\n");
        return rv;
    }
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_INFO, "Created tunnel id: %d\n", t);
    *tunnel_id = BRCM_SAI_CREATE_OBJ_SUB(SAI_OBJECT_TYPE_TUNNEL, type, t);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Remove tunnel
 *
 * Arguments:
 *    @param[in] tunnel_id - tunnel id
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_tunnel(_In_ sai_object_id_t tunnel_id)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int idx, tunnel_map_idx;
    uint32_t tunnel_type;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(tunnel_id, SAI_OBJECT_TYPE_TUNNEL))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    idx = BRCM_SAI_GET_OBJ_VAL(int, tunnel_id);
    if (idx > max_tunnel_rif)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    tunnel_type = (uint32_t)BRCM_SAI_GET_OBJ_SUB_TYPE(tunnel_id);
    if (SAI_TUNNEL_TYPE_VXLAN == tunnel_type)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_INFO, &idx, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unable to get tunnel info data.\n");
        }
        else
        {
            tunnel_map_idx = BRCM_SAI_GET_OBJ_VAL(int, data.tunnel_info.decap_map);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_MAP, &tunnel_map_idx, &data);
            BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data get", rv);
            data.tunnel_map.tunnel_idx = -1;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_MAP, &tunnel_map_idx, &data);
            BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data set", rv);
        }
    }

    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_TUNNEL_INFO, idx);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Set tunnel attribute
 *
 * Arguments:
 *    @param[in] tunnel_id - tunnel id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_tunnel_attribute(_In_ sai_object_id_t tunnel_id,
                              _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Get tunnel attribute
 *
 * Arguments:
 *    @param[in] tunnel _id - tunnel id
 *    @param[in] attr_count - number of attributes
 *    @param[inout] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_tunnel_attribute(_In_ sai_object_id_t tunnel_id,
                              _In_ uint32_t attr_count,
                              _Inout_ sai_attribute_t *attr_list)
{
    int i, tid;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(tunnel_id, SAI_OBJECT_TYPE_TUNNEL))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    tid = BRCM_SAI_GET_OBJ_VAL(int, tunnel_id);
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG,
                        "Get tunnel info: %d\n", tid);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel data get", rv);
    if (!data.tunnel_table.valid)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Invalid tunnel\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_TUNNEL_ATTR_TYPE:
                attr_list[i].value.s32 = data.tunnel_info.type;
                break;
            case SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE:
                BRCM_SAI_ATTR_LIST_OBJ(i) = data.tunnel_info.underlay_if;
                break;
            case SAI_TUNNEL_ATTR_OVERLAY_INTERFACE:
                BRCM_SAI_ATTR_LIST_OBJ(i) = data.tunnel_info.overlay_if;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_SRC_IP:
                attr_list[i].value.ipaddr = data.tunnel_info.ip_addr;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_TTL_MODE:
                attr_list[i].value.u32 = data.tunnel_info.encap_ttl_mode;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_TTL_VAL:
                attr_list[i].value.u8 = data.tunnel_info.encap_ttl;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_DSCP_MODE:
                attr_list[i].value.u32 = data.tunnel_info.encap_dscp_mode;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_DSCP_VAL:
                attr_list[i].value.u8 = data.tunnel_info.encap_dscp;
                break;
            case SAI_TUNNEL_ATTR_ENCAP_GRE_KEY_VALID:
            case SAI_TUNNEL_ATTR_ENCAP_GRE_KEY:
            case SAI_TUNNEL_ATTR_ENCAP_MAPPERS:
            case SAI_TUNNEL_ATTR_DECAP_MAPPERS:
            case SAI_TUNNEL_ATTR_ENCAP_ECN_MODE:
                rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0+i;
                break;
            case SAI_TUNNEL_ATTR_DECAP_ECN_MODE:
                attr_list[i].value.s32 = data.tunnel_info.ecn;
                break;
            case SAI_TUNNEL_ATTR_DECAP_TTL_MODE:
                attr_list[i].value.u32 = data.tunnel_info.decap_ttl_mode;
                break;
            case SAI_TUNNEL_ATTR_DECAP_DSCP_MODE:
                attr_list[i].value.u32 = data.tunnel_info.decap_dscp_mode;
                break;
            default:
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Unknown tunnel attribute %d passed\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_INFO,
                                "Error processing tunnel attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);
    return rv;
}

/**
 * @brief Get tunnel statistics counters.
 *
 * @param[in] tunnel_id Tunnel id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 * @param[out] counters Array of resulting counter values.
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_tunnel_stats(_In_ sai_object_id_t tunnel_id,
                          _In_ uint32_t number_of_counters,
                          _In_ const sai_tunnel_stat_t *counter_ids,
                          _Out_ uint64_t *counters)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * @brief Clear tunnel statistics counters.
 *
 * @param[in] tunnel_id Tunnel id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_clear_tunnel_stats(_In_ sai_object_id_t tunnel_id,
                            _In_ uint32_t number_of_counters,
                            _In_ const sai_tunnel_stat_t *counter_ids)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Create tunnel table entry
 *
 * Arguments:
 *    @param[out] tunnel_term_table_entry_id - tunnel_term_table_entry_id
 *    @param[in] attr_count - number of attributes
 *    @param[in] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 *
 * Note: IP address expected in Network Byte Order.
 */
STATIC sai_status_t
brcm_sai_create_tunnel_term_table_entry(_Out_ sai_object_id_t* tunnel_term_table_entry_id,
                                        _In_ sai_object_id_t switch_id,
                                        _In_ uint32_t attr_count,
                                        _In_ const sai_attribute_t *attr_list)
{
    int t, i, tid;
    sai_status_t rv;
    _brcm_sai_tunnel_info_t tinfo;
    _brcm_sai_indexed_data_t data;
    int vrf = 0, tt = -1, type = -1;
    bcm_tunnel_terminator_t tunnel_term;
    bool sip_chk = FALSE;
    bcm_gport_t tunnel_id = 0;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(tunnel_term_table_entry_id);

    /* Reserve an unused tunnel table index */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_TUNNEL_TABLE, 1,
                                              max_tunnel_rif, &t);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unexpected tunnel term table resource issue.\n");
        return rv;
    }
    bcm_tunnel_terminator_t_init(&tunnel_term);
    memset(&data, 0, sizeof(data));

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_VR_ID:
                tunnel_term.vrf = vrf = BRCM_SAI_ATTR_LIST_OBJ_VAL(bcm_vrf_t, i);
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE:
                tt = attr_list[i].value.u32;
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP:
                memcpy(&data.tunnel_table.dip, &attr_list[i].value.ipaddr, sizeof(data.tunnel_table.dip));
                if (BRCM_SAI_IS_ATTR_FAMILY_IPV4(attr_list[i].value.ipaddr))
                {
                    tunnel_term.dip = ntohl(attr_list[i].value.ipaddr.addr.ip4);
                    tunnel_term.dip_mask = bcm_ip_mask_create(32);
                    if (DEV_IS_TH3())
                    {
                        tunnel_term.type = bcmTunnelTypeIp4In4;
                    }
                    else
                    {
                        tunnel_term.type = bcmTunnelTypeIpAnyIn4;
                    }
                }
                else if (BRCM_SAI_IS_ATTR_FAMILY_IPV6(attr_list[i].value.ipaddr))
                {
                    sal_memcpy(&tunnel_term.dip6,
                               &attr_list[i].value.ipaddr.addr.ip6,
                               sizeof(bcm_ip6_t));
                    bcm_ip6_mask_create((uint8*)&tunnel_term.dip6_mask, 128);
                    if (DEV_IS_TH3())
                    {
                        tunnel_term.type = bcmTunnelTypeIp6In6;
                    }
                    else
                    {
                        tunnel_term.type = bcmTunnelTypeIpAnyIn6;
                    }
                }
                else
                {
                    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                        "Invalid address family passed.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_SRC_IP:
                memcpy(&data.tunnel_table.sip, &attr_list[i].value.ipaddr, sizeof(data.tunnel_table.sip));
                if  (BRCM_SAI_IS_ATTR_FAMILY_IPV4(attr_list[i].value.ipaddr))
                {
                    tunnel_term.sip = ntohl(attr_list[i].value.ipaddr.addr.ip4);
                    tunnel_term.sip_mask = bcm_ip_mask_create(32);
                    if (DEV_IS_TH3())
                    {
                        tunnel_term.type = bcmTunnelTypeIp4In4;
                    }
                    else
                    {
                        tunnel_term.type = bcmTunnelTypeIpAnyIn4;
                    }
                }
                else if (BRCM_SAI_IS_ATTR_FAMILY_IPV6(attr_list[i].value.ipaddr))
                {
                    sal_memcpy(&tunnel_term.sip6,
                               &attr_list[i].value.ipaddr.addr.ip6,
                               sizeof(bcm_ip6_t));
                    bcm_ip6_mask_create((uint8*)&tunnel_term.sip6_mask, 128);
                    if (DEV_IS_TH3())
                    {
                        tunnel_term.type = bcmTunnelTypeIp6In6;
                    }
                    else
                    {
                        tunnel_term.type = bcmTunnelTypeIpAnyIn6;
                    }
                }
                else
                {
                    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                        "Invalid address family passed.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                sip_chk = TRUE;
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TUNNEL_TYPE:
                if (!((SAI_TUNNEL_TYPE_IPINIP == attr_list[i].value.s32) ||
                      (SAI_TUNNEL_TYPE_VXLAN == attr_list[i].value.s32)))
                {
                    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                        "Tunnel type %d not implemented.\n", attr_list[i].value.s32);
                    return SAI_STATUS_NOT_IMPLEMENTED;
                }
                type = attr_list[i].value.s32;
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_ACTION_TUNNEL_ID:
                tid = BRCM_SAI_ATTR_LIST_OBJ_VAL(int, i);
                break;
            default:
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Unknown/unsupported tunnel term table attribute %d passed\n",
                                    attr_list[i].id);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }
    if (SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP == tt && SAI_TUNNEL_TYPE_VXLAN == type)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "P2MP Vxlan tunnels are not implemented.\n");
        return SAI_STATUS_NOT_IMPLEMENTED;
    }
    if (FALSE == sip_chk && SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2P == tt)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "SIP not provided for P2P tunnels.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (sip_chk && SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2MP == tt)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "SIP check not valid for P2MP tunnels.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (SAI_STATUS_SUCCESS != _brcm_sai_tunnel_info_get(tid, &tinfo))
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Invalid tunnel id.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (type != SAI_TUNNEL_TYPE_VXLAN)
    {
        if (SAI_TUNNEL_DSCP_MODE_PIPE_MODEL != tinfo.decap_dscp_mode)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unsupported tunnel dscp mode.\n");
            return SAI_STATUS_INVALID_ATTR_VALUE_0;
        }

        _brcm_sai_switch_pbmp_fp_all_get(&tunnel_term.pbmp);
        rv = _brcm_sai_tunnel_term_add(0, &tinfo, &tunnel_term);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "tunnel term add", rv);

        tunnel_id = tunnel_term.tunnel_id;
    }

    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG, "Tunnel %d term gport: %d(0x%x), src=0x%08x dst=0x%08x\n",
                        tid, tunnel_id, tunnel_id, tunnel_term.sip, tunnel_term.dip);

    data.tunnel_table.idx = t;
    data.tunnel_table.valid = TRUE;
    data.tunnel_table.tunnel_type = type;
    data.tunnel_table.tid = tid;
    data.tunnel_table.vr_id = vrf;
    data.tunnel_table.tunnel_term_entry_type = tt;
    data.tunnel_table.tunnel_id = tunnel_id;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_TABLE, &t, &data);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unable to save tunnel term table data.\n");
        return rv;
    }

    tinfo.in_use++;
    _brcm_sai_tunnel_info_set(tid, &tinfo);

    rv = _brcm_sai_vrf_ref_count_update(vrf, INC);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "vrf refcount inc", rv);

    *tunnel_term_table_entry_id =
        BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY, t);

    /* add reference to term obj into the tunnel object */
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data get", rv);
    data.tunnel_info.term_obj = *tunnel_term_table_entry_id;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Remove tunnel table entry
 *
 * Arguments:
 *    @param[in] tunnel_term_table_entry_id - tunnel term table entry id
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_tunnel_term_table_entry(_In_ sai_object_id_t tunnel_term_table_entry_id)
{
    sai_status_t rv;
    int term_table_entry_index;
    int tid;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_tunnel_info_t tinfo;
    bcm_tunnel_terminator_t tunnel_term;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(tunnel_term_table_entry_id, SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    term_table_entry_index = BRCM_SAI_GET_OBJ_VAL(int, tunnel_term_table_entry_id);
    if (term_table_entry_index > max_tunnel_rif)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_TABLE, &term_table_entry_index, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel term table data get", rv);

    /* remove tunnel terminator from SDK */
    if (data.tunnel_table.tunnel_type != SAI_TUNNEL_TYPE_VXLAN)
    {
        bcm_tunnel_terminator_t_init(&tunnel_term);

        _brcm_sai_switch_pbmp_fp_all_get(&tunnel_term.pbmp);
        tunnel_term.vrf = data.tunnel_table.vr_id;

        if (BRCM_SAI_IS_ATTR_FAMILY_IPV4(data.tunnel_table.dip))
        {
            tunnel_term.dip = ntohl(data.tunnel_table.dip.addr.ip4);
            tunnel_term.dip_mask = bcm_ip_mask_create(32);
            tunnel_term.type = bcmTunnelTypeIpAnyIn4;
        }
        else if (BRCM_SAI_IS_ATTR_FAMILY_IPV6(data.tunnel_table.dip))
        {
            sal_memcpy(&tunnel_term.dip6,
                       &data.tunnel_table.dip.addr.ip6,
                       sizeof(bcm_ip6_t));
            bcm_ip6_mask_create((uint8*)&tunnel_term.dip6_mask, 128);
            tunnel_term.type = bcmTunnelTypeIpAnyIn6;
        }
        else
        {
            /* if this happens, there is a bug in the create function */
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                "Invalid address family in data.tunnel_table.dip.\n");
            return SAI_STATUS_FAILURE;
        }

        if (SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2P == data.tunnel_table.tunnel_term_entry_type)
        {
            if  (BRCM_SAI_IS_ATTR_FAMILY_IPV4(data.tunnel_table.sip))
            {
                tunnel_term.sip = ntohl(data.tunnel_table.sip.addr.ip4);
                tunnel_term.sip_mask = bcm_ip_mask_create(32);
            }
            else if (BRCM_SAI_IS_ATTR_FAMILY_IPV6(data.tunnel_table.sip))
            {
                sal_memcpy(&tunnel_term.sip6,
                           &data.tunnel_table.sip.addr.ip6,
                           sizeof(bcm_ip6_t));
                bcm_ip6_mask_create((uint8*)&tunnel_term.sip6_mask, 128);
            }
            else
            {
                /* if this happens, there is a bug in the create function */
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Invalid address family in data.tunnel_table.sip.\n");
                return SAI_STATUS_FAILURE;
            }
        }

        rv = bcm_tunnel_terminator_delete(0, &tunnel_term);
        BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "tunnel term delete", rv);
    }

    rv = _brcm_sai_vrf_ref_count_update(data.tunnel_table.vr_id, DEC);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "vrf refcount dec", rv);

    tid = data.tunnel_table.tid;
    if (SAI_STATUS_SUCCESS != _brcm_sai_tunnel_info_get(tid, &tinfo))
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Invalid tunnel id.\n");
    }
    else
    {
        if (tinfo.in_use > 0)
        {
            tinfo.in_use--;
            _brcm_sai_tunnel_info_set(tid, &tinfo);
        }
    }

    /* remove reference to term obj from the tunnel object */
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data get", rv);
    data.tunnel_info.term_obj = 0;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tid, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map table data set", rv);

    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_TUNNEL_TABLE, term_table_entry_index);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Set tunnel table entry attribute
 *
 * Arguments:
 *    @param[in] tunnel_term_table_entry_id - tunnel term table entry id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_tunnel_term_table_entry_attribute(_In_ sai_object_id_t tunnel_term_table_entry_id,
                                               _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * Routine Description:
 *    @brief Get tunnel table entry attributes
 *
 * Arguments:
 *    @param[in] tunnel_term_table_entry_id - tunnel term table entry id
 *    @param[in] attr_count - number of attributes
 *    @param[inout] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_tunnel_term_table_entry_attribute(_In_ sai_object_id_t tunnel_term_table_entry_id,
                                               _In_ uint32_t attr_count,
                                               _Inout_ sai_attribute_t *attr_list)
{
    int i, ttte_id;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(tunnel_term_table_entry_id,
                                  SAI_OBJECT_TYPE_TUNNEL_TERM_TABLE_ENTRY))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    ttte_id = BRCM_SAI_GET_OBJ_VAL(int, tunnel_term_table_entry_id);
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_DEBUG,
                        "Get tunnel term table entry: %d\n", ttte_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_TABLE, &ttte_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel table data get", rv);
    if (!data.tunnel_table.valid)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Invalid tunnel term table entry\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_VR_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VIRTUAL_ROUTER, data.tunnel_table.vr_id);
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE:
                attr_list[i].value.u32 = data.tunnel_table.tunnel_term_entry_type;
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP:
                attr_list[i].value.ipaddr = data.tunnel_table.dip;
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_SRC_IP:
                if (SAI_TUNNEL_TERM_TABLE_ENTRY_TYPE_P2P != data.tunnel_table.tunnel_term_entry_type)
                {
                    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                        "SRC IP not applicable for P2P tunnel\n");
                    rv = SAI_STATUS_INVALID_ATTRIBUTE_0+i;
                    break;
                }
                attr_list[i].value.ipaddr = data.tunnel_table.sip;
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TUNNEL_TYPE:
                attr_list[i].value.s32 = data.tunnel_table.tunnel_type;
                break;
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_ACTION_TUNNEL_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) =
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_TUNNEL, data.tunnel_table.tid);
                break;
            default:
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                    "Unknown tunnel term table entry attribute %d passed\n",
                                    attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_INFO,
                                "Error processing tunnel term table entry attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);
    return rv;
}

/**
 * @brief Create tunnel map item
 *
 * @param[out] tunnel_map_entry_id Tunnel map item id
 * @param[in] switch_id Switch Id
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_tunnel_map_entry(_Out_ sai_object_id_t *tunnel_map_entry_id,
                                 _In_ sai_object_id_t switch_id,
                                 _In_ uint32_t attr_count,
                                 _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, map_val, vpn_idx;
    bcm_vpn_t vpn_id;
    int type = -1, val = 0, key_u32 = -1, val_u16 = -1;
    _brcm_sai_tunnel_map_entry_t tunnel_map_entry;
    sai_object_id_t map = SAI_NULL_OBJECT_ID;
    _brcm_sai_indexed_data_t data;
    int map_idx;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(tunnel_map_entry_id);

    for (i = 0; i < attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP_TYPE:
                type = attr_list[i].value.s32;
                break;
            case SAI_TUNNEL_MAP_ENTRY_ATTR_TUNNEL_MAP:
                map = BRCM_SAI_ATTR_LIST_OBJ(i);
                break;
            case SAI_TUNNEL_MAP_ENTRY_ATTR_VNI_ID_KEY:
                key_u32 = attr_list[i].value.u32;
                break;
            case SAI_TUNNEL_MAP_ENTRY_ATTR_VLAN_ID_VALUE:
                val_u16 = attr_list[i].value.u16;
                break;
            default:
                BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Unknown/unsupported tunnel map entry attribute %d passed\n", attr_list[i].id);

                if (attr_list[i].id >= SAI_TUNNEL_MAP_ENTRY_ATTR_END)
                {
                    return SAI_STATUS_UNKNOWN_ATTRIBUTE_0+i;
                }
                else
                {
                    return SAI_STATUS_ATTR_NOT_IMPLEMENTED_0+i;
                }
                break;
        }
    }

    if (-1 == type || SAI_NULL_OBJECT_ID == map)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Tunnel map entry type or tunnel map not available.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    map_idx = BRCM_SAI_GET_OBJ_VAL(int, map);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_MAP, &map_idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "can't get tunnel_map", rv);

    if (type != data.tunnel_map.type)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Tunnel map entry type does not match tunnel map type.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (SAI_TUNNEL_MAP_TYPE_VNI_TO_VLAN_ID == type)
    {
        if (-1 == key_u32 || -1 == val_u16)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Key or val is not available for the map entry.\n");
            return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
        }
    }
    else
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "unsupported map entry type");
        return SAI_STATUS_NOT_IMPLEMENTED;
    }

    DATA_CLEAR(tunnel_map_entry, _brcm_sai_tunnel_map_entry_t);
    tunnel_map_entry.type = type;
    if (SAI_TUNNEL_MAP_TYPE_VNI_TO_VLAN_ID == type)
    {
        map_val = val_u16;
        val     = key_u32;
        rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_VNI_VLAN, 1, _BRCM_SAI_MAX_VNI_VLAN, &vpn_idx);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "vni-vlan vpn id reserve", rv);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VNI_VLAN, &vpn_idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "vni-vlan vpn id index data get", rv);
        data.vni_vlan.idx = vpn_idx;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_VNI_VLAN, &vpn_idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "vni-vlan vpn id index data set", rv);
        vpn_id = vpn_idx + _BRCM_SAI_VNI_VLAN_BASE;

        tunnel_map_entry.key        = key_u32;
        tunnel_map_entry.val        = val_u16;
        tunnel_map_entry.tunnel_map = BRCM_SAI_GET_OBJ_VAL(int, map);
        tunnel_map_entry.vpn        = vpn_id;
    }
    else
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "unsupported map entry type");
        return SAI_STATUS_NOT_IMPLEMENTED;
    }

    /* Create native vxlan tunnel */
    if (SAI_TUNNEL_MAP_TYPE_VNI_TO_VLAN_ID == type)
    {
        rv = _brcm_sai_vxlan_enable(&tunnel_map_entry);
        if (rv != SAI_STATUS_SUCCESS)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Can't create brcm vxlan tunnel\n");
            return SAI_STATUS_FAILURE;
        }
    }

    rv = _brcm_sai_tunnel_map_entry_data_set(&tunnel_map_entry);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map entry data set", rv);

    *tunnel_map_entry_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY, type, map_val, val);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * @brief Remove tunnel map item
 *
 * @param[in] tunnel_map_entry_id Tunnel map item id
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_tunnel_map_entry(_In_ sai_object_id_t tunnel_map_entry_id)
{
    sai_status_t rv;
    int type, map_val, val, vni_vlan_map_idx;
    bcm_vpn_t vpn_id;
    _brcm_sai_tunnel_map_entry_t tunnel_map_entry;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(tunnel_map_entry_id, SAI_OBJECT_TYPE_TUNNEL_MAP_ENTRY))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    type    = BRCM_SAI_GET_OBJ_SUB_TYPE(tunnel_map_entry_id);
    map_val = BRCM_SAI_GET_OBJ_MAP(tunnel_map_entry_id);
    val     = BRCM_SAI_GET_OBJ_VAL(int, tunnel_map_entry_id);

    rv = _brcm_sai_tunnel_map_entry_key_get(type, val, &tunnel_map_entry);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map entry data get", rv);

    /* Delete native vxlan tunnel */
    if (SAI_TUNNEL_MAP_TYPE_VNI_TO_VLAN_ID == type)
    {
        rv = _brcm_sai_vxlan_disable(&tunnel_map_entry);
        if (rv != SAI_STATUS_SUCCESS)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "Can't delete brcm vxlan tunnel\n");
            return SAI_STATUS_FAILURE;
        }
    }

    vpn_id = tunnel_map_entry.vpn;
    if (vpn_id < _BRCM_SAI_VNI_VLAN_BASE)
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR, "vpn_id out of range (0x%x)\n", vpn_id);
    }
    else
    {
        vni_vlan_map_idx = vpn_id - _BRCM_SAI_VNI_VLAN_BASE;
        if (vni_vlan_map_idx > _BRCM_SAI_MAX_VNI_VLAN)
        {
            BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_ERROR,
                                "_BRCM_SAI_INDEXED_VNI_VLAN index out of range (%d)\n",
                                vni_vlan_map_idx);
        }
        else
        {
            _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_VNI_VLAN, vni_vlan_map_idx);
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * @brief Set tunnel map item attribute
 *
 * @param[in] tunnel_map_entry_id Tunnel map item id
 * @param[in] attr Attribute
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_tunnel_map_entry_attribute(_In_ sai_object_id_t tunnel_map_entry_id,
                                        _In_ const sai_attribute_t *attr)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/**
 * @brief Get tunnel map item attributes
 *
 * @param[in] tunnel_map_entry_id Tunnel map item id
 * @param[in] attr_count Number of attributes
 * @param[inout] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_tunnel_map_entry_attribute(_In_ sai_object_id_t tunnel_map_entry_id,
                                        _In_ uint32_t attr_count,
                                        _Inout_ sai_attribute_t *attr_list)
{
    sai_status_t rv = SAI_STATUS_NOT_IMPLEMENTED;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_TUNNEL);
    BRCM_SAI_SWITCH_INIT_CHECK;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_TUNNEL);

    return rv;
}

/*
################################################################################
#                              Internal functions                              #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_tunnel()
{
    sai_status_t rv;
    int max = 0, used = 0;

    rv = _brcm_sai_l3_config_get(7, &max);
    if (BCM_FAILURE(rv) || (0 == max))
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_CRITICAL,
                            "Error %d retreiving max rif !!\n", rv);
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_l3_config_get(3, &max);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "l3 config info", rv);
    rv = _brcm_sai_l3_config_get(4, &used);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "l3 config info", rv);
    max_tunnel_rif = max + used + 1;
    BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_INFO, "Max tunnels: %d.\n",
                        max_tunnel_rif);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TUNNEL_INFO,
                                    max_tunnel_rif))
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing tunnel info state !!\n");
        return SAI_STATUS_FAILURE;
    }
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TUNNEL_TABLE,
                                    max_tunnel_rif))
    {
        BRCM_SAI_LOG_TUNNEL(SAI_LOG_LEVEL_CRITICAL,
                            "Error initializing tunnel info state !!\n");
        return SAI_STATUS_FAILURE;
    }

    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_VNI_VLAN, _BRCM_SAI_MAX_VNI_VLAN);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "Error initializing vni vlan info", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TUNNEL_MAP,
                                     _BRCM_SAI_MAX_TUNNEL_MAP);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "Error initializing tunnel map info", rv);

    rv = _brcm_sai_db_table_create(_BRCM_SAI_TABLE_TNL_MAP_ENT, _BRCM_SAI_MAX_TNL_MAP);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "Error initializing tunnel map table", rv);
    rv = _brcm_sai_db_table_create(_BRCM_SAI_TABLE_TNL_NET_VPN_ENT, _BRCM_SAI_MAX_TNL_NET_VPN);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "Error initializing tunnel net vpn table", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_tunnel()
{
    sai_status_t rv;

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TUNNEL_INFO,
                                      1, max_tunnel_rif, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tunnel info", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TUNNEL_TABLE,
                                      1, max_tunnel_rif, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tunnel table info", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_VNI_VLAN,
                                      1, _BRCM_SAI_MAX_VNI_VLAN, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "freeing vni vlan info", rv);

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TUNNEL_MAP,
                                      1, _BRCM_SAI_MAX_TUNNEL_MAP, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_TUNNEL, SAI_LOG_LEVEL_CRITICAL,
                        "freeing tunnel map info", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_tunnel_info_get(int tunnel_id, _brcm_sai_tunnel_info_t *info)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    if (tunnel_id && tunnel_id < max_tunnel_rif)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TUNNEL_INFO,
                                        (int *)&tunnel_id, &data);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return rv;
        }
        if (data.tunnel_info.valid)
        {
            *info = data.tunnel_info;
            return SAI_STATUS_SUCCESS;
        }
    }
    return SAI_STATUS_FAILURE;
}

void
_brcm_sai_tunnel_info_set(int tunnel_id, _brcm_sai_tunnel_info_t *info)
{
    _brcm_sai_indexed_data_t data;

    data.tunnel_info = *info;
    (void)_brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TUNNEL_INFO, &tunnel_id,
                                     &data);
}

STATIC sai_status_t
_brcm_sai_tunnel_map_entry_data_set(_brcm_sai_tunnel_map_entry_t *tunnel_map_entry)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;

    tdata.tnl_map_entry = tunnel_map_entry;
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_TNL_MAP_ENT, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "tunnel map entry DB table entry add", rv);

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_tunnel_map_entry_key_get(int type, int value,
                                   _brcm_sai_tunnel_map_entry_t *tunnel_map_entry)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;
    _brcm_sai_tunnel_map_entry_t tme;

    DATA_CLEAR(tme, _brcm_sai_tunnel_map_entry_t);
    tme.type = type;
    tme.key = value;
    tdata.tnl_map_entry = &tme;

    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_TNL_MAP_ENT, &tdata);
    if (SAI_STATUS_SUCCESS == rv)
    {
        memcpy(tunnel_map_entry, &tme, sizeof(*tunnel_map_entry));
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_tunnel_net_vpn_entry_key_set(_brcm_sai_tunnel_net_vpn_entry_t *entry)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;

    tdata.tnl_net_vpn_entry = entry;

    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_TNL_NET_VPN_ENT, &tdata);

    return rv;
}

STATIC sai_status_t
_brcm_sai_tunnel_net_vpn_entry_key_get(int type, bcm_ip_t src_ip, bcm_ip_t dst_ip, uint16 udp_dp,
                                       _brcm_sai_tunnel_net_vpn_entry_t *entry)
{
    sai_status_t rv;
    _brcm_sai_tunnel_net_vpn_entry_t search_entry;
    _brcm_sai_table_data_t tdata;

    DATA_CLEAR(search_entry, _brcm_sai_tunnel_net_vpn_entry_t);
    search_entry.type = type;
    search_entry.src_ip = src_ip;
    search_entry.dst_ip = dst_ip;
    search_entry.udp_dp = udp_dp;
    tdata.tnl_net_vpn_entry = &search_entry;

    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_TNL_NET_VPN_ENT, &tdata);
    if (SAI_STATUS_SUCCESS == rv)
    {
        memcpy(entry, &search_entry, sizeof(*entry));
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_tunnel_net_vpn_entry_key_delete(_brcm_sai_tunnel_net_vpn_entry_t *entry)
{
    sai_status_t rv;
    _brcm_sai_table_data_t tdata;

    tdata.tnl_net_vpn_entry = entry;
    rv = _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_TNL_NET_VPN_ENT, &tdata);
    return rv;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_tunnel_api_t tunnel_apis = {
    brcm_sai_create_tunnel_map,
    brcm_sai_remove_tunnel_map,
    brcm_sai_set_tunnel_map_attribute,
    brcm_sai_get_tunnel_map_attribute,
    brcm_sai_create_tunnel,
    brcm_sai_remove_tunnel,
    brcm_sai_set_tunnel_attribute,
    brcm_sai_get_tunnel_attribute,
    brcm_sai_get_tunnel_stats,
    NULL, /* get_tunnel_stats_ext */
    brcm_sai_clear_tunnel_stats,
    brcm_sai_create_tunnel_term_table_entry,
    brcm_sai_remove_tunnel_term_table_entry,
    brcm_sai_set_tunnel_term_table_entry_attribute,
    brcm_sai_get_tunnel_term_table_entry_attribute,
    brcm_sai_create_tunnel_map_entry,
    brcm_sai_remove_tunnel_map_entry,
    brcm_sai_set_tunnel_map_entry_attribute,
    brcm_sai_get_tunnel_map_entry_attribute
};
