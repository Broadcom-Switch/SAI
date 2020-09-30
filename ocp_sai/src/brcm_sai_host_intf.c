/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>

#define LINK_CONTROL_STATE "/proc/bcm/knet/link"

typedef enum _brcm_sai_arp_trap_entry_updates_e {
    _ENTRY_ENABLE = 1,
    _ENTRY_REINSTALL,
    _ENTRY_PRIORITY_SET,
    _ENTRY_QUEUE_SET,
    _ENTRY_POL_DETACH,
    _ENTRY_POL_ACT_ADD,
    _ENTRY_POL_ACT_ADD_ONLY,
    _ENTRY_POL_ACT_REMOVE,
    _ENTRY_DROP_ADD,
    _ENTRY_DROP_ADD_ONLY,
    _ENTRY_DROP_REMOVE,
    _ENTRY_TRAP_REMOVE,
    _ENTRY_COPY_TO_CPU_ADD,
    _ENTRY_COPY_TO_CPU_ADD_ONLY,
    _ENTRY_COPY_TO_CPU_REMOVE,
    _ENTRY_POLICER_ATTACH
} _brcm_sai_arp_trap_entry_updates_t;

/*
################################################################################
#                     Local state - non-persistent across WB                   #
################################################################################
*/
static int _host_intf_count = 0;
static bcm_field_group_t _global_trap_group = 0;

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t
_brcm_sai_netif_create(bcm_knet_netif_t *netif, _brcm_sai_netif_info_t info);

STATIC sai_status_t
_brcm_sai_netfilter_create(bcm_knet_netif_t *netif, sai_hostif_vlan_tag_t tag_mode,
                           bool vlan_filter);

STATIC sai_status_t
_brcm_sai_netif_destroy(sai_object_id_t hif_id);

STATIC sai_status_t
_brcm_sai_netfilter_destroy(sai_object_id_t hif_id, int *netif_id);

STATIC sai_status_t
_brcm_sai_hostif_netif_assign_mac(sai_object_id_t rif_obj, bcm_mac_t *mac);

STATIC sai_status_t
_brcm_sai_update_traps(sai_object_id_t hostif_trap_group_id, int attr_id,
                       _brcm_sai_trap_group_t *tg);

STATIC sai_status_t
_brcm_sai_update_trap_pa(int index, bool add);

STATIC sai_status_t
_brcm_sai_arp_trap_entries_add(int unit, int type, bool state);

STATIC sai_status_t
_brcm_sai_ucast_arp_traps_remove(int type);

STATIC sai_status_t
_brcm_sai_trap_group_add_trap_ref(int trap_group_id, int trap_id);

STATIC sai_status_t
_brcm_sai_trap_group_delete_trap_ref(int trap_group_id, int trap_id);

STATIC sai_status_t
_brcm_sai_entry_policer_actions_add(bcm_field_entry_t entry, uint8_t act,
                                    sai_packet_action_t gpa,
                                    sai_packet_action_t ypa,
                                    sai_packet_action_t rpa);

STATIC sai_status_t
_brcm_sai_entry_policer_actions_remove(bcm_field_entry_t entry, uint8_t act,
                                       sai_packet_action_t gpa,
                                       sai_packet_action_t ypa,
                                       sai_packet_action_t rpa);


STATIC sai_status_t
_brcm_sai_cpuqueue_drop_action_add(bcm_field_entry_t* entry, int queue);

STATIC sai_status_t
_brcm_sai_cpuqueue_copy_action_add(bcm_field_entry_t* entry, int queue);

/*
################################################################################
#                            Host interface defines                            #
################################################################################
*/

/* Macro to create new trap entry using global trap group. Also add
   InPorts qual automatically */
#define BRCM_SAI_NEW_TRAP_ENTRY(__entry)                                \
  do {                                                                  \
      rv = bcm_field_entry_create(0, _global_trap_group, &(__entry));   \
      BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry create", rv);       \
      if (-1 != priority)                                               \
      {                                                                 \
          rv = bcm_field_entry_prio_set(0, (__entry), priority);        \
          BRCM_SAI_API_CHK(SAI_API_HOSTIF, "entry priority set", rv);   \
      }                                                                 \
      rv = bcm_field_qualify_InPorts(0, (__entry), pbmp, mpbmp);        \
      BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);            \
  } while(0)

/*
################################################################################
#                            Host interface functions                          #
################################################################################
*/

/*
* Routine Description:
*    Create host interface.
*
* Arguments:
*    [out] hif_id - host interface id
*    [in] attr_count - number of attributes
*    [in] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_host_interface(_Out_ sai_object_id_t* hif_id,
                               _In_ sai_object_id_t switch_id,
                               _In_ uint32_t attr_count,
                               _In_ const sai_attribute_t *attr_list)
{
    sai_status_t rv;
    int i, type = -1;
    sai_uint32_t port;
    sai_vlan_id_t vlan;
    _brcm_sai_data_t data;
    bcm_knet_netif_t netif;
    _brcm_sai_netif_info_t net_info;
    _brcm_sai_indexed_data_t idata;
    sai_object_id_t rpid = SAI_NULL_OBJECT_ID;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_OBJ_CREATE_PARAM_CHK(hif_id);

    rv = _brcm_sai_global_data_get(_BRCM_SAI_HOST_INTF_COUNT, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "getting global host intfs count", rv);
    if (data.u8 >= _BRCM_SAI_MAX_HOSTIF)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Max host intf count reached.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }
    bcm_knet_netif_t_init(&netif);
    net_info.tag_mode = SAI_HOSTIF_VLAN_TAG_STRIP;
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_HOSTIF_ATTR_TYPE:
                type = attr_list[i].value.u32;
                break;
            case SAI_HOSTIF_ATTR_OBJ_ID:
                rpid = BRCM_SAI_ATTR_LIST_OBJ(i);
                if (!(SAI_OBJECT_TYPE_PORT == BRCM_SAI_GET_OBJ_TYPE(rpid) ||
                      SAI_OBJECT_TYPE_LAG == BRCM_SAI_GET_OBJ_TYPE(rpid) ||
                      SAI_OBJECT_TYPE_VLAN == BRCM_SAI_GET_OBJ_TYPE(rpid)))
                {
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                       "Invalid vlan or port/lag id %d\n",
                                       BRCM_SAI_GET_OBJ_TYPE(rpid));
                    return SAI_STATUS_INVALID_ATTR_VALUE_0 + i;
                }
                /* Bypass CPU ingress logic and directly go to physical port for port hif &
                 * port rif based hif else send pkt into ingress to let the switching logic
                 * do the resolutions and decide the egress intf, vlan, lag/port.
                 */
                if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_GET_OBJ_TYPE(rpid))
                {
                    netif.type = BCM_KNET_NETIF_T_TX_LOCAL_PORT;
                }
                else
                {
                    netif.type = BCM_KNET_NETIF_T_TX_CPU_INGRESS;
                }
                net_info.if_obj = rpid;
                break;
            default:
                break;
        }
    }
    if (-1 == type)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "No supported interface type provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if (_brcm_sai_cpu_pool_config && (netif.type == BCM_KNET_NETIF_T_TX_LOCAL_PORT))
    {
        netif.cosq = BRCM_SAI_CPU_EGRESS_QUEUE_DEFAULT;
    }
    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_HOSTIF_ATTR_TYPE:
                if (attr_list[i].value.u32 != SAI_HOSTIF_TYPE_NETDEV)
                {
                   BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                      "Host Interface type(%d) unsupported\n",
                                      attr_list[i].value.u32);
                   return SAI_STATUS_NOT_IMPLEMENTED;
                }
                break;
            case SAI_HOSTIF_ATTR_OBJ_ID:
                break;
            case SAI_HOSTIF_ATTR_NAME:
                strncpy(netif.name, attr_list[i].value.chardata,
                        SAI_HOSTIF_NAME_SIZE);
                break;
            case SAI_HOSTIF_ATTR_OPER_STATUS:
                net_info.status = attr_list[i].value.booldata;
                break;
            case SAI_HOSTIF_ATTR_QUEUE:
                netif.cosq = attr_list[i].value.u32;
                break;
            case SAI_HOSTIF_ATTR_VLAN_TAG:
                if (attr_list[i].value.u32 >= SAI_HOSTIF_VLAN_TAG_ORIGINAL)
                {
                    return SAI_STATUS_NOT_IMPLEMENTED;
                }
                net_info.tag_mode = attr_list[i].value.u32;
                break;
            default:
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                                   "Un-supported attribute %d passed\n",
                                   attr_list[i].id);
                break;
        }
    }

    if (SAI_NULL_OBJECT_ID == rpid)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "No rif or port id provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    /* Going to SAI v1.0, SAI_HOSTIF_ATTR_RIF_OR_PORT_ID
     * no longer exists. So the "rpid" is now Port, LAG, or
     * VLAN object. Need to check if these objects are router
     * interfaces so the proper MAC address can be configured
     * for the NETIF.
     */
    if (SAI_OBJECT_TYPE_PORT == BRCM_SAI_GET_OBJ_TYPE(rpid))
    {
        int port;

        netif.port = port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, rpid);
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                           "Create port based host interface for port %d\n",
                           netif.port);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_RIF_TABLE,
                                        &port, &idata);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "port rif data get", rv);
        rv = _brcm_sai_hostif_netif_assign_mac(idata.port_rif.rif_obj, &netif.mac_addr);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "assign mac", rv);
        net_info.vlan_rif = FALSE;
        net_info.lag = FALSE;
        rv  = _brcm_sai_netif_create(&netif, net_info);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return rv;
        }
        *hif_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_HOSTIF, 0,
                                              netif.port, netif.id);
    }
    else if (SAI_OBJECT_TYPE_LAG == BRCM_SAI_GET_OBJ_TYPE(rpid))
    {
        int actual, tid;
        bcm_trunk_member_t members;
        bcm_trunk_info_t trunk_info;

        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                           "Create lag based host interface for lag %d\n",
                           BRCM_SAI_GET_OBJ_VAL(bcm_trunk_t, rpid));
        tid = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, rpid);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_LAG_INFO_TABLE,
                                        &tid, &idata);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "lag rif data get", rv);
        rv = _brcm_sai_hostif_netif_assign_mac(idata.lag_info.rif_obj, &netif.mac_addr);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "assign mac", rv);
        /* Change netif type to INGRESS to use the switch pipeline for egress */
        rv = bcm_trunk_get(0, BRCM_SAI_GET_OBJ_VAL(bcm_trunk_t, rpid),
                           &trunk_info, 1, &members, &actual);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "trunk get", rv);
        rv = bcm_port_local_get(0, members.gport, (bcm_port_t*)&port);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "port local get", rv);
        rv = bcm_port_untagged_vlan_get(0, port, &vlan);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "port untagged vlan get", rv);
        netif.vlan = vlan;
        net_info.vlan_rif = TRUE; /* Overloaded to reuse rif logic */
        net_info.lag = TRUE;
        rv  = _brcm_sai_netif_create(&netif, net_info);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return rv;
        }
        *hif_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_HOSTIF, 1,
                                              vlan, netif.id);
    }
    else
    {
        int vlan;

        netif.vlan = vlan = BRCM_SAI_GET_OBJ_VAL(bcm_if_t, rpid);
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                           "Create vlan based host interface for vlan %d\n",
                           netif.vlan);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_VLAN_RIF_TABLE,
                                        &vlan, &idata);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "vlan rif data get", rv);
        rv = _brcm_sai_hostif_netif_assign_mac(idata.vlan_rif.rif_obj, &netif.mac_addr);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "assign mac", rv);
        net_info.vlan_rif = TRUE;
        net_info.lag = FALSE;
        rv  = _brcm_sai_netif_create(&netif, net_info);
        if (SAI_STATUS_SUCCESS != rv)
        {
            return rv;
        }
        *hif_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_HOSTIF,
                                              1, netif.vlan, netif.id);
    }
    /* Set proc link control status */
    {
        char buf[128];
        int nbuf;
        FILE *fp = fopen(LINK_CONTROL_STATE, "w");
        if (fp == NULL)
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "cannot open knet link proc");
        }
        else
        {
            nbuf = snprintf(buf, 128, "%s=%s", netif.name,
                            (TRUE == net_info.status) ? "up" : "down");
            fwrite(buf, nbuf, 1, fp);
            fclose(fp);
        }
    }
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_HOST_INTF_COUNT, INC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error incrementing host intf count "
                           "global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
* Routine Description:
*    Remove host interface
*
* Arguments:
*    [in] hif_id - host interface id
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_host_interface(_In_ sai_object_id_t hif_id)
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    rv = _brcm_sai_netif_destroy(hif_id);
    if (SAI_STATUS_SUCCESS != rv)
    {
        return rv;
    }
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_HOST_INTF_COUNT, DEC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error incrementing host intf count global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
* Routine Description:
*    Set host interface attribute
*
* Arguments:
*    [in] hif_id - host interface id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_host_interface_attribute(_In_ sai_object_id_t hif_id,
                                      _In_ const sai_attribute_t *attr)
{
    int id;
    bool status = FALSE;
    bcm_knet_netif_t *netif;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_SET_OBJ_ATTRIB_PARAM_CHK(hif_id, SAI_OBJECT_TYPE_HOSTIF);

    id = BRCM_SAI_GET_OBJ_VAL(int, hif_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_INFO, &id, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif info get", rv);
    netif = &data.hostif_info.netif;

    switch (attr->id)
    {
        case SAI_HOSTIF_ATTR_OPER_STATUS:
        {
            char buf[128];
            int nbuf;
            FILE *fp = fopen(LINK_CONTROL_STATE, "w");

            if (attr->value.booldata)
            {
                status = TRUE;
            }
            if (fp == NULL)
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "cannot open knet link proc");
                rv = SAI_STATUS_FAILURE;
            }
            else
            {
                nbuf = snprintf(buf, 128, "%s=%s", netif->name,
                                (TRUE == status) ? "up" : "down");
                fwrite(buf, nbuf, 1, fp);
                fclose(fp);
            }
            data.hostif_info.info.status = status;
            break;
        }
        case SAI_HOSTIF_ATTR_VLAN_TAG:
        {
            sai_hostif_vlan_tag_t tag_mode = attr->value.u32;
            int netif_id, idx[2], vlan = netif->vlan;
            _brcm_sai_indexed_data_t _data;
            _brcm_sai_netif_map_t *map;
            bool vlan_filter = FALSE;

            if (BCM_KNET_NETIF_T_TX_CPU_INGRESS == netif->type)
            {
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_VLAN_MAP,
                                                &vlan, &_data);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif vlan map get", rv);
                map = _data.netif_map;
                if (map->num_filters)
                {
                    if (map->netif_id == netif->id)
                    {
                        /* Get one of the filters so we can see if the
                         * VLAN is included in the match criteria.
                         */
                        idx[0] = netif->id;
                        idx[1] = 0;
                        rv = _brcm_sai_indexed_data_get
                                (_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx, &_data);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                                        "hostif vlan filter data get", rv);
                        if (_data.hostif_filter.match_flags & BCM_KNET_FILTER_M_VLAN)
                        {
                            vlan_filter = TRUE;
                        }
                    }
                    else
                    {
                        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                           "Mismatch in DB for vlan netif detected\n");
                        return SAI_STATUS_FAILURE;
                    }
                }
                else
                {
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                       "No filters found for vlan netif\n");
                    return SAI_STATUS_FAILURE;
                }
            }
            if (attr->value.u32 >= SAI_HOSTIF_VLAN_TAG_ORIGINAL)
            {
                rv = SAI_STATUS_NOT_IMPLEMENTED;
            }
            else
            {
                rv = _brcm_sai_netfilter_destroy(hif_id, &netif_id);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "netfilter destroy", rv);
                rv = _brcm_sai_netfilter_create(netif, tag_mode, vlan_filter);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "netfilter create", rv);
            }
            data.hostif_info.info.tag_mode = tag_mode;
            break;
        }
        default:
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                               "Un-supported attribute %d passed\n",
                               attr->id);
            rv = SAI_STATUS_NOT_IMPLEMENTED;
            break;
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_INFO, &id,
                                    &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif info set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
* Routine Description:
*    Get host interface attribute
*
* Arguments:
*    [in] hif_id - host interface id
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_host_interface_attribute(_In_ sai_object_id_t hif_id,
                                      _In_ uint32_t attr_count,
                                      _Inout_ sai_attribute_t *attr_list)
{
    int i, nifid;
    sai_status_t rv;
    bcm_knet_netif_t netif;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(hif_id, SAI_OBJECT_TYPE_HOSTIF))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    nifid = BRCM_SAI_GET_OBJ_VAL(int, hif_id);
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                       "Get host intf: %d\n", nifid);

    rv = bcm_knet_netif_get(0, nifid, &netif);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "knet netif get", rv);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_INFO,
                                    &nifid, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif data get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_HOSTIF_ATTR_TYPE:
                attr_list[i].value.u32 = SAI_HOSTIF_TYPE_NETDEV;
                break;
            case SAI_HOSTIF_ATTR_NAME:
                strncpy(attr_list[i].value.chardata, netif.name,
                        SAI_HOSTIF_NAME_SIZE);
                break;
            case SAI_HOSTIF_ATTR_OBJ_ID:
                BRCM_SAI_ATTR_LIST_OBJ(i) = data.hostif_info.info.if_obj;
                break;
            case SAI_HOSTIF_ATTR_OPER_STATUS:
                attr_list[i].value.booldata = data.hostif_info.info.status;
                break;
            case SAI_HOSTIF_ATTR_QUEUE:
                attr_list[i].value.u32 = netif.cosq;
                break;
            case SAI_HOSTIF_ATTR_VLAN_TAG:
                attr_list[i].value.u32 = data.hostif_info.info.tag_mode;
                break;
            default:
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "Unknown hostif attribute %d passed\n",
                                   attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_INFO,
                               "Error processing hostif attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
* @brief Create host interface table entry
*
* @param[out] hif_table_entry Host interface table entry
* @param[in] switch_id Switch object id
* @param[in] attr_count Number of attributes
* @param[in] attr_list Array of attributes
*
* @return #SAI_STATUS_SUCCESS on success Failure status code on error
*/
STATIC sai_status_t
brcm_sai_create_hostif_table_entry(_Out_ sai_object_id_t *hif_table_entry,
                                   _In_ sai_object_id_t switch_id,
                                   _In_ uint32_t attr_count,
                                   _In_ const sai_attribute_t *attr_list)
{
    int i, idx, tidx;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;
    sai_object_id_t trap_obj = -1, if_obj = -1;
    int entry_type = -1, channel_type = -1;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (IS_NULL(hif_table_entry))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "NULL obj ptr passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    rv = _brcm_sai_global_data_get(_BRCM_SAI_HOST_INTF_ENTRY_COUNT, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "getting global hostif table entry count", rv);
    /* Check if there is space for a new entry */
    if (data.u8 >= _BRCM_SAI_MAX_TRAPS)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Max hostif table entry count reached.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_HOSTIF_TABLE_ENTRY_ATTR_TYPE:
                if (SAI_HOSTIF_TABLE_ENTRY_TYPE_TRAP_ID == attr_list[i].value.s32)
                {
                    return SAI_STATUS_NOT_SUPPORTED;
                }
                entry_type = attr_list[i].value.s32;
                break;
            case SAI_HOSTIF_TABLE_ENTRY_ATTR_OBJ_ID:
                if_obj = attr_list[i].value.oid;
                break;
            case SAI_HOSTIF_TABLE_ENTRY_ATTR_TRAP_ID:
                trap_obj = attr_list[i].value.oid;
                break;
            case SAI_HOSTIF_TABLE_ENTRY_ATTR_CHANNEL_TYPE:
                if ((SAI_HOSTIF_TABLE_ENTRY_CHANNEL_TYPE_CB == attr_list[i].value.s32) ||
                    (SAI_HOSTIF_TABLE_ENTRY_CHANNEL_TYPE_FD == attr_list[i].value.s32))
                {
                    return SAI_STATUS_NOT_SUPPORTED;
                }
                channel_type = attr_list[i].value.s32;
                break;
            case SAI_HOSTIF_TABLE_ENTRY_ATTR_HOST_IF:
                return SAI_STATUS_NOT_SUPPORTED;
            default:
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unimplemented attribute passed\n");
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    if (-1 == entry_type)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "No hostif entry type provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    else if (-1 == channel_type)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "No hostif entry channel type provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    else if (((SAI_HOSTIF_TABLE_ENTRY_TYPE_PORT == entry_type)  ||
              (SAI_HOSTIF_TABLE_ENTRY_TYPE_LAG  == entry_type)  ||
              (SAI_HOSTIF_TABLE_ENTRY_TYPE_VLAN == entry_type)) &&
              (-1 == if_obj))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "No hostif entry if obj provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    else if (((SAI_HOSTIF_TABLE_ENTRY_TYPE_PORT == entry_type)  ||
              (SAI_HOSTIF_TABLE_ENTRY_TYPE_LAG  == entry_type)  ||
              (SAI_HOSTIF_TABLE_ENTRY_TYPE_VLAN == entry_type)  ||
              (SAI_HOSTIF_TABLE_ENTRY_TYPE_TRAP_ID == entry_type)) &&
              (-1 == trap_obj))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "No hostif entry trap obj provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    else if (((SAI_HOSTIF_TABLE_ENTRY_TYPE_PORT == entry_type) &&
              (SAI_OBJECT_TYPE_PORT != BRCM_SAI_GET_OBJ_TYPE(if_obj))) ||
             ((SAI_HOSTIF_TABLE_ENTRY_TYPE_LAG == entry_type) &&
              (SAI_OBJECT_TYPE_LAG != BRCM_SAI_GET_OBJ_TYPE(if_obj))) ||
             ((SAI_HOSTIF_TABLE_ENTRY_TYPE_VLAN == entry_type) &&
              (SAI_OBJECT_TYPE_VLAN != BRCM_SAI_GET_OBJ_TYPE(if_obj))))
    {
        BRCM_SAI_LOG_VLAN(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%016lx passed\n",
                          if_obj);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    /* Reserve new entry */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_HOSTIF_TABLE, 1,
                                              _BRCM_SAI_MAX_HOSTIF_TABLE_ENTRIES, &idx);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unexpected hostif table entry "
                                                "resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Using new hostif table "
                                            "entry idx: %d\n", idx);

    if (SAI_HOSTIF_TABLE_ENTRY_TYPE_WILDCARD == entry_type)
    {
        /* Need to iterate through all traps and update
         * their PAs.
         */
        for (i = 1; i <= _BRCM_SAI_MAX_TRAPS; i++)
        {
            rv = _brcm_sai_update_trap_pa(i, TRUE);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "update trap pa", rv);
        }
    }
    else if (-1 != trap_obj)
    {
        /* Update Trap PA */
        tidx = BRCM_SAI_GET_OBJ_VAL(sai_hostif_trap_type_t, trap_obj);
        rv = _brcm_sai_update_trap_pa(tidx, TRUE);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "update trap pa", rv);
    }

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_TABLE, &idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif table entry get", rv);
    idata.hostif_table.idx = idx;
    idata.hostif_table.valid = TRUE;
    idata.hostif_table.entry_type = entry_type;
    idata.hostif_table.if_obj = if_obj;
    idata.hostif_table.trap_obj = trap_obj;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_TABLE, &idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif table entry set", rv);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_HOST_INTF_ENTRY_COUNT, INC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error incrementing hostif table count "
                           "global data.\n");
        return SAI_STATUS_FAILURE;
    }

    *hif_table_entry = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TABLE_ENTRY, idx);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
* @brief Remove host interface table entry
*
* @param[in] hif_table_entry - host interface table entry
*
* @return #SAI_STATUS_SUCCESS on success Failure status code on error
*/
STATIC sai_status_t
brcm_sai_remove_hostif_table_entry(_In_ sai_object_id_t hif_table_entry)
{
    int i, idx, tidx;
    _brcm_sai_indexed_data_t idata;
    sai_object_id_t trap_obj;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;

    if (BRCM_SAI_CHK_OBJ_MISMATCH(hif_table_entry, SAI_OBJECT_TYPE_HOSTIF_TABLE_ENTRY))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    idx = BRCM_SAI_GET_OBJ_VAL(int, hif_table_entry);
    if (_BRCM_SAI_MAX_TRAPS < idx)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_TABLE, &idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif table get", rv);
    trap_obj = idata.hostif_table.trap_obj;

    if (SAI_HOSTIF_TABLE_ENTRY_TYPE_WILDCARD == idata.hostif_table.entry_type)
    {
        /* Need to iterate through all traps and update
         * their PAs.
         */
        for (i = 1; i <= _BRCM_SAI_MAX_TRAPS; i++)
        {
            rv = _brcm_sai_update_trap_pa(i, FALSE);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "update trap pa", rv);
        }
    }
    else if (SAI_NULL_OBJECT_ID != trap_obj)
    {
        /* Update Trap PA */
        tidx = BRCM_SAI_GET_OBJ_VAL(sai_hostif_trap_type_t, trap_obj);
        rv = _brcm_sai_update_trap_pa(tidx, FALSE);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "update trap pa", rv);
    }

    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_HOSTIF_TABLE, idx);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_HOST_INTF_ENTRY_COUNT, DEC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error decrementing hostif table count "
                           "global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
 * Routine Description:
 *    @brief Create host interface trap group
 *
 * Arguments:
 *  @param[out] hostif_trap_group_id  - host interface trap group id
 *  @param[in] attr_count - number of attributes
 *  @param[in] attr_list - array of attributes
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_hostif_trap_group(_Out_ sai_object_id_t *hostif_trap_group_id,
                                  _In_ sai_object_id_t switch_id,
                                  _In_ uint32_t attr_count,
                                  _In_ const sai_attribute_t *attr_list)
{
    int i, idx;
    sai_status_t rv;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;
    bcm_policer_config_t pol_cfg;
    bcm_policer_t pol_id;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(hostif_trap_group_id))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "NULL obj ptr passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_GROUPS_COUNT, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "getting global trap groups count", rv);
    if (data.u32 >= _BRCM_SAI_MAX_TRAP_GROUPS)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Max trap groups count reached.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Reserve an unused id */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_TRAP_GROUP, 1,
                                              _BRCM_SAI_MAX_TRAP_GROUPS, &idx);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unexpected trap group resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Using trap group id: %d\n", idx);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
    idata.trap_group.idx = idx;
    idata.trap_group.state = TRUE;
    idata.trap_group.qnum = 0;

    for (i=0; i<attr_count; i++)
    {
        switch (attr_list[i].id)
        {
            case SAI_HOSTIF_TRAP_GROUP_ATTR_ADMIN_STATE:
                idata.trap_group.state = attr_list[i].value.booldata;
                break;
            case SAI_HOSTIF_TRAP_GROUP_ATTR_QUEUE:
                idata.trap_group.qnum = attr_list[i].value.u32;
                break;
            case SAI_HOSTIF_TRAP_GROUP_ATTR_POLICER:
                if (BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_LIST_OBJ(i), SAI_OBJECT_TYPE_POLICER))
                {
                    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_TRAP_GROUP, idx);
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                idata.trap_group.policer_id = BRCM_SAI_ATTR_LIST_OBJ(i);
                pol_id = BRCM_SAI_GET_OBJ_VAL(bcm_policer_t, idata.trap_group.policer_id);
                rv = bcm_policer_get(0, pol_id, &pol_cfg);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer get", rv);
                break;
            default:
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unimplemented attribute passed\n");
                _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_TRAP_GROUP, idx);
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group set", rv);

    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_TRAP_GROUPS_COUNT, INC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error incrementing trap groups count "
                           "global data.\n");
        return SAI_STATUS_FAILURE;
    }
    *hostif_trap_group_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, idx);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
 * Routine Description:
 *    @brief Remove host interface trap group
 *
 * Arguments:
 *  @param[in] hostif_trap_group_id - host interface trap group id
 *
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_hostif_trap_group(_In_ sai_object_id_t hostif_trap_group_id)
{
    int idx;
    _brcm_sai_indexed_data_t idata;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(hostif_trap_group_id, SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    idx = BRCM_SAI_GET_OBJ_VAL(int, hostif_trap_group_id);
    if (_BRCM_SAI_MAX_TRAP_GROUPS < idx)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &idx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
    if (idata.trap_group.ref_count)
    {
        return SAI_STATUS_OBJECT_IN_USE;
    }
    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_TRAP_GROUP, idx);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_TRAP_GROUPS_COUNT, DEC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error decrementing trap groups count "
                           "global data.\n");
        return SAI_STATUS_FAILURE;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
 * Routine Description:
 *   @brief Set host interface trap group attribute value.
 *
 * Arguments:
 *    @param[in] hostif_trap_group_id - host interface trap group id
 *    @param[in] attr - attribute
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_hostif_trap_group_attribute(_In_ sai_object_id_t hostif_trap_group_id,
                                         _In_ const sai_attribute_t *attr)
{
    int tgidx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t idata;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "NULL attr passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (BRCM_SAI_CHK_OBJ_MISMATCH(hostif_trap_group_id, SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Invalid object type 0x%016lx passed\n",
                           hostif_trap_group_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    tgidx = BRCM_SAI_GET_OBJ_VAL(int, hostif_trap_group_id);
    if (_BRCM_SAI_MAX_TRAP_GROUPS < tgidx)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &tgidx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
    if (tgidx && FALSE == idata.trap_group.valid)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    switch(attr->id)
    {
        case SAI_HOSTIF_TRAP_GROUP_ATTR_ADMIN_STATE:
            if (idata.trap_group.state != attr->value.booldata)
            {
                idata.trap_group.state = attr->value.booldata;
            }
            break;
        case SAI_HOSTIF_TRAP_GROUP_ATTR_QUEUE:
            if (idata.trap_group.qnum != attr->value.u32)
            {
                idata.trap_group.qnum = attr->value.u32;
            }
            break;
        case SAI_HOSTIF_TRAP_GROUP_ATTR_POLICER:
            if (SAI_NULL_OBJECT_ID != BRCM_SAI_ATTR_PTR_OBJ() &&
                BRCM_SAI_CHK_OBJ_MISMATCH(BRCM_SAI_ATTR_PTR_OBJ(), SAI_OBJECT_TYPE_POLICER))
            {
                return SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            if (idata.trap_group.policer_id != BRCM_SAI_ATTR_PTR_OBJ())
            {
                idata.trap_group.policer_id = BRCM_SAI_ATTR_PTR_OBJ();
            }
            break;
        default:
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unimplemented attribute passed\n");
            return SAI_STATUS_INVALID_PARAMETER;
    }

    rv = _brcm_sai_update_traps(hostif_trap_group_id, attr->id, &idata.trap_group);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "traps update", rv);
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &tgidx, &idata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
 * Routine Description:
 *   @brief get host interface trap group attribute value.
 *
 * Arguments:
 *    @param[in] hostif_trap_group_id - host interface trap group id
 *    @param[in] attr_count - number of attributes
 *    @param[in,out] attr_list - array of attributes
 *
 *
 * Return Values:
 *    @return SAI_STATUS_SUCCESS on success
 *            Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_hostif_trap_group_attribute(_In_ sai_object_id_t hostif_trap_group_id,
                                         _In_ uint32_t attr_count,
                                         _Inout_ sai_attribute_t *attr_list)
{
    int i, idx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(hostif_trap_group_id,
                                      SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP);
    idx = BRCM_SAI_GET_OBJ_VAL(int, hostif_trap_group_id);
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                       "Get host intf trap group: %d\n", idx);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif trap group data get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_HOSTIF_TRAP_GROUP_ATTR_ADMIN_STATE:
                attr_list[i].value.booldata = data.trap_group.state;
                break;
            case SAI_HOSTIF_TRAP_GROUP_ATTR_QUEUE:
                attr_list[i].value.u32 = data.trap_group.qnum;
                break;
            case SAI_HOSTIF_TRAP_GROUP_ATTR_POLICER:
                BRCM_SAI_ATTR_LIST_OBJ(i) = data.trap_group.policer_id;
                break;
            default:
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "Unknown hostif trap group attribute %d passed\n",
                                   attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_INFO,
                               "Error processing hostif trap group attributes\n");
            return rv;
        }
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
 * @brief Create host interface trap
 *
 * @param[out] hostif_trap_id Host interface trap id
 * @param[in] switch_id Switch object id
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Array of attributes
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_create_hostif_trap(_Out_ sai_object_id_t *hostif_trap_id,
                            _In_ sai_object_id_t switch_id,
                            _In_ uint32_t attr_count,
                            _In_ const sai_attribute_t *attr_list)
{
    _brcm_sai_data_t data;
    bcm_pbmp_t pbmp, mpbmp;
    bcm_field_entry_t entry[4];
    int i, idx, queue, entry_type;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    sai_object_id_t trap_group = SAI_NULL_OBJECT_ID;
    _brcm_sai_indexed_data_t idata_t, idata_tg, idata_te;
    bool state = TRUE, exclude_list = FALSE, forward_pa = FALSE;
    int tgidx = 0, priority = -1, pkt_action = -1, policer = -1, trap_id = -1;
    sai_object_id_t def_trap_group = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP, 0);

    /* Note: In v1.0, we only create the ACL but always force the
     * Packet Action to DROP. Then, when the "create_hostif_table_entry"
     * is called, the User configured Packet Action will get applied.
     */

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(hostif_trap_id))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Null attr param\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAPS_COUNT, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "getting global trap count", rv);
    /* Check if there is space for a new entry */
    if (data.u8 >= _BRCM_SAI_MAX_TRAPS)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Max trap count reached.\n");
        return SAI_STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Reserve new entry */
    rv = _brcm_sai_indexed_data_reserve_index(_BRCM_SAI_INDEXED_TRAP, 1,
                                              _BRCM_SAI_MAX_TRAPS, &idx);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unexpected trap resource issue.\n");
        return rv;
    }
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Using new trap idx: %d\n", idx);

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP, &idx, &idata_t);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap get", rv);
    idata_t.trap.idx = idx;
    idata_t.trap.priority = BCM_FIELD_GROUP_PRIO_ANY;
    idata_t.trap.trap_group = def_trap_group;
    idata_t.trap.count = 1;

    BCM_PBMP_CLEAR(pbmp);
    BCM_PBMP_CLEAR(mpbmp);

    if (DEV_IS_TD3())
    {
        /* For TD3, other inports are dont care */
        BCM_PBMP_PORT_ADD(mpbmp, CMIC_PORT(0));
    }
    else
    {
        /* TD2 and TH differ in how rules use InPorts.
         * Even though InPorts is allocated in the Group,
         * TD2 won't add this qualifier to the rule unless
         * it's non-zero. TH always includes InPorts if
         * it's part of the Group. So need to initialize for TH.
         */
        _brcm_sai_switch_pbmp_fp_all_get(&mpbmp);
        pbmp = mpbmp;
    }
    for (i=0; i<attr_count; i++)
    {
        /* Process attributes */
        switch(attr_list[i].id)
        {
            case SAI_HOSTIF_TRAP_ATTR_TRAP_TYPE:
                trap_id = attr_list[i].value.s32;
                break;
            case SAI_HOSTIF_TRAP_ATTR_PACKET_ACTION:
                pkt_action = attr_list[i].value.s32;
                break;
            case SAI_HOSTIF_TRAP_ATTR_TRAP_PRIORITY:
                priority =  attr_list[i].value.u32;
                break;
            case SAI_HOSTIF_TRAP_ATTR_EXCLUDE_PORT_LIST:
            {
                int p;
                if (0 < BRCM_SAI_ATTR_LIST_OBJ_COUNT(i))
                {
                    exclude_list = TRUE;
                }
                if (DEV_IS_TD3())
                {
                    /* For TD3, other inports are dont care */
                    for (p = 0; p < BRCM_SAI_ATTR_LIST_OBJ_COUNT(i); p++)
                    {
                        BCM_PBMP_PORT_ADD(mpbmp, BRCM_SAI_ATTR_LIST_OBJ_LIST_VAL(int, i, p));
                        BCM_PBMP_PORT_ADD(idata_t.trap.exclude_list,
                                          BRCM_SAI_ATTR_LIST_OBJ_LIST_VAL(int, i, p));
                    }
                }
                else
                {
                    for (p = 0; p < BRCM_SAI_ATTR_LIST_OBJ_COUNT(i); p++)
                    {
                        BCM_PBMP_PORT_REMOVE(pbmp, BRCM_SAI_ATTR_LIST_OBJ_LIST_VAL(int, i, p));
                        BCM_PBMP_PORT_ADD(idata_t.trap.exclude_list,
                                          BRCM_SAI_ATTR_LIST_OBJ_LIST_VAL(int, i, p));
                    }
                }
                break;
            }
            case SAI_HOSTIF_TRAP_ATTR_TRAP_GROUP:
                trap_group = BRCM_SAI_ATTR_LIST_OBJ(i);
                if (SAI_NULL_OBJECT_ID == trap_group)
                {
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                else if (BRCM_SAI_CHK_OBJ_MISMATCH(trap_group, SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP))
                {
                    return SAI_STATUS_INVALID_OBJECT_TYPE;
                }
                tgidx = BRCM_SAI_GET_OBJ_VAL(int, trap_group);
                if (_BRCM_SAI_MAX_TRAP_GROUPS < tgidx)
                {
                    return SAI_STATUS_INVALID_OBJECT_ID;
                }
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                                &tgidx, &idata_tg);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
                if (tgidx && FALSE == idata_tg.trap_group.valid)
                {
                    return SAI_STATUS_INVALID_OBJECT_ID;
                }
                break;
            default:
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unimplemented attribute passed\n");
                return SAI_STATUS_INVALID_PARAMETER;
        }
    }

    if (-1 == trap_id)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "No trap id provided.\n");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    /* Only attribute supported for L3_MTU_ERROR is PA. */
    if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR == trap_id)
    {
        if ((-1 != priority) ||
            (TRUE == exclude_list) ||
            (SAI_NULL_OBJECT_ID != trap_group))
        {
            return SAI_STATUS_INVALID_PARAMETER;
        }
    }
    if (SAI_NULL_OBJECT_ID == trap_group)
    {
        /* Get default Trap Group */
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                        &tgidx, &idata_tg);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
    }
    /* At this point, we have a valid Trap Group; either
     * default or User specified.
     */
    if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR != trap_id)
    {
        rv = _brcm_sai_trap_group_add_trap_ref(tgidx, idx);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "group trap ref add", rv);
    }

    /* FIXME - Need to assign default PA per trap type based on
     * Host If proposal 4, version 9. For now, just
     * default to TRAP.
     */
    if (-1 == pkt_action)
    {
        pkt_action = SAI_PACKET_ACTION_TRAP;
    }
    if (SAI_PACKET_ACTION_TRAP    == pkt_action ||
        SAI_PACKET_ACTION_COPY    == pkt_action ||
        SAI_PACKET_ACTION_DROP    == pkt_action ||
        SAI_PACKET_ACTION_FORWARD == pkt_action)
    {
        if (SAI_PACKET_ACTION_FORWARD == pkt_action)
        {
            forward_pa = TRUE;
        }
    }
    else
    {
        return SAI_STATUS_NOT_SUPPORTED;
    }

    idata_t.trap.trap_id = trap_id;
    if (SAI_NULL_OBJECT_ID != trap_group)
    {
        idata_t.trap.trap_group = trap_group;
    }
    if (-1 != priority)
    {
        idata_t.trap.priority = priority;
    }

    /* Load tg state */
    queue = idata_tg.trap_group.qnum;
    state = idata_tg.trap_group.state;
    if (SAI_NULL_OBJECT_ID != idata_tg.trap_group.policer_id)
    {
        policer = BRCM_SAI_GET_OBJ_VAL(int, idata_tg.trap_group.policer_id);
    }

    /* Process traps */
    switch (trap_id)
    {
        case SAI_HOSTIF_TRAP_TYPE_TTL_ERROR:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                /* v4 */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_EtherType(0, entry[0], 0x0800, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_Ttl(0, entry[0], 0, 0xfe);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                 if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                /* v6 */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_EtherType(0, entry[1], 0x86dd, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_Ttl(0, entry[1], 0, 0xfe);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_LACP:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                bcm_mac_t mcast_addr = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x02};
                bcm_mac_t mcast_addr_mask = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
                bcm_ip6_t l2_payload = {0x01, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0};
                bcm_ip6_t l2_payload_mask = {0xff, 0, 0, 0, 0, 0, 0, 0,
                                                    0, 0, 0, 0, 0, 0, 0, 0};
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_DstMac(0, entry[0], mcast_addr, mcast_addr_mask);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_EtherType(0, entry[0], 0x8809, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                /* For some reason, DstIp6 is an overlay of the L2 payload if the
                 * packet is not IP/IPv6.
                 */
                rv = bcm_field_qualify_DstIp6(0, entry[0], l2_payload, l2_payload_mask);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_BGP:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {

                /* Entry with src port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_EtherType(0, entry[0], 0x0800, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[0], 6, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4SrcPort(0, entry[0], 179, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4SrcPort", rv);
                rv = bcm_field_qualify_DstClassL3(0, entry[0], _BRCM_SAI_IP2ME_CLASS, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify dstL3class", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }

                /* Entry with dst port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_EtherType(0, entry[1], 0x0800, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[1], 6, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[1], 179, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4DstPort", rv);
                rv = bcm_field_qualify_DstClassL3(0, entry[1], _BRCM_SAI_IP2ME_CLASS, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify dstL3class", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install",
                                 rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_BGPV6:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                /* Entry with src port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_EtherType(0, entry[0], 0x86dd, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[0], 6, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4SrcPort(0, entry[0], 179, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4SrcPort", rv);
                rv = bcm_field_qualify_DstClassL3(0, entry[0], _BRCM_SAI_IP2ME_CLASS, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify dstL3class", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }

                /* Entry with dst port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_EtherType(0, entry[1], 0x86dd, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[1], 6, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[1], 179, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4DstPort", rv);
                rv = bcm_field_qualify_DstClassL3(0, entry[1], _BRCM_SAI_IP2ME_CLASS, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify dstL3class", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_DHCP:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                /* V4 entry with src port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_EtherType(0, entry[0], 0x0800, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[0], 17, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4SrcPort(0, entry[0], 68, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4SrcPort", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[0], 67, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4DstPort", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }

                /* V4 entry with dst port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_EtherType(0, entry[1], 0x0800, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[1], 17, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4SrcPort(0, entry[1], 67, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4SrcPort", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[1], 68, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4DstPort", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                 if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_DHCPV6:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                /* V6 entry with src port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_EtherType(0, entry[0], 0x86dd, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[0], 17, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify IpProtocol", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[0], 547, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4DstPort", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                 if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }

                /* V6 entry with dst port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_EtherType(0, entry[1], 0x86dd, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = bcm_field_qualify_IpProtocol(0, entry[1], 17, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify IpProtocol", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[1], 546, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify L4DstPort", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                 if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_SSH:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                /* L4 Src Port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_IpProtocol(0, entry[0], 6, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4SrcPort(0, entry[0], 22, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_DstClassL3(0, entry[0], _BRCM_SAI_IP2ME_CLASS, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                 if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                /* L4 Dst Port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_IpProtocol(0, entry[1], 6, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[1], 22, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_DstClassL3(0, entry[1], _BRCM_SAI_IP2ME_CLASS, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_SNMP:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                /* UDP - L4 Src Port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_IpProtocol(0, entry[0], 17, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4SrcPort(0, entry[0], 161, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                 if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                /* UDP - L4 Dst Port */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_IpProtocol(0, entry[1], 17, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_L4DstPort(0, entry[1], 161, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_IPV6_NEIGHBOR_DISCOVERY:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action ||
                SAI_PACKET_ACTION_COPY == pkt_action ||
                SAI_PACKET_ACTION_DROP == pkt_action)
            {
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_EtherType(0, entry[0], 0x86dd, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_Ip6NextHeader(0, entry[0], 58, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_Ip6HopLimit(0, entry[0], 255, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                /*
                 * The following types of Neighbor Discovery packets
                 * are trapped:
                 *
                 * Router Solicitation    - Type Code = 133 (0x85)
                 * Router Advertisement   - Type Code = 134 (0x86)
                 * Neighbor Solicitation  - Type Code = 135 (0x87)
                 * Neighbor Advertisement - Type Code = 136 (0x88)
                 * Redirect               - Type Code = 137 (0x89)
                 *
                 */
                rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_RANGE_ID, &data);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "getting global range id", rv);
                rv = bcm_field_qualify_RangeCheck(0, entry[0], data.u32, 0);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                if (SAI_PACKET_ACTION_COPY == pkt_action)
                {
                    rv = _brcm_sai_cpuqueue_copy_action_add(&entry[0], queue);
                }
                else
                {
                    rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                }

                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install",
                                 rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST:
        case SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action ||
                SAI_PACKET_ACTION_COPY == pkt_action ||
                SAI_PACKET_ACTION_DROP == pkt_action)
            {
                bcm_mac_t bcast_addr = _BCAST_MAC_ADDR;

                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_DstMac(0, entry[0], bcast_addr, bcast_addr);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_EtherType(0, entry[0], 0x0806, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_entry_field_qualify(entry[0], trap_id);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                if (forward_pa) /* temp for compiler warning */
                {
                    //FIXME: handle default PA
                }
                if (SAI_PACKET_ACTION_COPY == pkt_action)
                {
                    rv = _brcm_sai_cpuqueue_copy_action_add(&entry[0], queue);
                }
                else
                {
                    rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                }
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                data.u32 = idx;
                if (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == trap_id)
                {
                    rv = _brcm_sai_global_data_set(_BRCM_SAI_ARP_TRAP_REQ, &data);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                                    "setting arp trap req state global data", rv);
                }
                else
                {
                    rv = _brcm_sai_global_data_set(_BRCM_SAI_ARP_TRAP_RESP, &data);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                                    "setting arp trap resp state global data", rv);
                }
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_LLDP:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                bcm_mac_t mcast_addr = {0x01, 0x80, 0xc2, 0x00, 0x00, 0x00};
                bcm_mac_t mcast_addr_mask = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

                /* MCAST 01:80:c2:00:00:00 */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_DstMac(0, entry[0], mcast_addr, mcast_addr_mask);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_EtherType(0, entry[0], 0x88cc, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                /* MCAST 01:80:c2:00:00:03 */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                mcast_addr[5] = 0x03;
                rv = bcm_field_qualify_DstMac(0, entry[1], mcast_addr, mcast_addr_mask);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_EtherType(0, entry[1], 0x88cc, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                /* MCAST 01:80:c2:00:00:0e */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[2]);

                mcast_addr[5] = 0x0e;
                rv = bcm_field_qualify_DstMac(0, entry[2], mcast_addr, mcast_addr_mask);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = bcm_field_qualify_EtherType(0, entry[2], 0x88cc, 0xffff);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field qualify EtherType", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[2], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[2], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[2]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[2], 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.entry[2] = entry[2];
                idata_t.trap.count = 3;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_IP2ME:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_DstClassL3(0, entry[0], _BRCM_SAI_IP2ME_CLASS, 0xff);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = bcm_field_entry_enable_set(0, entry[0], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_UDLD:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                bcm_mac_t mcast_addr = {0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc};
                bcm_mac_t mcast_addr_mask = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

                /* MCAST 01:00:0c:cc:cc:cc */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = bcm_field_qualify_DstMac(0, entry[0], mcast_addr, mcast_addr_mask);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        case SAI_HOSTIF_TRAP_TYPE_PIPELINE_DISCARD_ROUTER:
        {
            if (SAI_PACKET_ACTION_TRAP == pkt_action || SAI_PACKET_ACTION_DROP == pkt_action)
            {
                bcm_if_t if_id;

                /* Trap drop if (black hole) */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[0]);

                rv = _brcm_sai_drop_if_get(&if_id);
                BRCM_SAI_RV_CHK(SAI_API_VLAN, "getting global drop intf", rv);
                rv = bcm_field_qualify_DstL3Egress(0, entry[0], if_id);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[0], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[0], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[0]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[0], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }

                /* Trap for ip checksum error */
                BRCM_SAI_NEW_TRAP_ENTRY(entry[1]);

                rv = bcm_field_qualify_IpInfo(0, entry[1],
                                              0, BCM_FIELD_IP_CHECKSUM_OK);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
                rv = _brcm_sai_cpuqueue_drop_action_add(&entry[1], queue);
                if (rv != SAI_STATUS_SUCCESS)
                {
                  return rv;
                }
                if (-1 != policer)
                {
                    rv = bcm_field_entry_policer_attach(0, entry[1], 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
                rv = bcm_field_entry_install(0, entry[1]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(entry[1], 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                idata_t.trap.group = _global_trap_group;
                idata_t.trap.entry[0] = entry[0];
                idata_t.trap.entry[1] = entry[1];
                idata_t.trap.count = 2;
            }
            else
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            break;
        }
        /* Special handling for SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR.
         * This is a global switch-level control that applies
         * to all ports. We can only enable/disable this globally.
         */
        case SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR:
        {
            /* For SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR, we enable bcmSwitchL3MtuFailToCpu
             * for WILDCARD entries. This occurs in _brcm_sai_update_trap_pa().
             */
            if (SAI_PACKET_ACTION_TRAP != pkt_action && SAI_PACKET_ACTION_DROP != pkt_action)
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            /* To ensure no FP entry processing occurs. */
            idata_t.trap.count = 0;
            break;
        }
        default:
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unknown trap id %x", trap_id);
            return SAI_STATUS_NOT_IMPLEMENTED;
    }

    idata_t.trap.pkta = pkt_action;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP, &idx, &idata_t);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap set", rv);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_TRAPS_COUNT, INC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error incrementing traps count "
                           "global data.\n");
        return SAI_STATUS_FAILURE;
    }

    if (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == trap_id ||
        SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE == trap_id)
    {
        rv = _brcm_sai_arp_trap_entries_add(0,
                 SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == trap_id ? 0 : 1, state);
        BRCM_SAI_RV_CHK(SAI_API_SWITCH, "arp trap entries add", rv);
    }

    /* Need to iterate through all trap table entries.
     * If there are any WILDCARD ones, then apply User PA.
     */
    for (i = 1; i <= _BRCM_SAI_MAX_HOSTIF_TABLE_ENTRIES; i++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_TABLE, &i, &idata_te);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif table entry get", rv);
        entry_type = idata_te.hostif_table.entry_type;
        if ((TRUE == idata_te.hostif_table.valid) &&
            (SAI_HOSTIF_TABLE_ENTRY_TYPE_WILDCARD == entry_type))
        {
            rv = _brcm_sai_update_trap_pa(idx, (SAI_PACKET_ACTION_COPY != pkt_action));
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "update trap pa", rv);
            break;
        }
    }
    *hostif_trap_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TRAP, idx);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/**
 * @brief Remove host interface trap
 *
 * @param[in] hostif_trap_id Host interface trap id
 *
 * @return #SAI_STATUS_SUCCESS on success Failure status code on error
 */
STATIC sai_status_t
brcm_sai_remove_hostif_trap(_In_ sai_object_id_t hostif_trap_id)
{
    int idx, tgidx, e;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    _brcm_sai_indexed_data_t idata_t, idata_tg;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(hostif_trap_id, SAI_OBJECT_TYPE_HOSTIF_TRAP))
    {
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    idx = BRCM_SAI_GET_OBJ_VAL(int, hostif_trap_id);
    if (_BRCM_SAI_MAX_TRAPS < idx)
    {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP,
                                    &idx, &idata_t);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap get", rv);

    /* Not creating any FP entries for L3_MTU_ERROR, so
     * no need to remove any FP entries.
     */
    if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR != idata_t.trap.trap_id)
    {
        for (e=0; e<idata_t.trap.count; e++)
        {
            rv = bcm_field_entry_destroy(0, idata_t.trap.entry[e]);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry destroy", rv);
        }

        tgidx = BRCM_SAI_GET_OBJ_VAL(int, idata_t.trap.trap_group);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                        &tgidx, &idata_tg);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
        rv = _brcm_sai_trap_group_delete_trap_ref(tgidx, idx);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "group trap ref delete", rv);

        if (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == idata_t.trap.trap_id ||
            SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE == idata_t.trap.trap_id)
        {
            rv = _brcm_sai_ucast_arp_traps_remove
                    (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == idata_t.trap.trap_id ? 0 : 1);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "ucast arp traps remove", rv);
        }
    }
    else
    {
        rv =  bcm_switch_control_set(0, bcmSwitchL3MtuFailToCpu, 0);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "setting l3 mtu fail to cpu", rv);
    }

    _brcm_sai_indexed_data_free_index(_BRCM_SAI_INDEXED_TRAP, idx);
    if (SAI_STATUS_SUCCESS !=
        _brcm_sai_global_data_bump(_BRCM_SAI_TRAPS_COUNT, DEC))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error decrementing trap count "
                           "global data.\n");
        return SAI_STATUS_FAILURE;
    }
    _brcm_sai_policer_action_ref_detach(hostif_trap_id);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

STATIC sai_status_t
_brcm_sai_arp_trap_entries_update_inports(int arp_trap_id, bcm_pbmp_t pbmp,
                                          bcm_pbmp_t mpbmp)

{
    int type;
    _brcm_sai_unicast_arp_t uat;
    _brcm_sai_table_data_t tdata;
    sai_status_t _rv, rv = SAI_STATUS_SUCCESS;

    type = SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == arp_trap_id ? 0 : 1;

    DATA_CLEAR(uat, _brcm_sai_unicast_arp_t);
    tdata.ucast_arp = &uat;
    do
    {
        rv = _brcm_sai_db_table_entry_getnext(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
        if (SAI_STATUS_SUCCESS == rv && type == uat.type && FALSE == uat.sysmac)
        {
            _rv = bcm_field_qualify_InPorts(0, uat.entry_id, pbmp, mpbmp);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", _rv);
            _rv = bcm_field_entry_reinstall(0, uat.entry_id);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
        }
    } while (SAI_STATUS_SUCCESS == rv);
    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        rv = SAI_STATUS_SUCCESS;
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_arp_trap_entries_update(int arp_trap_id, int update_type, bool state,
                                  int priority, int qnum, int policer,
                                  _brcm_sai_policer_pa_t *ppa)
{
    int type;
    _brcm_sai_unicast_arp_t uat;
    _brcm_sai_table_data_t tdata;
    sai_status_t _rv, rv = SAI_STATUS_SUCCESS;

    type = SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == arp_trap_id ? 0 : 1;

    DATA_CLEAR(uat, _brcm_sai_unicast_arp_t);
    tdata.ucast_arp = &uat;
    do
    {
        rv = _brcm_sai_db_table_entry_getnext(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
        if (SAI_STATUS_SUCCESS == rv && type == uat.type && FALSE == uat.sysmac)
        {
            switch (update_type)
            {
                case _ENTRY_ENABLE:
                    _rv = _brcm_entry_enable(uat.entry_id, state);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable", _rv);
                    break;
                case _ENTRY_REINSTALL:
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_PRIORITY_SET:
                    _rv = bcm_field_entry_prio_set(0, uat.entry_id, priority);
                    BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "entry priority set", _rv);
                    break;
                case _ENTRY_QUEUE_SET:
                    _rv = bcm_field_action_remove(0, uat.entry_id, bcmFieldActionCosQCpuNew);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    _rv = bcm_field_action_add(0, uat.entry_id, bcmFieldActionCosQCpuNew,
                                               qnum, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_POL_DETACH:
                    _rv = bcm_field_entry_policer_detach(0, uat.entry_id, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer detach", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_POL_ACT_ADD_ONLY:
                    _rv = _brcm_sai_entry_policer_actions_add(uat.entry_id,
                                                              ppa->act, ppa->gpa,
                                                              ppa->ypa, ppa->rpa);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions add", _rv);
                    break;
                case _ENTRY_POL_ACT_ADD:
                    _rv = _brcm_sai_entry_policer_actions_add(uat.entry_id,
                                                              ppa->act, ppa->gpa,
                                                              ppa->ypa, ppa->rpa);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions add", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_POL_ACT_REMOVE:
                    _rv = _brcm_sai_entry_policer_actions_remove(uat.entry_id,
                                                                 ppa->act, ppa->gpa,
                                                                 ppa->ypa, ppa->rpa);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions delete", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_DROP_ADD_ONLY:
                    _rv = bcm_field_action_add(0, uat.entry_id, bcmFieldActionDrop, 0, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", _rv);
                    break;
                case _ENTRY_DROP_ADD:
                    _rv = bcm_field_action_add(0, uat.entry_id, bcmFieldActionDrop, 0, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_DROP_REMOVE:
                    _rv = bcm_field_action_remove(0, uat.entry_id, bcmFieldActionDrop);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_COPY_TO_CPU_REMOVE:
                    _rv = bcm_field_action_remove(0, uat.entry_id, bcmFieldActionCopyToCpu);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_COPY_TO_CPU_ADD_ONLY:
                    _rv = bcm_field_action_add(0, uat.entry_id, bcmFieldActionCopyToCpu, 0, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", _rv);
                    break;
                case _ENTRY_COPY_TO_CPU_ADD:
                    _rv = bcm_field_action_add(0, uat.entry_id, bcmFieldActionCopyToCpu, 0, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_TRAP_REMOVE:
                    _rv = bcm_field_action_remove(0, uat.entry_id, bcmFieldActionCopyToCpu);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", _rv);
                    _rv = bcm_field_action_remove(0, uat.entry_id, bcmFieldActionDrop);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", _rv);
                    _rv = bcm_field_entry_reinstall(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", _rv);
                    break;
                case _ENTRY_POLICER_ATTACH:
                    _rv = bcm_field_entry_policer_attach(0, uat.entry_id, 0, policer);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", _rv);
                    break;
                default:
                    return SAI_STATUS_INVALID_PARAMETER;
            }
        }
    } while (SAI_STATUS_SUCCESS == rv);
    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        rv = SAI_STATUS_SUCCESS;
    }

    return rv;
}

/*
* Routine Description:
*   Set trap attribute value.
*
* Arguments:
*    [in] hostif_trap_id - host interface trap id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_hostif_trap_attribute(_In_ sai_object_id_t hostif_trap_id,
                                   _In_ const sai_attribute_t *attr)
{
    bcm_pbmp_t pbmp, mpbmp;
    bcm_field_entry_t entry[4];
    int act = 0, arp_type = -1;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    sai_packet_action_t gpa=0, ypa=0, rpa=0;
    _brcm_sai_indexed_data_t idata_t, idata_tg;
    int e, idx, queue, new_queue = 0, tgidx = 0;
    bool new_entry = FALSE, exclude_list = FALSE;
    sai_object_id_t trap_group = SAI_NULL_OBJECT_ID;
    sai_object_id_t policer_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t new_policer_id = SAI_NULL_OBJECT_ID;
    bool state = TRUE, new_state = TRUE, change = FALSE;
    int priority = -1, pkt_action = -1, policer = -1, new_policer = -1;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Null attr param\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (BRCM_SAI_CHK_OBJ_MISMATCH(hostif_trap_id, SAI_OBJECT_TYPE_HOSTIF_TRAP))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%016lx passed\n",
                          hostif_trap_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    idx = BRCM_SAI_GET_OBJ_VAL(sai_hostif_trap_type_t, hostif_trap_id);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP, &idx, &idata_t);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap get", rv);
    if (FALSE == idata_t.trap.valid)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Invalid hostif trap.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    _brcm_sai_switch_pbmp_fp_all_get(&mpbmp);
    pbmp = mpbmp;

    /* Process attributes */
    switch(attr->id)
    {
        case SAI_HOSTIF_TRAP_ATTR_TRAP_TYPE:
            return SAI_STATUS_INVALID_PARAMETER;
        case SAI_HOSTIF_TRAP_ATTR_PACKET_ACTION:
            pkt_action = attr->value.s32;
            break;
        case SAI_HOSTIF_TRAP_ATTR_TRAP_PRIORITY:
        {
            if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR == idata_t.trap.trap_id)
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            priority =  attr->value.u32;
            break;
        }
        case SAI_HOSTIF_TRAP_ATTR_EXCLUDE_PORT_LIST:
        {
            int p, port;
            if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR == idata_t.trap.trap_id)
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            if (0 < BRCM_SAI_ATTR_PTR_OBJ_COUNT())
            {
                exclude_list = TRUE;
            }
            BCM_PBMP_CLEAR(idata_t.trap.exclude_list);
            if (DEV_IS_TD3())
            {
                /* For TD3, other inports are dont care */
                for (p = 0; p < BRCM_SAI_ATTR_PTR_OBJ_COUNT(); p++)
                {
                    port = BRCM_SAI_GET_OBJ_VAL(int, BRCM_SAI_ATTR_PTR_OBJ_LIST(p));
                    BCM_PBMP_PORT_ADD(mpbmp, port);
                    BCM_PBMP_PORT_ADD(idata_t.trap.exclude_list, port);
                }
            }
            else
            {
                for (p = 0; p < BRCM_SAI_ATTR_PTR_OBJ_COUNT(); p++)
                {
                    port = BRCM_SAI_GET_OBJ_VAL(int, BRCM_SAI_ATTR_PTR_OBJ_LIST(p));
                    BCM_PBMP_PORT_REMOVE(pbmp, port);
                    BCM_PBMP_PORT_ADD(idata_t.trap.exclude_list, port);
                }
            }
            break;
        }
        case SAI_HOSTIF_TRAP_ATTR_TRAP_GROUP:
        {
            if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR == idata_t.trap.trap_id)
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            trap_group = BRCM_SAI_ATTR_PTR_OBJ();
            if (SAI_NULL_OBJECT_ID == trap_group)
            {
                return SAI_STATUS_INVALID_PARAMETER;
            }
            else if (BRCM_SAI_CHK_OBJ_MISMATCH(trap_group, SAI_OBJECT_TYPE_HOSTIF_TRAP_GROUP))
            {
                return SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            tgidx = BRCM_SAI_GET_OBJ_VAL(int, trap_group);
            if (_BRCM_SAI_MAX_TRAP_GROUPS < tgidx)
            {
                return SAI_STATUS_INVALID_OBJECT_ID;
            }
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                            &tgidx, &idata_tg);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
            if (tgidx && FALSE == idata_tg.trap_group.valid)
            {
                return SAI_STATUS_INVALID_OBJECT_ID;
            }
            break;
        }
        default:
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unimplemented attribute passed\n");
            return SAI_STATUS_INVALID_PARAMETER;
    }
    if (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == idata_t.trap.trap_id ||
        SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE == idata_t.trap.trap_id)
    {
        arp_type = idata_t.trap.trap_id;
    }

    if (-1 != priority)
    {
        if (TRUE == idata_t.trap.installed)
        {
            for (e=0; e<idata_t.trap.count; e++)
            {
                rv = bcm_field_entry_prio_set(0, idata_t.trap.entry[e], priority);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "entry priority set", rv);
            }
            if (-1 != arp_type)
            {
                /* Update arp entry priorities */
                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_PRIORITY_SET,
                                                       -1, priority, -1, -1, NULL);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap priority update", rv);
            }
        }
    }
    else if (SAI_NULL_OBJECT_ID != trap_group)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                        &tgidx, &idata_tg);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
        new_queue = idata_tg.trap_group.qnum;
        new_state = idata_tg.trap_group.state;
        if (SAI_NULL_OBJECT_ID != idata_tg.trap_group.policer_id)
        {
            new_policer_id = idata_tg.trap_group.policer_id;
            new_policer = BRCM_SAI_GET_OBJ_VAL(int, new_policer_id);
        }
    }

    /* Load previous tg state */
    tgidx = BRCM_SAI_GET_OBJ_VAL(int, idata_t.trap.trap_group);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP, &tgidx, &idata_tg);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
    queue = idata_tg.trap_group.qnum;
    state = idata_tg.trap_group.state;
    if (SAI_NULL_OBJECT_ID != idata_tg.trap_group.policer_id)
    {
        policer_id = idata_tg.trap_group.policer_id;
        policer = BRCM_SAI_GET_OBJ_VAL(int, policer_id);
    }

    /* Process updates */
    for (e=0; e<idata_t.trap.count; e++)
    {
        entry[e] = idata_t.trap.entry[e];
    }
    if (-1 != pkt_action)
    {
        /* Only want to update the PA if the Hostif
         * Table entry has been created. Until that
         * time, the PA should be DROP.
         */
        if ((pkt_action != idata_t.trap.pkta) &&
            (TRUE == idata_t.trap.installed))
        {
            /* 1st, remove old actions */
            if (SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta)
            {
                if (-1 != policer)
                {
                    act = BRCM_SAI_GET_OBJ_SUB_TYPE(policer_id);
                    if (act)
                    {
                        rv = _brcm_sai_policer_actions_get(policer_id,
                                                           &gpa, &ypa, &rpa);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                    }
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = _brcm_sai_entry_policer_actions_remove(entry[e],
                                                                    act, gpa,
                                                                    ypa, rpa);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions delete", rv);
                        rv = bcm_field_entry_reinstall(0, entry[e]);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                    }
                    if (-1 != arp_type)
                    {
                        _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_REMOVE,
                                                               -1, -1, -1, -1, &ppa);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap pol action remove", rv);
                    }
                }
                else
                {
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = bcm_field_action_remove(0, entry[e], bcmFieldActionCopyToCpu);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "remove copy to cpu", rv);
                        rv = bcm_field_entry_reinstall(0, entry[e]);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                    }
                    if (-1 != arp_type)
                    {
                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_COPY_TO_CPU_REMOVE,
                                                               -1, -1, -1, -1, NULL);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap remove copy to cpu", rv);
                    }
                }
                if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR == idata_t.trap.trap_id)
                {
                    rv =  bcm_switch_control_set(0, bcmSwitchL3MtuFailToCpu, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "setting l3 mtu fail to cpu", rv);
                }
            }
            if (!act)
            {
                for (e=0; e<idata_t.trap.count; e++)
                {
                    rv = bcm_field_action_remove(0, entry[e], bcmFieldActionDrop);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                    rv = bcm_field_entry_reinstall(0, entry[e]);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                }
                if (-1 != arp_type)
                {
                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_REMOVE,
                                                           -1, -1, -1, -1, NULL);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap drop remove", rv);
                }
            }
            /* Now, apply new actions */
            if (SAI_PACKET_ACTION_TRAP == pkt_action)
            {
                if (-1 != new_policer)
                {
                    act = BRCM_SAI_GET_OBJ_SUB_TYPE(new_policer_id);
                    if (act)
                    {
                        rv = _brcm_sai_policer_actions_get(new_policer_id,
                                                           &gpa, &ypa, &rpa);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                    }
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = _brcm_sai_entry_policer_actions_add(entry[e], act, gpa,
                                                                 ypa, rpa);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions add", rv);
                        rv = bcm_field_entry_reinstall(0, entry[e]);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                    }
                    if (-1 != arp_type)
                    {
                        _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_ADD,
                                                               -1, -1, -1, -1, &ppa);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap pol action remove", rv);
                    }
                }
                else
                {
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = bcm_field_action_add(0, entry[e], bcmFieldActionCopyToCpu, 0, 0);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "add copy to cpu", rv);
                        rv = bcm_field_entry_reinstall(0, entry[e]);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                    }
                    if (-1 != arp_type)
                    {
                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_COPY_TO_CPU_ADD,
                                                               -1, -1, -1, -1, NULL);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap copy to cpu add", rv);
                    }
                }
                if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR == idata_t.trap.trap_id)
                {
                    rv =  bcm_switch_control_set(0, bcmSwitchL3MtuFailToCpu, 1);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "setting l3 mtu fail to cpu", rv);
                }
            }
            if (!act)
            {
                for (e=0; e<idata_t.trap.count; e++)
                {
                    rv = bcm_field_action_add(0, entry[e], bcmFieldActionDrop, 0, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                    rv = bcm_field_entry_reinstall(0, entry[e]);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                }
                if (-1 != arp_type)
                {
                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_ADD,
                                                           -1, -1, -1, -1, NULL);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap drop add", rv);
                }
            }
        }
    }
    else if (SAI_NULL_OBJECT_ID != trap_group)
    {
        if (trap_group != idata_t.trap.trap_group)
        {
            /* update group entry */
            if (queue != new_queue)
            {
                for (e=0; e<idata_t.trap.count; e++)
                {
                    rv = bcm_field_action_remove(0, entry[e], bcmFieldActionCosQCpuNew);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                    rv = bcm_field_entry_reinstall(0, entry[e]);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                    rv = bcm_field_action_add(0, entry[e], bcmFieldActionCosQCpuNew, new_queue, 0);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                    rv = bcm_field_entry_reinstall(0, entry[e]);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                }
                if (-1 != arp_type)
                {
                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_QUEUE_SET,
                                                           -1, -1, new_queue, -1, NULL);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                }
            }
            /* If a valid policer is being applied */
            if (-1 != new_policer)
            {
                /* If new and old are not the same */
                if (new_policer != policer)
                {
                    /* If old is not null */
                    if (-1 != policer)
                    {
                        for (e=0; e<idata_t.trap.count; e++)
                        {
                            rv = bcm_field_entry_policer_detach(0, entry[e], 0);
                            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer detach", rv);
                            rv = bcm_field_entry_reinstall(0, entry[e]);
                            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                        }
                        if (-1 != arp_type)
                        {
                            rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_DETACH,
                                                                   -1, -1, -1, -1, NULL);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                        }
                        if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                            (TRUE == idata_t.trap.installed))
                        {
                            /* Check and remove old policer actions */
                            act = BRCM_SAI_GET_OBJ_SUB_TYPE(policer_id);
                            if (act)
                            {
                                rv = _brcm_sai_policer_actions_get(policer_id,
                                                                   &gpa, &ypa, &rpa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                            }
                            for (e=0; e<idata_t.trap.count; e++)
                            {
                                rv = _brcm_sai_entry_policer_actions_remove(entry[e], act, gpa, ypa, rpa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer remove", rv);
                                rv = bcm_field_entry_reinstall(0, entry[e]);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                                if (!act)
                                {
                                    rv = bcm_field_action_remove(0, entry[e], bcmFieldActionDrop);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                                    rv = bcm_field_entry_reinstall(0, entry[e]);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                                }
                            }
                            if (-1 != arp_type)
                            {
                                _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_REMOVE,
                                                                       -1, -1, -1, -1, &ppa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap pol action remove", rv);
                                if (!act)
                                {
                                     rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_REMOVE,
                                                                            -1, -1, -1, -1, NULL);
                                     BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap drop remove", rv);
                                }
                            }
                        }
                        if (act)
                        {
                            /* Detach from old policer */
                            _brcm_sai_policer_action_ref_detach(hostif_trap_id);
                        }
                    }
                    else /* Handle old null policer */
                    {
                        if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                            (TRUE == idata_t.trap.installed))
                        {
                            for (e=0; e<idata_t.trap.count; e++)
                            {
                                rv = bcm_field_action_remove(0, entry[e], bcmFieldActionCopyToCpu);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                                rv = bcm_field_action_remove(0, entry[e], bcmFieldActionDrop);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                                rv = bcm_field_entry_reinstall(0, entry[e]);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                            }
                            if (-1 != arp_type)
                            {
                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_TRAP_REMOVE,
                                                                       -1, -1, -1, -1, NULL);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap remove", rv);
                            }
                        }
                    }
                    if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                        (TRUE == idata_t.trap.installed))
                    {
                        /* Check and apply new policer actions */
                        act = BRCM_SAI_GET_OBJ_SUB_TYPE(new_policer_id);
                        if (act)
                        {
                            rv = _brcm_sai_policer_actions_get(new_policer_id,
                                                               &gpa, &ypa, &rpa);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                        }
                        for (e=0; e<idata_t.trap.count; e++)
                        {
                            rv = _brcm_sai_entry_policer_actions_add(entry[e], act, gpa, ypa, rpa);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions add", rv);
                            if (!act)
                            {
                                rv = bcm_field_action_add(0, entry[e], bcmFieldActionDrop, 0, 0);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                            }
                            change = TRUE;
                        }
                        if (-1 != arp_type)
                        {
                            _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                            rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_ADD_ONLY,
                                                                   -1, -1, -1, -1, &ppa);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap pol action remove", rv);
                            if (!act)
                            {
                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_ADD_ONLY,
                                                                       -1, -1, -1, -1, NULL);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap drop", rv);
                            }
                        }
                    }
                    if (act)
                    {
                        /* Attach with new policer */
                        rv = _brcm_sai_policer_action_ref_attach(new_policer_id,
                                                                 hostif_trap_id);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer action ref attach", rv);
                    }
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = bcm_field_entry_policer_attach(0, entry[e], 0,
                                                            BRCM_SAI_GET_OBJ_VAL(int, new_policer_id));
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                        change = TRUE;
                    }
                    if (-1 != arp_type)
                    {
                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POLICER_ATTACH,
                                                               -1, -1, -1,
                                                               BRCM_SAI_GET_OBJ_VAL(int, new_policer_id),
                                                               NULL);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap drop", rv);
                    }
                }
            }
            else /* A null policer is applied - remove previous policer */
            {
                /* Did an older policer exist */
                if (-1 != policer)
                {
                    if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                        (TRUE == idata_t.trap.installed))
                    {
                        act = BRCM_SAI_GET_OBJ_SUB_TYPE(policer_id);
                        if (act)
                        {
                            rv = _brcm_sai_policer_actions_get(policer_id,
                                                               &gpa, &ypa, &rpa);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                        }
                    }
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = bcm_field_entry_policer_detach(0, entry[e], 0);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer detach", rv);
                        rv = bcm_field_entry_reinstall(0, entry[e]);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                        if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                            (TRUE == idata_t.trap.installed))
                        {
                            rv = _brcm_sai_entry_policer_actions_remove(entry[e], act, gpa, ypa, rpa);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer remove", rv);
                            rv = bcm_field_entry_reinstall(0, entry[e]);
                            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                            if (act) /* Only if old policer was color aware */
                            {
                                rv = bcm_field_action_add(0, entry[e], bcmFieldActionDrop, 0, 0);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                            }
                            rv = bcm_field_action_add(0, entry[e], bcmFieldActionCopyToCpu,
                                                      0, 0);
                            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                            change = TRUE;
                        }
                    }
                    if (-1 != arp_type)
                    {
                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_DETACH,
                                                               -1, -1, -1, -1, NULL);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                        if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                            (TRUE == idata_t.trap.installed))
                        {
                            _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                            rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_REMOVE,
                                                                   -1, -1, -1, -1, &ppa);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                            if (act) /* Only if old policer was color aware */
                            {
                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_ADD_ONLY,
                                                                       -1, -1, -1, -1, NULL);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                            }
                            rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_COPY_TO_CPU_ADD_ONLY,
                                                                   -1, -1, -1, -1, NULL);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap copy to cpu", rv);
                        }
                    }
                    if (act)
                    {
                        /* Detach from old policer */
                        _brcm_sai_policer_action_ref_detach(hostif_trap_id);
                    }
                }
            }
            if (state != new_state)
            {
                for (e=0; e<idata_t.trap.count; e++)
                {
                    rv = _brcm_entry_enable(entry[e], new_state);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable", rv);
                }
                if (-1 != arp_type)
                {
                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_ENABLE,
                                                           new_state, -1, -1, -1, NULL);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                }
                change = TRUE;
            }
            /* decrement ref count for the old trap group */
            tgidx = BRCM_SAI_GET_OBJ_VAL(int, idata_t.trap.trap_group);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP, &tgidx, &idata_tg);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
            rv = _brcm_sai_trap_group_delete_trap_ref(tgidx, idx);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "group trap ref delete", rv);
            tgidx = BRCM_SAI_GET_OBJ_VAL(int, trap_group);
            /* increment ref count for the new trap group */
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP, &tgidx, &idata_tg);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
            rv = _brcm_sai_trap_group_add_trap_ref(tgidx, idx);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "group trap ref add", rv);
        }
        if (change)
        {
            for (e=0; e<idata_t.trap.count; e++)
            {
                rv = bcm_field_entry_reinstall(0, entry[e]);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
            }
            if (-1 != arp_type)
            {
                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_REINSTALL,
                                                       -1, -1, -1, -1, NULL);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap reinstall entries", rv);
            }
        }
    }
    else if (TRUE == exclude_list)
    {
        for (e=0; e<idata_t.trap.count; e++)
        {
            entry[e] = idata_t.trap.entry[e];
            rv = bcm_field_qualify_InPorts(0, entry[e], pbmp, mpbmp);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
            rv = bcm_field_entry_reinstall(0, entry[e]);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
        }
        if (-1 != arp_type)
        {
            rv = _brcm_sai_arp_trap_entries_update_inports(arp_type, pbmp, mpbmp);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap reinstall entries", rv);
        }
    }

    if (SAI_NULL_OBJECT_ID != trap_group)
    {
        idata_t.trap.trap_group = trap_group;
    }
    else if (-1 != pkt_action)
    {
        idata_t.trap.pkta = pkt_action;
    }
    else if (-1 != priority)
    {
        idata_t.trap.priority = priority;
    }

    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP, &idx, &idata_t);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap set", rv);
    if (new_entry)
    {
        if (SAI_STATUS_SUCCESS !=
            _brcm_sai_global_data_bump(_BRCM_SAI_TRAPS_COUNT, INC))
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                               "Error incrementing traps count "
                               "global data.\n");
            return SAI_STATUS_FAILURE;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
* Routine Description:
*   Get trap attribute value.
*
* Arguments:
*    [in] hostif_trap_id - host interface trap id
*    [in] attr_count - number of attributes
*    [in,out] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_hostif_trap_attribute(_In_ sai_object_id_t hostif_trap_id,
                                   _In_ uint32_t attr_count,
                                   _Inout_ sai_attribute_t *attr_list)
{
    int i, idx;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(hostif_trap_id, SAI_OBJECT_TYPE_HOSTIF_TRAP);
    idx = BRCM_SAI_GET_OBJ_VAL(int, hostif_trap_id);
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                       "Get host intf trap: %d\n", idx);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP,
                                    &idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif trap data get", rv);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_HOSTIF_TRAP_ATTR_TRAP_TYPE:
                attr_list[i].value.s32 = data.trap.trap_id;
                break;
            case SAI_HOSTIF_TRAP_ATTR_PACKET_ACTION:
                attr_list[i].value.s32 = data.trap.pkta;
                break;
            case SAI_HOSTIF_TRAP_ATTR_TRAP_PRIORITY:
                attr_list[i].value.u32 = data.trap.priority;
                break;
            case SAI_HOSTIF_TRAP_ATTR_EXCLUDE_PORT_LIST:
            {
                int c = 0, p, limit, count;

                BCM_PBMP_COUNT(data.trap.exclude_list, count);
                limit = count;
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < limit)
                {
                    limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i);
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                if (limit)
                {
                    PBMP_ITER(data.trap.exclude_list, p)
                    {
                        BRCM_SAI_ATTR_LIST_OBJ_LIST(i, c) =
                            BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, p);
                        c++;
                        if (c >= limit)
                        {
                            break;
                        }
                    }
                }
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = count;
                break;
            }
            case SAI_HOSTIF_TRAP_ATTR_TRAP_GROUP:
                BRCM_SAI_ATTR_LIST_OBJ(i) = data.trap.trap_group;
                break;
            default:
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "Unknown hostif trap attribute %d passed\n",
                                   attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_INFO,
                               "Error processing hostif trap attributes\n");
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
################################################################################
#                            Internal KNET functions                           #
################################################################################
*/

/*
* Routine Description:
*   Internal function to create hostif
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*
* Notes :- Does not check if the knet interface already exists for that port.
*          Param 'lag' is not used for now.
*
*/
STATIC sai_status_t
_brcm_sai_netif_create(bcm_knet_netif_t *netif, _brcm_sai_netif_info_t net_info)
{
    int unit = 0;
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    bool vlan_filter = FALSE;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);

    /* KEEP in KNET means do-not-touch. We will implement strip via KNET-CB */
    netif->flags |= BCM_KNET_NETIF_F_KEEP_RX_TAG;
    rv = bcm_knet_netif_create(unit, netif);
    if (!BCM_SUCCESS(rv))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Failed to create iface for %s %d. Error %s\n",
                           net_info.vlan_rif ? "vlan" : "port",
                           net_info.vlan_rif ? netif->vlan : netif->port,
                           bcm_errmsg(rv));
        return BRCM_RV_BCM_TO_SAI(rv);
    }
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Created netif:%d\n", netif->id);
    /* Save netif info */
    data.hostif_info.idx = netif->id;
    memcpy(&data.hostif_info.info, &net_info, sizeof(_brcm_sai_netif_info_t));
    memcpy(&data.hostif_info.netif, netif, sizeof(_brcm_sai_hostif_t));
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_INFO, &netif->id,
                                    &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif info set", rv);
    if ((TRUE == net_info.vlan_rif) && (FALSE == net_info.lag))
    {
        vlan_filter = TRUE;
    }
    rv = _brcm_sai_netfilter_create(netif, net_info.tag_mode, vlan_filter);
    if (!BCM_SUCCESS(rv))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Failed to set rx filter for port %d. "
                           "Error %s(0x%x)\n",
                           netif->port, bcm_errmsg(rv), rv);

        /* Delete the netif here */
        (void)bcm_knet_netif_destroy(unit, netif->id);
        return BRCM_RV_BCM_TO_SAI(rv);
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
* Routine Description:
*   Internal function to create KNET filters
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*
*/
STATIC sai_status_t
_brcm_sai_netfilter_create(bcm_knet_netif_t *netif, sai_hostif_vlan_tag_t tag_mode, bool vlan_filter)
{
    int idx[2], unit = 0;
    sai_status_t rv;
    bcm_knet_filter_t filter;
    _brcm_sai_indexed_data_t data;
    bcm_vlan_t vlan = netif->vlan;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    /* For each hostif, for Rx, we need to set filter to get packets from CPU &
       only from that ingress port */
    bcm_knet_filter_t_init(&filter);
    filter.type = BCM_KNET_FILTER_T_RX_PKT;
    filter.dest_type = BCM_KNET_DEST_T_NETIF;
    filter.dest_id = netif->id;
    filter.match_flags = (BCM_KNET_FILTER_M_INGPORT);

    /* Indicate via filter cb data that you want to strip/keep */
    filter.cb_user_data = tag_mode;

    if (BCM_KNET_NETIF_T_TX_LOCAL_PORT == netif->type)
    {
        _brcm_sai_netif_map_t *netif_map_port;

        filter.m_ingport = netif->port;
        rv = bcm_knet_filter_create(unit, &filter);
        if (!BCM_SUCCESS(rv))
        {
            return rv;
        }
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Created filter %d for port: %d\n",
                           filter.id, filter.m_ingport);
        if (netif->port < _BRCM_SAI_MAX_PORTS)
        {
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_PORT_MAP,
                                            &netif->port, &data);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif port map get", rv);
            netif_map_port = data.netif_map;
            netif_map_port->idx = netif->port;
            netif_map_port->netif_id = netif->id;
            netif_map_port->filter_id[netif_map_port->num_filters] = filter.id;

        }
        else
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Out of range port: %d\n",
                               netif->port);
            return SAI_STATUS_INVALID_PARAMETER;
        }
        /* save filter info */
        idx[0] = netif->id;
        idx[1] = netif_map_port->num_filters;
        data.hostif_filter.idx1 = netif->id;
        data.hostif_filter.idx2 = netif_map_port->num_filters;
        data.hostif_filter.type = filter.type;
        data.hostif_filter.dest_type = filter.dest_type;
        data.hostif_filter.flags = filter.flags;
        data.hostif_filter.match_flags = filter.match_flags;
        data.hostif_filter.m_ingport = filter.m_ingport;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx,
                                        &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif filter set", rv);
        netif_map_port->num_filters++;
        if (netif_map_port->num_filters > _BRCM_SAI_MAX_FILTERS_PER_INTF)
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                               "Max filters exceeded for port intf: %d\n",
                               netif->port);
            return SAI_STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else /* Rif and LAG */
    {
        int i = vlan;
        pbmp_t pbm;
        pbmp_t upbm;
        _brcm_sai_netif_map_t *netif_map_vlan;

        rv  = bcm_vlan_port_get(0, (bcm_vlan_t)vlan, &pbm, &upbm);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "Get ports per vlan", rv);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_VLAN_MAP,
                                        &i, &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif vlan map get", rv);
        netif_map_vlan = data.netif_map;
        netif_map_vlan->idx = vlan;
        netif_map_vlan->netif_id = netif->id;
        if (TRUE == vlan_filter)
        {
        /* FIXME: Not sure this can be fixed. The following code was added
         * to support LAGs with overlapping VLANs. But this fix seems to break
         * basic functionality.
         */
        #if 0
            filter.match_flags |= BCM_KNET_FILTER_M_VLAN;
            filter.m_vlan = vlan;
        #endif
        }
        /* Skip cpu port */
        BCM_PBMP_PORT_REMOVE(pbm, 0);
        BCM_PBMP_ITER(pbm, i)
        {
            filter.m_ingport = i;
            rv = bcm_knet_filter_create(unit, &filter);
            if (!BCM_SUCCESS(rv))
            {
                return rv;
            }
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                               "Created filter %d for vid-port: %d-%d\n",
                               filter.id, vlan, filter.m_ingport);
            if (i < _BRCM_SAI_MAX_PORTS)
            {
                netif_map_vlan->filter_id[netif_map_vlan->num_filters] =
                    filter.id;
            }
            else
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Out of range port: %d\n", i);
                return SAI_STATUS_INVALID_PARAMETER;
            }
            /* save filter info */
            idx[0] = netif->id;
            idx[1] = netif_map_vlan->num_filters;
            data.hostif_filter.idx1 = netif->id;
            data.hostif_filter.idx2 = netif_map_vlan->num_filters;
            data.hostif_filter.type = filter.type;
            data.hostif_filter.dest_type = filter.dest_type;
            data.hostif_filter.flags = filter.flags;
            data.hostif_filter.match_flags = filter.match_flags;
            data.hostif_filter.m_ingport = filter.m_ingport;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx,
                                            &data);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif filter set", rv);
            netif_map_vlan->num_filters++;
            if (netif_map_vlan->num_filters > _BRCM_SAI_MAX_FILTERS_PER_INTF)
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "Max filters exceeded for vlan: %d\n",
                                   vlan);
                return SAI_STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_hostif_recreate(int icount, bool filter_only)
{
    bool type_vlan;
    sai_status_t rv;
    int i, f, idx[2], unit = 0;
    _brcm_sai_netif_map_t *map;
    bcm_knet_netif_t *netif;
    bcm_knet_filter_t filter;
    _brcm_sai_hostif_filter_t *_filter;
    _brcm_sai_indexed_data_t data, _data;
    char buf[128];
    int nbuf;
    FILE *fp;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    for (i=1; i<=icount; i++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_INFO, &i,
                                        &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif info get", rv);
        netif = &data.hostif_info.netif;
        type_vlan = (netif->type == BCM_KNET_NETIF_T_TX_CPU_INGRESS);
        if (FALSE == filter_only)
        {
            rv = bcm_knet_netif_create(unit, netif);
            if (!BCM_SUCCESS(rv))
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "WB: Failed to create iface for %s %d. Error %s\n",
                                   type_vlan ? "vlan" : "port",
                                   type_vlan ? netif->vlan : netif->port,
                                   bcm_errmsg(rv));
                return BRCM_RV_BCM_TO_SAI(rv);
            }
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Re-created netif:%d\n", netif->id);
        }
        if (type_vlan)
        {
            /* Get this netif's filter count and info from the vlan map */
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_VLAN_MAP,
                                            (int*)&netif->vlan, &_data);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif vlan map get", rv);
            map = _data.netif_map;
            if (!map->num_filters)
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Host vlan intf with no port members.\n");
                continue;
            }
            if (map->netif_id == netif->id)
            {
                idx[0] = i;
                for (f = 0; f < map->num_filters; f++)
                {
                    idx[1] = f;
                    rv = _brcm_sai_indexed_data_get
                            (_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx, &_data);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                                    "hostif vlan filter data get", rv);
                    _filter = &_data.hostif_filter;
                    bcm_knet_filter_t_init(&filter);
                    filter.type = _filter->type;
                    filter.dest_type = _filter->dest_type;
                    filter.dest_id = netif->id;
                    filter.match_flags = _filter->match_flags;
                    filter.flags = _filter->flags;
                    filter.m_ingport = _filter->m_ingport;
                    filter.cb_user_data = data.hostif_info.info.tag_mode;
                    rv = bcm_knet_filter_create(unit, &filter);
                    if (!BCM_SUCCESS(rv))
                    {
                        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                           "Failed to set rx filter for port %d."
                                           " Error %s(0x%x)\n",
                                           netif->port, bcm_errmsg(rv), rv);

                        /* Delete the netif here */
                        rv = bcm_knet_netif_destroy(unit, netif->id);
                        return BRCM_RV_BCM_TO_SAI(rv);
                    }
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                                       "Re-created filter %d for vid-port: %d-%d\n",
                                       filter.id, netif->vlan, filter.m_ingport);
                    /* clear old filter info */
                    _data.hostif_filter.idx1 = 0;
                    _data.hostif_filter.idx2 = 0;
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx,
                                                    &_data);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif filter set", rv);
                    /* Setup new filter info */
                    idx[1] = f;
                    _data.hostif_filter.idx1 = i;
                    _data.hostif_filter.idx2 = f;
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx,
                                                    &_data);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif filter set", rv);
                    /* Update filter id in the map */
                    map->filter_id[f] = filter.id;
                }
            }
            else
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "Mismatch in DB for vlan netif detected.\n");
                return SAI_STATUS_FAILURE;
            }
        }
        else
        {
            /* Get this netif's filter info from the port map */
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_PORT_MAP,
                                            &netif->port, &_data);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif port map get", rv);
            map = _data.netif_map;
            if (1 == map->num_filters && map->netif_id == netif->id)
            {
                idx[0] = i; idx[1] = 0;
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_FILTERS,
                                                idx, &_data);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                                "hostif port filter data get", rv);
                _filter = &_data.hostif_filter;
                bcm_knet_filter_t_init(&filter);
                filter.type = _filter->type;
                filter.dest_type = _filter->dest_type;
                filter.dest_id = netif->id;
                filter.match_flags = _filter->match_flags;
                filter.flags = _filter->flags;
                filter.m_ingport = _filter->m_ingport;
                filter.cb_user_data = data.hostif_info.info.tag_mode;
                rv = bcm_knet_filter_create(unit, &filter);
                if (!BCM_SUCCESS(rv))
                {
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                       "Failed to set rx filter for port %d. "
                                       "Error %s(0x%x)\n",
                                       netif->port, bcm_errmsg(rv), rv);

                    /* Delete the netif here */
                    (void)bcm_knet_netif_destroy(unit, netif->id);
                    return BRCM_RV_BCM_TO_SAI(rv);
                }
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                                   "Re-created filter %d for port: %d\n",
                                   filter.id, filter.m_ingport);
                /* clear old filter info */
                _data.hostif_filter.idx1 = 0;
                _data.hostif_filter.idx2 = 0;
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx,
                                                &_data);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif filter set", rv);
                /* Setup new filter info */
                idx[1] = 0;
                _data.hostif_filter.idx1 = i;
                _data.hostif_filter.idx2 = 0;
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_FILTERS, idx,
                                                &_data);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif filter set", rv);
                /* Update filter id in the map */
                map->filter_id[0] = filter.id;
            }
            else
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "Mismatch in DB for port netif detected.\n");
                return SAI_STATUS_FAILURE;
            }
        }
        /* restore netif status */
        fp = fopen(LINK_CONTROL_STATE, "w");
        nbuf = snprintf(buf, 128, "%s=%s", netif->name,
                       (TRUE == data.hostif_info.info.status) ? "up" : "down");
        fwrite(buf, nbuf, 1, fp);
        fclose(fp);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_netif_destroy(sai_object_id_t hif_id)
{
    sai_status_t rv;
    int netif_id, unit = 0;
    _brcm_sai_netif_map_t *map;
    _brcm_sai_indexed_data_t data, _data;
    uint8_t id = BRCM_SAI_GET_OBJ_VAL(uint8_t, hif_id);
    int h = BRCM_SAI_GET_OBJ_MAP(hif_id);

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    if (BRCM_SAI_GET_OBJ_SUB_TYPE(hif_id))
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_VLAN_MAP,
                                        &h, &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif vlan map get", rv);
        map = data.netif_map;
    }
    else
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_PORT_MAP,
                                        &h, &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif port map get", rv);
        map = data.netif_map;
    }
    if (id != map->netif_id)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Interface id mismatch: map[%d], %d != %d\n", h, id,
                           map->netif_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    else
    {
        rv = bcm_knet_netif_destroy(unit, map->netif_id);
        if (!BCM_SUCCESS(rv))
        {
              BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                 "Error 0x%x destroying netif %d\n",rv,
                                 map->netif_id);
        }
    }
    rv = _brcm_sai_netfilter_destroy(hif_id, &netif_id);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "netfilter destroy", rv);
    memset(map, 0, sizeof(_brcm_sai_netif_map_t));
    /* remove netif info as well */
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_INFO, &netif_id,
                                    &_data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif info get", rv);
    _data.hostif_info.idx = 0;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_INFO,
                                    &netif_id, &_data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif info set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_netfilter_destroy(sai_object_id_t hif_id, int *netif_id)
{
    int idx[2], unit = 0;
    sai_status_t rv;
    _brcm_sai_netif_map_t *map;
    _brcm_sai_indexed_data_t data, _data;
    uint8_t id = BRCM_SAI_GET_OBJ_VAL(uint8_t, hif_id);
    int h = BRCM_SAI_GET_OBJ_MAP(hif_id);

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    if (BRCM_SAI_GET_OBJ_SUB_TYPE(hif_id))
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_VLAN_MAP,
                                        &h, &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif vlan map get", rv);
        map = data.netif_map;
    }
    else
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_NETIF_PORT_MAP,
                                        &h, &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "hostif port map get", rv);
        map = data.netif_map;
    }
    if (id != map->netif_id)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Interface id mismatch: map[%d], %d != %d\n", h, id,
                           map->netif_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    *netif_id = idx[0] = map->netif_id;
    while(map->num_filters-- > 0)
    {
        rv = bcm_knet_filter_destroy(unit, map->filter_id[map->num_filters]);
        if (!BCM_SUCCESS(rv))
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                               "Error 0x%x destroying filter id %d\n",rv,
                               map->filter_id[map->num_filters]);
        }
        /* remove filter info as well */
        idx[1] = map->num_filters;
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_HOSTIF_FILTERS,
                                        idx, &_data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                        "hostif vlan filter data get", rv);
        _data.hostif_filter.idx1 = 0;
        _data.hostif_filter.idx2 = 0;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_HOSTIF_FILTERS,
                                        idx, &_data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                        "hostif vlan filter data set", rv);
    }
    map->num_filters = 0;

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

STATIC int
_brcm_sai_hostif_netif_clean(int unit, bcm_knet_netif_t *netif, void *user_data)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;

    if ((bool)user_data)
    {
        rv = bcm_knet_netif_destroy(unit, netif->id);
        if (BCM_SUCCESS(rv))
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Deleted netif %d\n",
                               netif->id);
        }
        else
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Error 0x%x destroying netif %d\n",
                               rv, netif->id);
        }
    }
    return rv;
}

STATIC sai_status_t
_brcm_sai_hostif_netif_assign_mac(sai_object_id_t rif_obj, bcm_mac_t *mac)
{
    bcm_l3_intf_t l3_intf;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    if (BRCM_SAI_CHK_OBJ_MISMATCH(rif_obj, SAI_OBJECT_TYPE_ROUTER_INTERFACE))
    {
        /* If mismatch, then must not be a routing interface.
         * Copy system mac address to host if mac.
         */
        rv = _brcm_sai_system_mac_get(mac);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "retrieving system mac", rv);
    }
    else
    {
        bcm_l3_intf_t_init(&l3_intf);
        l3_intf.l3a_intf_id =  BRCM_SAI_GET_OBJ_VAL(bcm_if_t, rif_obj);
        rv = bcm_l3_intf_get(0, &l3_intf);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "L3 interface get", rv);
        /* Copy router intf mac address to host if mac */
        memcpy(mac, l3_intf.l3a_mac_addr, sizeof(sai_mac_t));
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

STATIC int
_brcm_sai_hostif_filter_clean(int unit, bcm_knet_filter_t *filter,
                             void *user_data)
{
    sai_status_t rv = 0;

    if ((bool)user_data)
    {
        rv = bcm_knet_filter_destroy(unit, filter->id);
        if (BCM_SUCCESS(rv))
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Deleted filter %d\n",
                               filter->id);
        }
        else
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Error 0x%x destroying filter %d\n",
                               rv, filter->id);
        }
    }
    return rv;
}

STATIC int
_brcm_sai_hostif_netif_count(int unit, bcm_knet_netif_t *netif, void *user_data)
{
    if ((bool)user_data)
    {
        _host_intf_count++;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_hostif_clean()
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    rv = bcm_knet_netif_traverse(0, _brcm_sai_hostif_netif_clean, (void *)TRUE);
    if (!BCM_SUCCESS(rv))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Error 0x%x traversing netifs.\n", rv);
        return BRCM_RV_BCM_TO_SAI(rv);
    }
    rv = bcm_knet_filter_traverse(0, _brcm_sai_hostif_filter_clean, (void *)TRUE);
    if (!BCM_SUCCESS(rv))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Error 0x%x traversing filters.\n", rv);
        return BRCM_RV_BCM_TO_SAI(rv);
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_create_trap_resource()
{
    sai_status_t rv;
    _brcm_sai_data_t data;
    bcm_field_qset_t qset;
    bcm_field_range_t range;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    BCM_FIELD_QSET_INIT(qset);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyStageIngress);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyEtherType);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyInPorts);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyTtl);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstMac);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyEtherType);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstIp6);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIpProtocol);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyL4SrcPort);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyL4DstPort);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIpType);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIp6NextHeader);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIp6HopLimit);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstClassL3);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyRangeCheck);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyPacketRes);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstIp);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyIpInfo);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyDstL3Egress);
    BCM_FIELD_QSET_ADD(qset, bcmFieldQualifyL3DestRouteHit);

    rv = bcm_field_group_create(0, qset, BCM_FIELD_GROUP_PRIO_ANY,
                                &_global_trap_group);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field group create", rv);
    data.u32 = _global_trap_group;
    rv = _brcm_sai_global_data_set(_BRCM_SAI_TRAP_FP_GROUP, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                            "setting global fp group", rv);
    /*
     * Create global range ID for use by Neighbor Discovery.
     *
     * The following types of Neighbor Discovery packets
     * are trapped:
     *
     * Router Solicitation    - Type Code = 133 (0x85)
     * Router Advertisement   - Type Code = 134 (0x86)
     * Neighbor Solicitation  - Type Code = 135 (0x87)
     * Neighbor Advertisement - Type Code = 136 (0x88)
     * Redirect               - Type Code = 137 (0x89)
     *
     * Need to shift the TC left by 8 to calculate the range.
     * The TC "appears" in the upper 8 bits of the L4SrcPort
     * field.
     */
    if (DEV_IS_TH3())
    {
        bcm_port_config_t port_config;
        bcm_range_config_t range_config;

        rv = bcm_port_config_get(0, &port_config);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "port config get", rv);
        bcm_range_config_t_init(&range_config);
        range_config.rtype = bcmRangeTypeL4SrcPort;
        range_config.offset = 0;
        range_config.width = 16;
        range_config.min = 34048;
        range_config.max = 35072;
        range_config.ports = port_config.all;
        rv = bcm_range_create(0, 0, &range_config);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "range create", rv);
        range = range_config.rid;
    }
    else
    {
        uint32 flags = BCM_FIELD_RANGE_SRCPORT;

        rv = bcm_field_range_create(0, &range, flags, 34048, 35072);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field range create", rv);
    }
    data.u32 = range;
    rv = _brcm_sai_global_data_set(_BRCM_SAI_TRAP_RANGE_ID, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                            "setting global range id", rv);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

bcm_field_group_t
_brcm_sai_global_trap_group_get(int unit)
{
    return _global_trap_group;
}

sai_status_t
_brcm_sai_alloc_hostif()
{
    sai_status_t rv;
    int idx, max = 0;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_NETIF_PORT_MAP,
                                     _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing netif port map", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_NETIF_VLAN_MAP,
                                     _BRCM_SAI_VR_MAX_VID+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing netif vlan map", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_HOSTIF_INFO,
                                     _BRCM_SAI_MAX_HOSTIF+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing netif info", rv);
    rv = _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_HOSTIF_FILTERS,
                                      _BRCM_SAI_MAX_HOSTIF+1,
                                      _BRCM_SAI_MAX_FILTERS_PER_INTF+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing netif filters", rv);

    /* If in WB mode then get global knet netif count & if non zero then
     * traverse knet intfs to determine if it was just an app termination
     * (i.e linux intfs may be intact) or a host shutdown (i.e linux intfs need
     * to be re-installed).
     * Then either recreate netifs, filters and over-write maps or only
     * recreate filters and over-write map.
     *
     * In the future, objects may need to be re-created and provided to host
     * adapter.
     */
    if (_brcm_sai_switch_wb_state_get())
    {
        rv = _brcm_sai_global_data_get(_BRCM_SAI_HOST_INTF_COUNT, &data);
        BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                            "getting global host intfs count", rv);
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "WB: %d host intfs count loaded.\n",
                           data.u8);
        if (data.u8) /* Linux intfs were created earlier */
        {
            _host_intf_count = 0;
            rv = bcm_knet_netif_traverse(0, _brcm_sai_hostif_netif_count,
                                         (void *)TRUE);
            if (!BCM_SUCCESS(rv))
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                                   "Error 0x%x traversing netifs.\n",rv);
                return BRCM_RV_BCM_TO_SAI(rv);
            }
            if (_host_intf_count == data.u8) /* All linux intf are still intact */
            /* Only recreate filters */
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                                   "WB: %d host intfs traversed.\n", data.u8);
                /* Remove default filter */
                (void)bcm_knet_filter_destroy(0, 1);
                rv = _brcm_sai_hostif_recreate(data.u8, TRUE);
                BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                                    "recreating host intfs", rv);
            }
            else if (0 == _host_intf_count) /* Recreate all intfs and filters */
            {
                /* Remove default filter */
                (void)bcm_knet_filter_destroy(0, 1);
                rv = _brcm_sai_hostif_recreate(data.u8, FALSE);
                BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                                    "recreating host intfs", rv);
            }
            else /* Currently we do not support reconciliation */
            {
                BRCM_SAI_LOG_HINTF
                    (SAI_LOG_LEVEL_CRITICAL,
                     "WB: Detected netif count descrepency: DB:%d - knet:%d\n",
                     data.u8, _host_intf_count);
                return SAI_STATUS_FAILURE;
            }
        }
    }
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                     _BRCM_SAI_MAX_TRAP_GROUPS+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing trap groups info", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_TRAP,
                                     _BRCM_SAI_MAX_TRAPS+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing traps info", rv);
    rv = _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_HOSTIF_TABLE,
                                     _BRCM_SAI_MAX_HOSTIF_TABLE_ENTRIES+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing hostif trap table info", rv);
    rv = _brcm_sai_l3_config_get(7, &max);
    if (BCM_FAILURE(rv) || (0 == max))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_CRITICAL,
                           "Error %d retreiving max rif !!\n", rv);
        return SAI_STATUS_FAILURE;
    }
    rv = _brcm_sai_db_table_create(_BRCM_SAI_TABLE_UCAST_ARP, max+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "initializing ucast arp table", rv);

    if (_brcm_sai_switch_wb_state_get())
    {
        rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_GROUPS_COUNT, &data);
        BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                            "getting global trap groups count", rv);
        max  = 0;
        if (data.u8)
        {
            max = _BRCM_SAI_MAX_TRAP_GROUPS;
        }
        for (idx = 0; idx <= max; idx++)
        {
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                            &idx, &idata);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
            if (idata.trap_group.state && idata.trap_group.traps &&
                idata.trap_group.ref_count)
            {
                rv = _brcm_sai_list_init(_BRCM_SAI_LIST_TGRP_TRAP_REF, idx+1,
                                         idata.trap_group.ref_count,
                                         (void**)&idata.trap_group.traps);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "list init trap group trap ref", rv);
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                                &idx, &idata);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group set", rv);
            }
        }

        rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_FP_GROUP, &data);
        BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                            "getting global fp group", rv);
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "WB: fp group loaded %d.\n",
                           data.u32);
        _global_trap_group = data.u32; /* Set global default */
    }
    else
    {
        idx = 0;
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                        &idx, &idata);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
        idata.trap_group.valid = TRUE;
        idata.trap_group.state = TRUE;
        rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                        &idx, &idata);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group set", rv);

        return _brcm_sai_create_trap_resource();
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_hostif()
{
    sai_status_t rv;
    int idx, max = 0;
    _brcm_sai_data_t data;
    _brcm_sai_indexed_data_t idata;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_NETIF_PORT_MAP,
                                      1, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hostif port map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_NETIF_VLAN_MAP,
                                      1, _BRCM_SAI_VR_MAX_VID+1, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hostif vlan map", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_HOSTIF_INFO,
                                      1, _BRCM_SAI_MAX_HOSTIF+1, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hostif info", rv);
    rv = _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_HOSTIF_FILTERS,
                                      _BRCM_SAI_MAX_HOSTIF+1,
                                      _BRCM_SAI_MAX_FILTERS_PER_INTF+1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hostif filters", rv);

    rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_GROUPS_COUNT, &data);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "getting global trap groups count", rv);
    if (data.u8)
    {
        max = _BRCM_SAI_MAX_TRAP_GROUPS;
    }
    for (idx = 0; idx <= max; idx++)
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                        &idx, &idata);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
        if (idata.trap_group.state && idata.trap_group.traps)
        {
            rv = _brcm_sai_list_free(_BRCM_SAI_LIST_TGRP_TRAP_REF, idx+1,
                                     idata.trap_group.ref_count,
                                     idata.trap_group.traps);
            BRCM_SAI_RV_CHK(SAI_API_ROUTE, "list free trap group trap ref", rv);
            idata.trap_group.traps = (_brcm_sai_trap_refs_t*)(uint64_t)(idx+1);
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                            &idx, &idata);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
        }
    }

    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                      0, _BRCM_SAI_MAX_TRAP_GROUPS+1, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing trap groups info", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_TRAP,
                                      1, _BRCM_SAI_MAX_TRAPS+1, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing traps info", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_HOSTIF_TABLE,
                                      1, _BRCM_SAI_MAX_HOSTIF_TABLE_ENTRIES+1, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                        "freeing hostif trap table info", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_entry_policer_actions_add(bcm_field_entry_t entry, uint8_t act,
                                    sai_packet_action_t gpa,
                                    sai_packet_action_t ypa,
                                    sai_packet_action_t rpa)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    if (act)
    {
        if (!(act & 0x1))
        {
            act |= 0x1;
            gpa = SAI_PACKET_ACTION_FORWARD;
        }
        switch(gpa)
        {
            case SAI_PACKET_ACTION_DROP:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionGpDrop, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_PACKET_ACTION_FORWARD:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionGpDrop, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionGpCopyToCpu, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            default: break;
        }
        if (!(act & 0x2))
        {
            act |= 0x2;
            ypa = SAI_PACKET_ACTION_FORWARD;
        }
        switch(ypa)
        {
            case SAI_PACKET_ACTION_DROP:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionYpDrop, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_PACKET_ACTION_FORWARD:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionYpDrop, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionYpCopyToCpu, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            default: break;
        }
        if (!(act & 0x4))
        {
            act |= 0x4;
            rpa = SAI_PACKET_ACTION_FORWARD;
        }
        switch(rpa)
        {
            case SAI_PACKET_ACTION_DROP:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionRpDrop, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            case SAI_PACKET_ACTION_FORWARD:
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionRpDrop, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                rv = bcm_field_action_add(0, entry,
                         bcmFieldActionRpCopyToCpu, 0, 0);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action add", rv);
                break;
            default: break;
        }
    }
    else
    {
        rv = bcm_field_action_add(0, entry, bcmFieldActionGpCopyToCpu, 0, 0);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

STATIC sai_status_t
_brcm_sai_entry_policer_actions_remove(bcm_field_entry_t entry, uint8_t act,
                                       sai_packet_action_t gpa,
                                       sai_packet_action_t ypa,
                                       sai_packet_action_t rpa)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    if (act)
    {
        if (!(act & 0x1))
        {
            act |= 0x1;
            gpa = SAI_PACKET_ACTION_FORWARD;
        }
        switch(gpa)
        {
            case SAI_PACKET_ACTION_DROP:
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionGpDrop);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                break;
            case SAI_PACKET_ACTION_FORWARD:
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionGpDrop);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionGpCopyToCpu);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                break;
            default: break;
        }
        if (!(act & 0x2))
        {
            act |= 0x2;
            ypa = SAI_PACKET_ACTION_FORWARD;
        }
        switch(ypa)
        {
            case SAI_PACKET_ACTION_DROP:
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionYpDrop);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                break;
            case SAI_PACKET_ACTION_FORWARD:
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionYpDrop);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionYpCopyToCpu);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                break;
            default: break;
        }
        if (!(act & 0x4))
        {
            act |= 0x4;
            rpa = SAI_PACKET_ACTION_FORWARD;
        }
        switch(rpa)
        {
            case SAI_PACKET_ACTION_DROP:
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionRpDrop);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                break;
            case SAI_PACKET_ACTION_FORWARD:
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionRpDrop);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                rv = bcm_field_action_remove(0, entry,
                                             bcmFieldActionRpCopyToCpu);
                BRCM_SAI_API_CHK(SAI_API_ACL, "field action delete", rv);
                break;
            default: break;
        }
    }
    else
    {
        rv = bcm_field_action_remove(0, entry, bcmFieldActionGpCopyToCpu);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

sai_status_t
_brcm_sai_update_traps(sai_object_id_t hostif_trap_group_id, int attr_id,
                       _brcm_sai_trap_group_t *tg)
{
    sai_status_t rv;
    int e, tgid, arp_type;
    bcm_field_entry_t *entry;
    _brcm_sai_trap_refs_t *current;
    _brcm_sai_indexed_data_t idata_t, idata_tg;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    if (FALSE == tg->valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = tg->traps;
    if (IS_NULL(current) || 0 == tg->ref_count)
    {
        return SAI_STATUS_SUCCESS;
    }

    do
    {
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP, &current->trap,
                                        &idata_t);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap get", rv);
        entry = idata_t.trap.entry;
        tgid = BRCM_SAI_GET_OBJ_VAL(int, idata_t.trap.trap_group);
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                        &tgid, &idata_tg);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group data get", rv);
        arp_type = -1;
        if (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == idata_t.trap.trap_id ||
            SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE == idata_t.trap.trap_id)
        {
            arp_type = idata_t.trap.trap_id;
        }

        switch (attr_id)
        {
            case SAI_HOSTIF_TRAP_GROUP_ATTR_ADMIN_STATE:
                if (tg->state != idata_tg.trap_group.state)
                {
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = _brcm_entry_enable(entry[e], tg->state);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable", rv);
                    }
                    if (-1 != arp_type)
                    {
                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_ENABLE,
                                                               tg->state, -1, -1, -1, NULL);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                    }
                }
                break;
            case SAI_HOSTIF_TRAP_GROUP_ATTR_QUEUE:
                if (tg->qnum != idata_tg.trap_group.qnum)
                {
                    for (e=0; e<idata_t.trap.count; e++)
                    {
                        rv = bcm_field_action_remove(0, entry[e], bcmFieldActionCosQCpuNew);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                        rv = bcm_field_entry_reinstall(0, entry[e]);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                        rv = bcm_field_action_add(0, entry[e], bcmFieldActionCosQCpuNew,
                                                  tg->qnum, 0);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                        rv = bcm_field_entry_reinstall(0, entry[e]);
                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                    }
                    if (-1 != arp_type)
                    {
                        rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_QUEUE_SET,
                                                               -1, -1, tg->qnum, -1, NULL);
                        BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                    }
                }
                break;
            case SAI_HOSTIF_TRAP_GROUP_ATTR_POLICER:
            {
                uint8_t act;
                sai_packet_action_t gpa=0, ypa=0, rpa=0;

                /* If a valid policer is being applied */
                if (SAI_NULL_OBJECT_ID != tg->policer_id)
                {
                    /* If new and old are not the same */
                    if (tg->policer_id != idata_tg.trap_group.policer_id)
                    {
                        /* If old is not null */
                        if (SAI_NULL_OBJECT_ID != idata_tg.trap_group.policer_id)
                        {
                            for (e=0; e<idata_t.trap.count; e++)
                            {
                                rv = bcm_field_entry_policer_detach(0, entry[e], 0);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer detach", rv);
                                rv = bcm_field_entry_reinstall(0, entry[e]);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                            }
                            if (-1 != arp_type)
                            {
                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_DETACH,
                                                                       -1, -1, -1, -1, NULL);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                            }
                            if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                                (TRUE == idata_t.trap.installed))
                            {
                                /* Check and remove old policer actions */
                                act = BRCM_SAI_GET_OBJ_SUB_TYPE(idata_tg.trap_group.policer_id);
                                if (act)
                                {
                                    rv = _brcm_sai_policer_actions_get(idata_tg.trap_group.policer_id,
                                                                       &gpa, &ypa, &rpa);
                                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                                }
                                for (e=0; e<idata_t.trap.count; e++)
                                {
                                    rv = _brcm_sai_entry_policer_actions_remove(entry[e], act, gpa, ypa, rpa);
                                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer remove", rv);
                                    rv = bcm_field_entry_reinstall(0, entry[e]);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                                    if (!act)
                                    {
                                        rv = bcm_field_action_remove(0, entry[e], bcmFieldActionDrop);
                                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                                        rv = bcm_field_entry_reinstall(0, entry[e]);
                                        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                                    }
                                }
                                if (-1 != arp_type)
                                {
                                    _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_REMOVE,
                                                                           -1, -1, -1, -1, &ppa);
                                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap pol action remove", rv);
                                    if (!act)
                                    {
                                         rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_REMOVE,
                                                                                -1, -1, -1, -1, NULL);
                                         BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap drop remove", rv);
                                    }
                                }
                            }
                            if (act)
                            {
                                /* Detach from old policer */
                                _brcm_sai_policer_action_ref_detach
                                    (BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TRAP,
                                                         idata_t.trap.idx));
                            }
                        }
                        else /* Handle old null policer */
                        {
                            if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                                (TRUE == idata_t.trap.installed))
                            {
                                for (e=0; e<idata_t.trap.count; e++)
                                {
                                    rv = bcm_field_action_remove(0, entry[e], bcmFieldActionCopyToCpu);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                                    rv = bcm_field_action_remove(0, entry[e], bcmFieldActionDrop);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action delete", rv);
                                    rv = bcm_field_entry_reinstall(0, entry[e]);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                                }
                                if (-1 != arp_type)
                                {
                                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_TRAP_REMOVE,
                                                                           -1, -1, -1, -1, NULL);
                                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap remove", rv);
                                }
                            }
                        }
                        if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                            (TRUE == idata_t.trap.installed))
                        {
                            /* Check and apply new policer actions */
                            act = BRCM_SAI_GET_OBJ_SUB_TYPE(tg->policer_id);
                            if (act)
                            {
                                rv = _brcm_sai_policer_actions_get(tg->policer_id,
                                                                   &gpa, &ypa, &rpa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                            }
                            for (e=0; e<idata_t.trap.count; e++)
                            {
                                rv = _brcm_sai_entry_policer_actions_add(entry[e], act, gpa, ypa, rpa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions add", rv);
                                if (!act)
                                {
                                    rv = bcm_field_action_add(0, entry[e], bcmFieldActionDrop, 0, 0);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                                }
                            }
                            if (-1 != arp_type)
                            {
                                _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_ADD_ONLY,
                                                                       -1, -1, -1, -1, &ppa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap pol action remove", rv);
                                if (!act)
                                {
                                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_ADD_ONLY,
                                                                           -1, -1, -1, -1, NULL);
                                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap drop", rv);
                                }
                            }
                        }
                        if (act)
                        {
                            /* Attach with new policer */
                            rv = _brcm_sai_policer_action_ref_attach(tg->policer_id,
                                     (BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TRAP,
                                                          idata_t.trap.idx)));
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer action ref attach", rv);
                        }
                        for (e=0; e<idata_t.trap.count; e++)
                        {
                            rv = bcm_field_entry_policer_attach(0, entry[e], 0,
                                                                BRCM_SAI_GET_OBJ_VAL(int, tg->policer_id));
                            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                        }
                        if (-1 != arp_type)
                        {
                            rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POLICER_ATTACH,
                                                                   -1, -1, -1,
                                                                   BRCM_SAI_GET_OBJ_VAL(int, tg->policer_id),
                                                                   NULL);
                        }
                    }
                }
                else /* A null policer is applied - remove previous policer */
                {
                    /* Did an older policer exist */
                    if (SAI_NULL_OBJECT_ID != idata_tg.trap_group.policer_id)
                    {
                        if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                            (TRUE == idata_t.trap.installed))
                        {
                            act = BRCM_SAI_GET_OBJ_SUB_TYPE(idata_tg.trap_group.policer_id);
                            if (act)
                            {
                                rv = _brcm_sai_policer_actions_get(idata_tg.trap_group.policer_id,
                                                                   &gpa, &ypa, &rpa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                            }
                        }
                        for (e=0; e<idata_t.trap.count; e++)
                        {
                            rv = bcm_field_entry_policer_detach(0, entry[e], 0);
                            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer detach", rv);
                            if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                                (TRUE == idata_t.trap.installed))
                            {
                                rv = _brcm_sai_entry_policer_actions_remove(entry[e], act, gpa, ypa, rpa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer remove", rv);
                                rv = bcm_field_entry_reinstall(0, entry[e]);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                                if (act) /* Only if old policer was color aware */
                                {
                                    rv = bcm_field_action_add(0, entry[e], bcmFieldActionDrop, 0, 0);
                                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                                }
                                rv = bcm_field_action_add(0, entry[e], bcmFieldActionCopyToCpu,
                                                          0, 0);
                                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
                            }
                        }
                        if (-1 != arp_type)
                        {
                            rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_DETACH,
                                                                   -1, -1, -1, -1, NULL);
                            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                            if ((SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta) &&
                                (TRUE == idata_t.trap.installed))
                            {
                                _brcm_sai_policer_pa_t ppa = { act, gpa, ypa, rpa };

                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_POL_ACT_REMOVE,
                                                                       -1, -1, -1, -1, &ppa);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                                if (act) /* Only if old policer was color aware */
                                {
                                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_DROP_ADD_ONLY,
                                                                           -1, -1, -1, -1, NULL);
                                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap enable entries", rv);
                                }
                                rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_COPY_TO_CPU_ADD_ONLY,
                                                                       -1, -1, -1, -1, NULL);
                                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap copy to cpu", rv);
                            }
                        }
                        if (act)
                        {
                            /* Detach from old policer */
                            _brcm_sai_policer_action_ref_detach
                                (BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_HOSTIF_TRAP,
                                                     idata_t.trap.idx));
                        }
                    }
                }
                for (e=0; e<idata_t.trap.count; e++)
                {
                    rv = bcm_field_entry_reinstall(0, entry[e]);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
                }
                if (-1 != arp_type)
                {
                    rv = _brcm_sai_arp_trap_entries_update(arp_type, _ENTRY_REINSTALL,
                                                           -1, -1, -1, -1, NULL);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap reinstall entries", rv);
                }
                break;
            }
            default:
                return SAI_STATUS_INVALID_PARAMETER;
        }
        current = current->next;
    } while (!IS_NULL(current));

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
__brcm_update_trap_pa(bcm_field_entry_t entry, bool add, int policer,
                       uint8_t act, sai_packet_action_t gpa,
                      sai_packet_action_t ypa, sai_packet_action_t rpa)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;

    /* 1st, remove current DROP action */
    rv = bcm_field_action_remove(0, entry, bcmFieldActionDrop);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action remove", rv);
    }
    rv = bcm_field_entry_reinstall(0, entry);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);
    if (TRUE == add)
    {
        if (-1 != policer)
        {
            /* Remove current COPY action */
            rv = bcm_field_action_remove(0, entry, bcmFieldActionCopyToCpu);
            if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
            {
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action remove", rv);
            }
            rv = _brcm_sai_entry_policer_actions_add(entry, act,
                                                     gpa, ypa, rpa);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions add", rv);
        }
        else
        {
            rv = bcm_field_action_add(0, entry, bcmFieldActionCopyToCpu,
                                      0, 0);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
        }
        if (!act)
        {
            rv = bcm_field_action_add(0, entry, bcmFieldActionDrop, 0, 0);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
        }
    }
    else
    {
        if (-1 != policer)
        {
            rv = _brcm_sai_entry_policer_actions_remove(entry, act,
                                                        gpa, ypa, rpa);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policer actions remove", rv);
        }
        else
        {
            rv = bcm_field_action_remove(0, entry, bcmFieldActionCopyToCpu);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action remove", rv);
        }
        rv = bcm_field_action_add(0, entry, bcmFieldActionDrop, 0, 0);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
    }
    rv = bcm_field_entry_reinstall(0, entry);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry reinstall", rv);

    return rv;
}


STATIC sai_status_t
_brcm_sai_update_arp_trap_pa(int type, bool add, int policer,
                              uint8_t act, sai_packet_action_t gpa,
                             sai_packet_action_t ypa, sai_packet_action_t rpa)
{
    _brcm_sai_unicast_arp_t uat;
    _brcm_sai_table_data_t tdata;
    sai_status_t _rv, rv = SAI_STATUS_SUCCESS;

    DATA_CLEAR(uat, _brcm_sai_unicast_arp_t);
    tdata.ucast_arp = &uat;
    do
    {
        rv = _brcm_sai_db_table_entry_getnext(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
        if (SAI_STATUS_SUCCESS == rv && type == uat.type && FALSE == uat.sysmac)
        {
            _rv = __brcm_update_trap_pa(uat.entry_id, add, policer, act,
                                        gpa, ypa, rpa);
            BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "update trap pa", _rv);
        }
    } while (SAI_STATUS_SUCCESS == rv);
    if (SAI_STATUS_ITEM_NOT_FOUND == rv)
    {
        rv = SAI_STATUS_SUCCESS;
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_update_trap_pa(int index, bool add)
{
    uint8_t act = 0;
    sai_status_t rv;
    int e, tgidx, policer = -1;
    sai_packet_action_t gpa=0, ypa=0, rpa=0;
    _brcm_sai_indexed_data_t idata_t, idata_tg;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP, &index, &idata_t);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap get", rv);
    if ((FALSE == idata_t.trap.valid) ||
       ((TRUE == idata_t.trap.installed) && (TRUE == add)) ||
       ((FALSE == idata_t.trap.installed) && (FALSE == add)))
    {
        return rv;
    }
    /* Get Trap Group info */
    tgidx = BRCM_SAI_GET_OBJ_VAL(int, idata_t.trap.trap_group);
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                       "update trap %d pa for tg %d\n", index, tgidx);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP, &tgidx, &idata_tg);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
    if (SAI_NULL_OBJECT_ID != idata_tg.trap_group.policer_id)
    {
        policer = BRCM_SAI_GET_OBJ_VAL(int, idata_tg.trap_group.policer_id);
    }
    /* When the Trap was created, the default PA is (always) DROP.
     * Creation of Table Entry should cause the configured PA to
     * take effect. Removal should take the PA back to DROP.
     */
    if (SAI_PACKET_ACTION_DROP != idata_t.trap.pkta)
    {
        if (SAI_PACKET_ACTION_TRAP == idata_t.trap.pkta ||
            SAI_PACKET_ACTION_COPY == idata_t.trap.pkta)
        {
            if (-1 != policer)
            {
                act = BRCM_SAI_GET_OBJ_SUB_TYPE(idata_tg.trap_group.policer_id);
                if (act)
                {
                    rv = _brcm_sai_policer_actions_get(idata_tg.trap_group.policer_id,
                                                       &gpa, &ypa, &rpa);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "policers actions get", rv);
                }
            }
            for (e=0; e<idata_t.trap.count; e++)
            {
                rv = __brcm_update_trap_pa(idata_t.trap.entry[e], add, policer,
                                           act, gpa, ypa, rpa);
                BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "update trap pa", rv);
            }
            if (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == idata_t.trap.trap_id ||
                SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE == idata_t.trap.trap_id)
            {
                rv = _brcm_sai_update_arp_trap_pa
                         (SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == idata_t.trap.trap_id ?
                          0 : 1, add, policer, act, gpa, ypa, rpa);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "arp trap pa update", rv);
            }
            if (SAI_HOSTIF_TRAP_TYPE_L3_MTU_ERROR == idata_t.trap.trap_id)
            {
                rv =  bcm_switch_control_set(0, bcmSwitchL3MtuFailToCpu, 1);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "setting l3 mtu fail to cpu", rv);
            }
        }
        else
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                               "Invalid packet action %d\n", idata_t.trap.pkta);
            return SAI_STATUS_INVALID_PARAMETER;
        }
    }
    if (TRUE == add)
    {
        idata_t.trap.installed = TRUE;
    }
    else
    {
        idata_t.trap.installed = FALSE;
    }
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP, &index, &idata_t);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

sai_status_t
_brcm_sai_trap_group_add_trap_ref(int trap_group_id, int trap_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_trap_refs_t *current;
    _brcm_sai_trap_refs_t *prev = NULL, *new_ref;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &trap_group_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group data get", rv);
    if (FALSE == data.trap_group.valid)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.trap_group.traps;
    /* See if the object is already in the list. */
    while (NULL != current)
    {
        if (current->trap == trap_id)
        {
            /* Node found */
            return SAI_STATUS_SUCCESS;
        }
        prev = current;
        current = current->next;
    }
    current = prev;

    /* Need to add to the list. */
    new_ref = ALLOC_CLEAR(1, sizeof(_brcm_sai_trap_refs_t));
    if (IS_NULL(new_ref))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_CRITICAL,
                           "Error allocating memory for trap group trap refs list.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    new_ref->trap = trap_id;
    new_ref->next = NULL;
    if (IS_NULL(current))
    {
        /* 1st object */
        current = new_ref;
        data.trap_group.traps = current;
    }
    else
    {
        current->next = new_ref;
    }
    data.trap_group.ref_count++;
    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &trap_group_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group set", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_trap_group_delete_trap_ref(int trap_group_id, int trap_id)
{
    sai_status_t rv;
    _brcm_sai_indexed_data_t data;
    _brcm_sai_trap_refs_t *prev, *current;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                    &trap_group_id, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "udf group data get", rv);
    if (FALSE == data.trap_group.valid)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Invalid trap group.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    current = data.trap_group.traps;
    if (IS_NULL(current) || 0 == data.trap_group.ref_count)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unused trap group.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    do
    {
        if (current->trap == trap_id)
        {
            data.trap_group.ref_count--;
            if (current == data.trap_group.traps)
            {
                data.trap_group.traps = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            CHECK_FREE_SIZE(current);
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                            &trap_group_id, &data);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group data set", rv);
            break;
        }
        prev = current;
        current = current->next;
    } while (!IS_NULL(current));

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_entry_enable(bcm_field_entry_t entry, bool state)
{
    sai_status_t rv;

    rv = bcm_field_entry_enable_set(0, entry, state);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "entry enable set", rv);

    return rv;
}

sai_status_t
_brcm_entry_field_qualify(bcm_field_entry_t entry,
                          sai_hostif_trap_type_t hostif_trapid)
{
    sai_status_t rv;
    switch (hostif_trapid)
    {
        case SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST:
        case SAI_HOSTIF_TRAP_TYPE_ARP_RESPONSE:
        {
            rv = bcm_field_qualify_IpType(0, entry,
                                          SAI_HOSTIF_TRAP_TYPE_ARP_REQUEST == hostif_trapid ?
                                          bcmFieldIpTypeArpRequest : bcmFieldIpTypeArpReply);
            BRCM_SAI_API_CHK(SAI_API_HOSTIF, "entry field qualify", rv);
            break;
        }
        default:
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "Unknown trap id %x",
                               hostif_trapid);
            return SAI_STATUS_NOT_IMPLEMENTED;
    }

    return rv;
}

/*
 * type = 0: REQUEST, 1: RESPONSE
 */
STATIC sai_status_t
_brcm_sai_ucast_arp_table_add(bcm_mac_t mac, int type, bool *new,
                              _brcm_sai_unicast_arp_t *uat)
{
    _brcm_sai_table_data_t tdata;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    PTR_CLEAR(uat, _brcm_sai_unicast_arp_t);
    sal_memcpy(&uat->mac, mac, sizeof(bcm_mac_t));
    uat->type = type;
    uat->sysmac = FALSE;
    tdata.ucast_arp = uat;
    rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
    if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "error in ucast arp table lookup.\n");
        return rv;
    }
    if (SAI_STATUS_SUCCESS == rv) /* existing entry */
    {
        uat->ref_count++;
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "MAC[%02x:%02x:%02x:%02x:%02x:%02x] "
                           "type: %d refcount: %d\n",
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                           uat->type, uat->ref_count);
        *new = FALSE;
    }
    else /* new entry */
    {
        uat->entry_id = -1;
        uat->ref_count = 1;
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "MAC[%02x:%02x:%02x:%02x:%02x:%02x] "
                           "type %d added\n",
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], uat->type);
        *new = TRUE;
    }
    rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "ucast arp table entry add", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
 * type = 0: REQUEST, 1: RESPONSE, -1: both
 * mac_type = 0: switch, 1: rif
 */
sai_status_t
_brcm_sai_arp_trap_add(int unit, bool state, int trap_type, int mac_type, bcm_mac_t *_mac)
{
    bcm_mac_t mac;
    bool new = FALSE;
    //bcm_policer_t pol_id;
    _brcm_sai_data_t data;
    _brcm_sai_unicast_arp_t uat;
    _brcm_sai_table_data_t tdata;
    int idx, tgidx, b_entry, n_entry;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_mac_t bcast_addr = _BCAST_MAC_ADDR;
    _brcm_sai_indexed_data_t idata, idata_tg;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    DATA_CLEAR(uat, _brcm_sai_unicast_arp_t);
    sal_memcpy(&mac, _mac, sizeof(bcm_mac_t));
    if (-1 == trap_type || 0 == trap_type)
    {
        rv = _brcm_sai_global_data_get(_BRCM_SAI_ARP_TRAP_REQ, &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                        "getting arp trap req state global data", rv);
        if (data.u32) /* If ARP REQUEST trap has been created */
        {
            rv = _brcm_sai_ucast_arp_table_add(mac, 0, &new, &uat);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "ucast arp table add/update", rv);
            if (new)
            {
                idx = data.u32;
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP,
                                                &idx, &idata);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap get", rv);
                b_entry = idata.trap.entry[0];
                rv = bcm_field_entry_copy(0, b_entry, &n_entry);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry copy", rv);
                rv = bcm_field_qualify_DstMac(0, n_entry, mac, bcast_addr);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
#if 0 /* Not required with SDK 6.5.10+ */
                rv = bcm_field_entry_policer_get(unit, b_entry, 0, &pol_id);
                if (BCM_SUCCESS(rv))
                {
                    rv = bcm_field_entry_policer_attach(0, n_entry, 0, pol_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
#endif
                rv = bcm_field_entry_install(0, n_entry);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Added ARP trap request entry %d\n",
                                   n_entry);
                if (-1 == state)
                {
                    tgidx = BRCM_SAI_GET_OBJ_VAL(int, idata.trap.trap_group);
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                                    &tgidx, &idata_tg);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
                    state = idata_tg.trap_group.state;
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                                       "Loaded state (%d) from group id: %d\n",
                                       state, tgidx);
                }
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(n_entry, 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                if (0 == mac_type)
                {
                    idata.trap.entry[1] = n_entry;
                    idata.trap.count = 2;
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP,
                                                    &idx, &idata);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap set", rv);
                    uat.sysmac = TRUE;
                }
                uat.entry_id = n_entry;
                tdata.ucast_arp = &uat;
                rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "ucast arp table entry add", rv);
            }
        }
        else
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "ARP request trap not installed.\n");
        }
    }
    if (-1 == trap_type || 1 == trap_type)
    {
        rv = _brcm_sai_global_data_get(_BRCM_SAI_ARP_TRAP_RESP, &data);
        BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                        "getting arp trap resp state global data", rv);
        if (data.u32) /* If ARP RESPONSE trap has been created */
        {
            rv = _brcm_sai_ucast_arp_table_add(mac, 1, &new, &uat);
            BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "ucast arp table add/update", rv);
            if (new)
            {
                idx = data.u32;
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP,
                                                &idx, &idata);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap get", rv);
                b_entry = idata.trap.entry[0];
                rv = bcm_field_entry_copy(0, b_entry, &n_entry);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry copy", rv);
                rv = bcm_field_qualify_DstMac(0, n_entry, mac, bcast_addr);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field qualify", rv);
#if 0 /* Not required with SDK 6.5.10+ */
                rv = bcm_field_entry_policer_get(unit, b_entry, 0, &pol_id);
                if (BCM_SUCCESS(rv))
                {
                    rv = bcm_field_entry_policer_attach(0, n_entry, 0, pol_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry policer attach", rv);
                }
#endif
                rv = bcm_field_entry_install(0, n_entry);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Added ARP trap response entry %d\n",
                                   n_entry);
                if (-1 == state)
                {
                    tgidx = BRCM_SAI_GET_OBJ_VAL(int, idata.trap.trap_group);
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_TRAP_GROUP,
                                                    &tgidx, &idata_tg);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap group get", rv);
                    state = idata_tg.trap_group.state;
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                                       "Loaded state (%d) from group id: %d\n",
                                       state, tgidx);
                }
                if (FALSE == state)
                {
                    rv = _brcm_entry_enable(n_entry, 0);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "field entry enable set", rv);
                }
                if (0 == mac_type)
                {
                    idata.trap.entry[1] = n_entry;
                    idata.trap.count = 2;
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_TRAP,
                                                    &idx, &idata);
                    BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "trap set", rv);
                    uat.sysmac = TRUE;
                }
                uat.entry_id = n_entry;
                tdata.ucast_arp = &uat;
                rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "ucast arp table entry add", rv);
            }
        }
        else
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "ARP response trap not installed.\n");
        }
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

void
_brcm_sai_arp_rif_cb(int unit, _brcm_sai_unicast_arp_cb_info_t *cb_info)
{
    bcm_mac_t mac;
    char *rift, *_rift[] = { "PORT", "LAG", "VLAN" };
    sai_status_t rv = SAI_STATUS_SUCCESS;

    sal_memcpy(&mac, cb_info->mac, sizeof(bcm_mac_t));
    rv = _brcm_sai_arp_trap_add(0, cb_info->state, cb_info->arp_type, 1, &mac);
    switch (cb_info->rif_type)
    {
        case SAI_ROUTER_INTERFACE_TYPE_PORT:
            rift = _rift[0];
            break;
        case _BRCM_SAI_RIF_TYPE_LAG:
            rift = _rift[1];
            break;
        case SAI_ROUTER_INTERFACE_TYPE_VLAN:
            rift = _rift[2];
            break;
        default:
            return;
    }

    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR,
                           "Error processing arp trap type %d for %s rif "
                           "for mac[%02x%02x%02x%02x%02x%02x]\n", cb_info->arp_type, rift,
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    else
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Processed %s rif based arp "
                           "trap type %d entry update.\n", rift, cb_info->arp_type);
    }

    return;
}

/*
 * type = 0: REQUEST, 1: RESPONSE
 */
STATIC sai_status_t
_brcm_sai_arp_trap_entries_add(int unit, int type, bool state)
{
    bcm_mac_t mac;
    _brcm_sai_unicast_arp_cb_info_t cb_info;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    /* Add switch mac unicast arp trap */
    rv = _brcm_sai_system_mac_get(&mac);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "system mac get", rv);
    rv = _brcm_sai_arp_trap_add(0, state, type, 0, &mac);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "arp trap add", rv);

    cb_info.rif_type = SAI_ROUTER_INTERFACE_TYPE_PORT;
    cb_info.arp_type = type;
    cb_info.state = state;
    /* Iterate over all RIFs, get macs and create arp trap entries */
    rv = _brcm_sai_rif_traverse(unit, &cb_info, _brcm_sai_arp_rif_cb);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "port rif traverse", rv);
    cb_info.rif_type = SAI_ROUTER_INTERFACE_TYPE_VLAN;
    rv = _brcm_sai_rif_traverse(unit, &cb_info, _brcm_sai_arp_rif_cb);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "vlan rif traverse", rv);
    cb_info.rif_type = _BRCM_SAI_RIF_TYPE_LAG;
    rv = _brcm_sai_rif_traverse(unit, &cb_info, _brcm_sai_arp_rif_cb);
    BRCM_SAI_RV_CHK(SAI_API_SWITCH, "lag rif traverse", rv);

    return rv;
}

/*
 * type = 0: REQUEST, 1: RESPONSE
 */
STATIC sai_status_t
_brcm_sai_ucast_arp_traps_remove(int type)
{
    _brcm_sai_data_t data;
    _brcm_sai_unicast_arp_t uat;
    _brcm_sai_table_data_t tdata;
    sai_status_t _rv, rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    /* Iterate through table and remove all matching entries */
    DATA_CLEAR(uat, _brcm_sai_unicast_arp_t);
    tdata.ucast_arp = &uat;
    do
    {
        rv = _brcm_sai_db_table_entry_getnext(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
        if (SAI_STATUS_SUCCESS == rv && type == uat.type)
        {
            if (FALSE == uat.sysmac)
            {
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Removing entry: %d",
                                   uat.entry_id);
                _rv = bcm_field_entry_destroy(0, uat.entry_id);
                BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry destroy", _rv);
            }
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG,
                               "Removing MAC[%02x:%02x:%02x:%02x:%02x:%02x] "
                               "type: %d with refcount: %d\n",
                               uat.mac[0], uat.mac[1], uat.mac[2],
                               uat.mac[3], uat.mac[4], uat.mac[5],
                               uat.type, uat.ref_count);
            _rv =  _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
            BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "ucast arp table entry delete.", _rv);
        }
    } while (SAI_STATUS_SUCCESS == rv);

    data.u32 = 0;
    rv = _brcm_sai_global_data_set(0 == type ? _BRCM_SAI_ARP_TRAP_REQ :
                                   _BRCM_SAI_ARP_TRAP_RESP, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                    "setting arp trap state global data", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}

/*
 * trap_type = 0: REQUEST, 1: RESPONSE
 * mac_type = 0: switch, 1: rif
 */
sai_status_t
_brcm_sai_ucast_arp_trap_entry_del(int unit, int trap_type, int mac_type,
                                   bcm_mac_t *mac)
{
    _brcm_sai_data_t data;
    _brcm_sai_unicast_arp_t uat;
    _brcm_sai_table_data_t tdata;
    sai_status_t rv = SAI_STATUS_SUCCESS;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HOSTIF);
    DATA_CLEAR(uat, _brcm_sai_unicast_arp_t);
    rv = _brcm_sai_global_data_get(0 == trap_type ?
                                   _BRCM_SAI_ARP_TRAP_REQ :
                                   _BRCM_SAI_ARP_TRAP_RESP, &data);
    BRCM_SAI_RV_CHK(SAI_API_HOSTIF,
                    "getting arp trap req state global data", rv);
    if (data.u32) /* If ARP TRAP trap has been created */
    {
        sal_memcpy(&uat.mac, mac, sizeof(bcm_mac_t));
        uat.type = trap_type;
        tdata.ucast_arp = &uat;
        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
        if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_ERROR, "error in ucast arp table lookup.\n");
            return rv;
        }
        if (SAI_STATUS_SUCCESS == rv) /* existing entry */
        {
            uat.ref_count--;
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "MAC[%02x:%02x:%02x:%02x:%02x:%02x] "
                               "type: %d refcount: %d entry: %d\n", uat.mac[0], uat.mac[1],
                               uat.mac[2], uat.mac[3], uat.mac[4], uat.mac[5],
                               uat.type, uat.ref_count, uat.entry_id);
            if (0 == uat.ref_count)
            {
                if (FALSE == uat.sysmac)
                {
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Removing entry: %d",
                                       uat.entry_id);
                    rv = bcm_field_entry_destroy(0, uat.entry_id);
                    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry destroy", rv);
                }
                else
                {
                    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "MAC[%02x:%02x:%02x:%02x:%02x:%02x] "
                                       "(matching system mac) type %d found with ref count 0 !!\n",
                                       uat.mac[0], uat.mac[1], uat.mac[2],
                                       uat.mac[3], uat.mac[4], uat.mac[5], uat.type);

                }
                rv =  _brcm_sai_db_table_entry_delete(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
                BRCM_SAI_RV_CHK(SAI_API_NEXT_HOP, "ucast arp table entry delete.", rv);
            }
            else
            {
                rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_UCAST_ARP, &tdata);
                BRCM_SAI_RV_CHK(SAI_API_HOSTIF, "ucast arp table entry add", rv);
                BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Refcount decremented and table entry saved.");
            }
        }
        else /* entry not found */
        {
            BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "RIF MAC[%02x:%02x:%02x:%02x:%02x:%02x] "
                               "type %d not found !!\n",
                               uat.mac[0], uat.mac[1], uat.mac[2],
                               uat.mac[3], uat.mac[4], uat.mac[5], uat.type);
            /* This is possible for multiple RIFs having same mac addr */
            rv = SAI_STATUS_SUCCESS;
        }
    }
    else
    {
        BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "ARP %s trap not installed.\n",
                           0 == trap_type ? "request" : "response");
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HOSTIF);
    return rv;
}


/**
 * @brief      Function to add CPUCosQueue and drop action
 * @param[in]  Entry Id and Queue
 *
 * @return  SAI_STATUS_SUCCESS on success
 *           Failure status code on error
 */
sai_status_t
_brcm_sai_cpuqueue_drop_action_add(bcm_field_entry_t* entry, int queue)
{
  sai_status_t rv;
  rv = bcm_field_action_add(0, *entry, bcmFieldActionDrop, 0, 0);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
  rv = bcm_field_action_add(0, *entry, bcmFieldActionCosQCpuNew, queue, 0);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
  if (_brcm_sai_cpu_pool_config)
  {
    /* CPU bound packets need to be accounted for in CPU PG */
    rv = bcm_field_action_add(0, *entry, bcmFieldActionPrioIntNew, _brcm_sai_cpu_pg_id, 0);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
  }
  return rv;
}

 /**
 * @brief      Function to add CPUCosQueue
 * @param[in]  Entry Id and Queue
 *
 * @return  SAI_STATUS_SUCCESS on success
 *           Failure status code on error
 */
sai_status_t
_brcm_sai_cpuqueue_copy_action_add(bcm_field_entry_t* entry, int queue)
{
    sai_status_t rv;
    rv = bcm_field_action_add(0, *entry, bcmFieldActionCopyToCpu, 0, 0);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
    rv = bcm_field_action_add(0, *entry, bcmFieldActionCosQCpuNew, queue, 0);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
    if (_brcm_sai_cpu_pool_config)
    {
        /* CPU bound packets need to be accounted for in CPU PG */
        rv = bcm_field_action_add(0, *entry, bcmFieldActionPrioIntNew, _brcm_sai_cpu_pg_id, 0);
        BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
    }
    return rv;
}

/**
 * @brief      Function to create trap entry for sending packets to PG7
 * @param[in]  None
 *
 * @return  SAI_STATUS_SUCCESS on success
 *           Failure status code on error
 */
sai_status_t
_brcm_sai_create_trap_group_entry()
{
  _brcm_sai_data_t data;
  int group, egress_id;
  bcm_field_entry_t entry;
  int rv;
  static int trap_group_entry_set = 0;

  if (trap_group_entry_set != 0)
  {
    /* First delete the existing entry */
    rv = bcm_field_entry_destroy(0, trap_group_entry_set);
    BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry destroy", rv);
    trap_group_entry_set = 0;
  }

  rv = _brcm_sai_global_data_get(_BRCM_SAI_TRAP_FP_GROUP, &data);
  group = data.u32;
  BRCM_SAI_RV_LVL_CHK(SAI_API_HOSTIF, SAI_LOG_LEVEL_CRITICAL,
                      "getting global fp group", rv);

  rv = bcm_field_entry_create(0, group, &entry);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "Field Entry for CPU Pool", rv);
  /* This rule should not override the host interface rules */
  rv = bcm_field_entry_prio_set(0, entry,
                                BCM_FIELD_ENTRY_PRIO_DEFAULT);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "Field Entry Prio for CPU Pool", rv);

  /* All packets going to CPU must be sent to PG7 */
  rv = _brcm_sai_trap_if_get(&egress_id);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "Egress trap intf id", rv);
  if (egress_id == 0)
  {
    /* This should be set during switch create */
    BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_CRITICAL,
                       "Error getting trap intf for rule\n");
    return SAI_STATUS_FAILURE;
  }
  rv = bcm_field_qualify_DstL3Egress(0, entry, egress_id);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "Entry Qual for CPU Pool", rv);

  rv = bcm_field_action_add(0, entry, bcmFieldActionPrioIntNew,
                            _brcm_sai_cpu_pg_id, 0);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field action add", rv);
  rv = bcm_field_entry_install(0, entry);
  BRCM_SAI_API_CHK(SAI_API_HOSTIF, "field entry install", rv);

  rv = _brcm_entry_enable(entry, TRUE);
  BRCM_SAI_LOG_HINTF(SAI_LOG_LEVEL_DEBUG, "Installed Entry for CPU Pool on Tomahawk2");

  trap_group_entry_set = entry;

  return SAI_STATUS_SUCCESS;
}




/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_hostif_api_t hostif_apis = {
    brcm_sai_create_host_interface,
    brcm_sai_remove_host_interface,
    brcm_sai_set_host_interface_attribute,
    brcm_sai_get_host_interface_attribute,
    brcm_sai_create_hostif_table_entry,
    brcm_sai_remove_hostif_table_entry,
    NULL,
    NULL,
    brcm_sai_create_hostif_trap_group,
    brcm_sai_remove_hostif_trap_group,
    brcm_sai_set_hostif_trap_group_attribute,
    brcm_sai_get_hostif_trap_group_attribute,
    brcm_sai_create_hostif_trap,
    brcm_sai_remove_hostif_trap,
    brcm_sai_set_hostif_trap_attribute,
    brcm_sai_get_hostif_trap_attribute,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};
