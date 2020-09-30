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
#                     Local state - non-persistent across WB                   #
################################################################################
*/
bcm_pbmp_t _rx_los_pbm;
sai_port_state_change_notification_fn _port_state_change_event = NULL;
_brcm_sai_queue_tc_mapping_t port_tc_queue_map_cache[_BRCM_SAI_MAX_PORTS][NUM_QUEUES];

static _brcm_sai_qos_egress_map_t default_pfc_q_map = 
{
    .idx = 0,
    .valid = TRUE,
    .count = 8,
    .map[0].key.prio = 0,
    .map[0].value.queue_index = 0,
    .map[1].key.prio = 1,         
    .map[1].value.queue_index = 1,
    .map[2].key.prio = 2,         
    .map[2].value.queue_index = 2,
    .map[3].key.prio = 3,         
    .map[3].value.queue_index = 3,
    .map[4].key.prio = 4,         
    .map[4].value.queue_index = 4,
    .map[5].key.prio = 5,         
    .map[5].value.queue_index = 5,
    .map[6].key.prio = 6,         
    .map[6].value.queue_index = 6,
    .map[7].key.prio = 7,         
    .map[7].value.queue_index = 7
};

/*
################################################################################
#                             Forward declarations                             #
################################################################################
*/
STATIC sai_status_t
_brcm_sai_port_dot1p_tc_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_dot1p_color_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_dscp_tc_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_dscp_color_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_tc_queue_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_tc_color_dot1p_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_tc_color_dscp_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_tc_pg_map_set(int unit, int port, uint32_t map_id);
STATIC sai_status_t
_brcm_sai_port_pfc_queue_map_set(int unit, int port, uint32_t map_id, 
                                 uint8 pfc_class);
STATIC sai_status_t
_brcm_sai_port_qos_map_array_get(int qmapid, bcm_qos_map_t qosmap_array[_BRCM_SAI_MAX_QOS_MAPS]);
STATIC sai_status_t
_brcm_sai_port_qos_map_add(int qmapid, int pkt_prio, int int_pri, bcm_color_t color);

/*
################################################################################
#                               Event handlers                                 #
################################################################################
*/
void
_brcm_sai_link_event_cb(int unit, bcm_port_t port, 
                        bcm_port_info_t *info)
{
    sai_status_t rv;
    sai_port_oper_status_notification_t status;

    if (IS_NULL(_port_state_change_event))
    {
        return;
    }
    status.port_id = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT, port);
    if (BCM_PORT_LINK_STATUS_UP == info->linkstatus)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_INFO, "Port %d link status event: UP\n",
                          port);
        status.port_state = SAI_PORT_OPER_STATUS_UP;
    }
    else
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_INFO, "Port %d link status event: DOWN\n",
                          port);
        status.port_state = SAI_PORT_OPER_STATUS_DOWN;
        if (DEV_IS_TD2())
        {
            /* Check and enable RX LOS */
            if (!BCM_PBMP_MEMBER(_rx_los_pbm, port))
            {
                rv = _brcm_sai_port_set_rxlos(port, 1);
                if (SAI_STATUS_SUCCESS != rv)
                {
                    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_CRITICAL,
                                      "Port phy control set failed !!\n");
                }
                BCM_PBMP_PORT_ADD(_rx_los_pbm, port);
            }
        }
    }
    _port_state_change_event(1, &status);
}

int
_brcm_get_hardware_lane_count(int unit, int port, 
                              int* lanes)
{
  int rv ;
  bcm_port_resource_t res;

  rv = bcm_port_control_get(unit, port, 
                            bcmPortControlLanes, lanes);
  /* Some platforms only support newer APIs */
  if (rv == BCM_E_UNAVAIL)
  {
    rv = bcm_port_resource_get(unit, port, 
                               &res);
    
    if (rv == BCM_E_NONE)
    {
      *lanes = res.lanes;
    }
  }
  return BRCM_RV_BCM_TO_SAI(rv);
}


/*
################################################################################
#                              Port functions                                  #
################################################################################
*/
/*
* Routine Description:
*   Set port attribute value.
*
* Arguments:
*    [in] port_id - port id
*    [in] attr - attribute
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_set_port_attribute(_In_ sai_object_id_t port_id,
                            _In_ const sai_attribute_t *attr)
{
#define _SET_PORT "Set port"
    int cos=-1;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_port_t port;
    _brcm_sai_indexed_data_t data;
    uint32 rval = 0;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_PORT);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "NULL attr passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (BRCM_SAI_CHK_OBJ_MISMATCH(port_id, SAI_OBJECT_TYPE_PORT))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%16lx passed\n",
                          port_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, port_id);
    switch(attr->id)
    {
        case SAI_PORT_ATTR_TYPE:
        case SAI_PORT_ATTR_OPER_STATUS:
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            break;
        case SAI_PORT_ATTR_ADVERTISED_SPEED:
            rv = SAI_STATUS_SUCCESS; /* FIXME */
            break;
        case SAI_PORT_ATTR_SPEED:
            rv = bcm_port_speed_set(0, port, attr->value.u32);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        case SAI_PORT_ATTR_FULL_DUPLEX_MODE:
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            break;
        case SAI_PORT_ATTR_AUTO_NEG_MODE:
            rv = bcm_port_autoneg_set(0, port, attr->value.booldata);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        case SAI_PORT_ATTR_ADMIN_STATE:
            rv = bcm_port_enable_set(0, port, attr->value.booldata);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        case SAI_PORT_ATTR_MEDIA_TYPE:
        {
            if ((SAI_PORT_MEDIA_TYPE_FIBER != attr->value.u32) &&
                (SAI_PORT_MEDIA_TYPE_COPPER != attr->value.u32))
            {
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            }
            else
            {
                rv = bcm_port_interface_set(0, port, 
                                                SAI_PORT_MEDIA_TYPE_COPPER == attr->value.u32 ?
                                                BCM_PORT_IF_CR4 : BCM_PORT_IF_SR4);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                          attr->id);
            }
            break;
        }
        case SAI_PORT_ATTR_PORT_VLAN_ID:
            rv = bcm_port_untagged_vlan_set(0, port, BRCM_SAI_ATTR_PTR_OBJ_VAL(uint16_t));
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        case SAI_PORT_ATTR_DEFAULT_VLAN_PRIORITY:
            rv = bcm_port_untagged_priority_set(0, port, attr->value.u8);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        case SAI_PORT_ATTR_DROP_UNTAGGED:
        case SAI_PORT_ATTR_DROP_TAGGED:
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            break;
        case SAI_PORT_ATTR_FEC_MODE:
            if (SAI_PORT_FEC_MODE_FC == attr->value.u32)
            {
              /* Turn off CL91 */
              rv = bcm_port_phy_control_set(0, port, 
                                            BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION_CL91,
                                            BCM_PORT_PHY_CONTROL_FEC_OFF);
              rv = bcm_port_phy_control_set(0, port, 
                                            BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION,
                                            BCM_PORT_PHY_CONTROL_FEC_ON);
              BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                        attr->id);
            }
            else if (SAI_PORT_FEC_MODE_RS == attr->value.u32)
            {
              /* Turn off FC */
              rv = bcm_port_phy_control_set(0, port, 
                                            BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION,
                                            BCM_PORT_PHY_CONTROL_FEC_OFF);
              rv = bcm_port_phy_control_set(0, port, 
                                            BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION_CL91,
                                            BCM_PORT_PHY_CONTROL_FEC_ON);
              BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                        attr->id);
            }
            else if (SAI_PORT_FEC_MODE_NONE == attr->value.u32)
            {
                uint32_t val;

                rv = bcm_port_phy_control_get(0, port, 
                         BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION, &val);
                if (BCM_SUCCESS(rv) && (BCM_PORT_PHY_CONTROL_FEC_OFF != val))
                {
                    rv = bcm_port_phy_control_set(0, port, 
                             BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION,
                             BCM_PORT_PHY_CONTROL_FEC_OFF);
                    BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                              attr->id);
                }
                rv = bcm_port_phy_control_get(0, port, 
                         BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION_CL91, &val);
                if (BCM_SUCCESS(rv) && (BCM_PORT_PHY_CONTROL_FEC_OFF != val))
                {
                    rv = bcm_port_phy_control_set(0, port, 
                             BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION_CL91,
                             BCM_PORT_PHY_CONTROL_FEC_OFF);
                    BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                              attr->id);
                }
                else
                {
                    rv = SAI_STATUS_SUCCESS;
                }
            }
            else
            {
                rv = SAI_STATUS_INVALID_ATTR_VALUE_0;
            }
            break;
        case SAI_PORT_ATTR_UPDATE_DSCP:
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            break;
        case SAI_PORT_ATTR_MTU:
            rv = bcm_port_frame_max_set(0, port, attr->value.u32);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            rv = bcm_port_control_set(0, port, 
                                          bcmPortControlStatOversize,
                                          attr->value.s32);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        case SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE:
        {
            rv = _brcm_sai_set_port_attribute(port_id, attr, 0, 0, 0);
            break;
        }
        case SAI_PORT_ATTR_FLOOD_STORM_CONTROL_POLICER_ID:
        case SAI_PORT_ATTR_BROADCAST_STORM_CONTROL_POLICER_ID:
        case SAI_PORT_ATTR_MULTICAST_STORM_CONTROL_POLICER_ID:
        {
            bool pkt_mode;
            int cir, cbs;

            if (DEV_IS_TH3())
            {
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            }
            else
            {
                rv = _brcm_sai_storm_info_get(attr->value.oid, &pkt_mode, &cir, &cbs);
                if (0 != rv)
                {
                    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                                      "Error retreiving policer info.\n");
                    return SAI_STATUS_FAILURE;
                }
                
                rv = _brcm_sai_set_port_attribute(port_id, attr, pkt_mode, cir, cbs);
            }
            break;
        }
        case SAI_PORT_ATTR_GLOBAL_FLOW_CONTROL_MODE:
        case SAI_PORT_ATTR_SUPPORTED_SPEED:
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            break;
        case SAI_PORT_ATTR_INGRESS_ACL:
            rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_PTR_OBJ(), INGRESS,
                                        port_id);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "ACL obj port bind", rv);
            break;
        case SAI_PORT_ATTR_EGRESS_ACL:
            rv = _brcm_sai_acl_obj_bind(BRCM_SAI_ATTR_PTR_OBJ(), EGRESS,
                                        port_id);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "ACL obj port bind", rv);
            break;
        case SAI_PORT_ATTR_INGRESS_MIRROR_SESSION:
        {
            int j, idx;
            bcm_gport_t ms;

            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data get", rv);
            if (0 == BRCM_SAI_ATTR_PTR_OBJ_COUNT())
            {
                rv = bcm_mirror_port_dest_delete_all(0, port, 
                         BCM_MIRROR_PORT_INGRESS);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                              attr->id);
                for (j = 0; j < sizeof(data.port_info.ingress_ms); j++)
                {
                    if (data.port_info.ingress_ms[j])
                    {
                        rv = _brcm_sai_mirror_ref_update(0, j, FALSE);
                        BRCM_SAI_RV_CHK(SAI_API_PORT, "mirror session ref add", rv);
                        data.port_info.ingress_ms[j] = FALSE;
                    }
                }
            }
            else
            {
                for (j=0; j<BRCM_SAI_ATTR_PTR_OBJ_COUNT(); j++)
                {
                    ms = BRCM_SAI_GET_OBJ_VAL(uint32_t, BRCM_SAI_ATTR_PTR_OBJ_LIST(j));
                    rv = bcm_mirror_port_dest_add(0, port, 
                             BCM_MIRROR_PORT_ENABLE|BCM_MIRROR_PORT_INGRESS, ms);
                    BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                              attr->id);
                    idx = ms & 0xff;
                    if (FALSE == data.port_info.ingress_ms[idx])
                    {
                        rv = _brcm_sai_mirror_ref_update(0, idx, TRUE);
                        BRCM_SAI_RV_CHK(SAI_API_PORT, "mirror session ref add", rv);
                        data.port_info.ingress_ms[idx] = TRUE;
                    }
                }
            }
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data set", rv);
            break;
        }
        case SAI_PORT_ATTR_EGRESS_MIRROR_SESSION:
        {
            int j, idx;
            bcm_gport_t ms;

            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data get", rv);
            if (0 == BRCM_SAI_ATTR_PTR_OBJ_COUNT())
            {
                rv = bcm_mirror_port_dest_delete_all(0, port, 
                         BCM_MIRROR_PORT_EGRESS);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                              attr->id);
                for (j = 0; j < sizeof(data.port_info.egress_ms); j++)
                {
                    if (data.port_info.egress_ms[j])
                    {
                        rv = _brcm_sai_mirror_ref_update(0, j, FALSE);
                        BRCM_SAI_RV_CHK(SAI_API_PORT, "mirror session ref add", rv);
                        data.port_info.egress_ms[j] = FALSE;
                    }
                }
            }
            else
            {
                for (j=0; j<BRCM_SAI_ATTR_PTR_OBJ_COUNT(); j++)
                {
                    ms = BRCM_SAI_GET_OBJ_VAL(uint32_t, BRCM_SAI_ATTR_PTR_OBJ_LIST(j));
                    rv = bcm_mirror_port_dest_add(0, port, 
                             BCM_MIRROR_PORT_ENABLE|BCM_MIRROR_PORT_EGRESS, ms);
                    BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                              attr->id);
                    idx = ms & 0xff;
                    if (FALSE == data.port_info.egress_ms[idx])
                    {
                        rv = _brcm_sai_mirror_ref_update(0, idx, TRUE);
                        BRCM_SAI_RV_CHK(SAI_API_PORT, "mirror session ref add", rv);
                        data.port_info.egress_ms[idx] = TRUE;
                    }
                }
            }
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data set", rv);
            break;
        }
        case SAI_PORT_ATTR_INGRESS_SAMPLEPACKET_ENABLE:
        case SAI_PORT_ATTR_EGRESS_SAMPLEPACKET_ENABLE:
        case SAI_PORT_ATTR_POLICER_ID:
        case SAI_PORT_ATTR_QOS_DEFAULT_TC:
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            break;
        case SAI_PORT_ATTR_QOS_DOT1P_TO_TC_MAP:
            rv = _brcm_sai_port_dot1p_tc_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_DOT1P_TO_COLOR_MAP:
            rv = _brcm_sai_port_dot1p_color_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_DSCP_TO_TC_MAP:
            rv = _brcm_sai_port_dscp_tc_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_DSCP_TO_COLOR_MAP:
            rv = _brcm_sai_port_dscp_color_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_TC_TO_QUEUE_MAP:
            rv = _brcm_sai_port_tc_queue_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_TC_AND_COLOR_TO_DOT1P_MAP:
            rv = _brcm_sai_port_tc_color_dot1p_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_TC_AND_COLOR_TO_DSCP_MAP:
            rv = _brcm_sai_port_tc_color_dscp_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_TC_TO_PRIORITY_GROUP_MAP:
            rv = _brcm_sai_port_tc_pg_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid));
            break;
        case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_PRIORITY_GROUP_MAP:
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            break;
        case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_QUEUE_MAP:
        {
            int map_id = BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid);
            
            rv = _brcm_sai_port_pfc_queue_map_set(0, port,
                     BRCM_SAI_GET_OBJ_VAL(uint32_t, attr->value.oid), 0xFF);
            BRCM_SAI_RV_ATTR_CHK(SAI_API_PORT, _SET_PORT, port, rv, map_id);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data get", rv);
            data.port_info.idx = port;
            data.port_info.pfc_q_map = map_id;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data set", rv);
            break;
        }
        case SAI_PORT_ATTR_QOS_SCHEDULER_PROFILE_ID:
        {
            int id, mode;
            bcm_gport_t gport, scheduler_gport;
            _brcm_sai_qos_scheduler_t scheduler;

            id = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
            /* Lookup current id */
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data get", rv);
            /* Allow user to configure a NULL scheduler to a port. */
            if (0 == id)
            {
                int _id;

                /* FIXME: In the future apply the default config */
                _id = data.port_info.sched_id;
                if (_id)
                {
                    data.port_info.sched_id = attr->value.oid;
                    rv = _brcm_sai_scheduler_get(_id, &scheduler);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "sched prof data get", rv);
                    /* Detach non default id */
                    if (SAI_STATUS_SUCCESS !=
                        _brcm_sai_scheduler_detach_object(_id, &scheduler, port_id, FALSE))
                    {
                        BRCM_SAI_LOG_SCHED_GROUP(SAI_LOG_LEVEL_ERROR,
                                                 "Unable to detach port object %lx from scheduler %lx.\n",
                                                 port_id, attr->value.oid);
                        return SAI_STATUS_FAILURE;
                    }
                    /* Update table */
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO, 
                                                    &port, &data);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data set", rv);
                }
                break;
            }
            rv = _brcm_sai_scheduler_get(id, &scheduler);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "sched prof data get", rv);
            if ((0 != scheduler.minimum_bandwidth) ||
                (0 != scheduler.minimum_burst))
            {
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, 
                                  "Scheduler %lx contains minimum bandwidth attributes. "
                                  "This is not supported at the port level.\n",
                                  attr->value.oid);
                return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
            }
            /* Attach port object to scheduler list. */
            if (SAI_STATUS_SUCCESS != 
                    _brcm_sai_scheduler_attach_object(id, &scheduler, port_id))
            {
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, 
                                  "Unable to attach port object %lx to scheduler %lx.\n",
                                  port_id, attr->value.oid);
                return SAI_STATUS_FAILURE;
            }
            mode = _brcm_sai_scheduler_mode_get(&scheduler);
            scheduler_gport = _brcm_sai_switch_port_queue_get(port, 0, 
                                  _BRCM_SAI_PORT_SCHEDULER_TYPE);
            if (-1 == scheduler_gport)
            {
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                return SAI_STATUS_FAILURE;
            }
            rv = bcm_port_gport_get(0, port, &gport);
            BRCM_SAI_API_CHK(SAI_API_PORT, "port gport get", rv);

            /* Note: You may just have a scheduler profile that contains
             * metering/shaping attributes.
             */
            if (_BRCM_SAI_SCHEDULER_ATTR_INVALID != mode)
            {
                rv = bcm_cosq_gport_sched_set(0, gport, 0, mode, scheduler.weight);
                BRCM_SAI_API_CHK(SAI_API_PORT, "cosq gport sched set", rv);
            }
            rv = _brcm_sai_port_sched_prof_set(&scheduler, scheduler_gport, 
                                               (DEV_IS_THX()|| DEV_IS_TD3())
                                               ? TRUE : FALSE,
                                               cos);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "sched prof set", rv);
            /* Update table */
            data.port_info.sched_id = attr->value.oid;
            rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data set", rv);
            break;
        }
        case SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST:
        case SAI_PORT_ATTR_QOS_EGRESS_BUFFER_PROFILE_LIST:
        {
            uint32_t max;
            int j, idx[2];
            sai_status_t rc;
            _brcm_sai_buf_pool_t *buf_pool = NULL;
            _brcm_sai_buf_profile_t *buf_profile = NULL;
            
            rv = driverMmuInfoGet(1, &max);
            BRCM_SAI_API_CHK(SAI_API_PORT, "max pool num", rv);
            if (BRCM_SAI_ATTR_PTR_OBJ_COUNT() > max)
            {
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                                  "buff profile count more than max buffers.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            for (j=0; j<BRCM_SAI_ATTR_PTR_OBJ_COUNT(); j++)
            {
                rc = _brcm_sai_buffer_profile_get(BRCM_SAI_ATTR_PTR_OBJ_LIST(j),
                                                  &buf_profile);
                if (SAI_STATUS_ERROR(rc) || IS_NULL(buf_profile))
                {
                    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                                      "Unable to retreive buffer profile object info.\n");
                    return rc;
                }
                rc = _brcm_sai_buffer_pool_get(buf_profile->pool_id, &buf_pool);
                if (SAI_STATUS_ERROR(rc) || IS_NULL(buf_pool))
                {
                    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                                      "Unable to retreive buffer pool object info.\n");
                    return rc;
                }
                if (SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST == attr->id &&
                    SAI_BUFFER_POOL_TYPE_INGRESS != buf_pool->type)
                {
                    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                                      "invalid buffer profile type.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                data.port_buff.idx2 = idx[1] = port;
                data.port_buff.prof_applied = TRUE;
                if (SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST == attr->id)
                {
                    rv = driverPortSPSet(port, buf_profile->pool_id, 3,
                                         buf_profile->shared_val);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "port SP Max Limit Set", rv);
                    rv = driverPortSPSet(port, buf_profile->pool_id, 2,
                                         buf_profile->size - buf_profile->xoff_thresh);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "port SP Min Limit Set", rv);
                    rv = driverPortSPSet(port, buf_profile->pool_id, 1,
                                         buf_profile->xon_thresh);
                    BRCM_SAI_API_CHK(SAI_API_PORT, "port SP Resume Limit Set", rv);
                    data.port_buff.idx1 = idx[0] = 0;
                }
                else
                {
                    rv = driverEgressPortPoolLimitSet(port, buf_profile->pool_id,
                                                      buf_pool->size);
                    BRCM_SAI_API_CHK(SAI_API_PORT, "port egress pool Limit Set", rv);
                    if (SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC == buf_pool->mode)
                    {
                        rv = driverEgressPortPoolResumeSet(port, buf_profile->pool_id,
                                                           buf_pool->size*75/100);
                        BRCM_SAI_API_CHK(SAI_API_PORT, "port egress pool Resume Set", rv);
                    }
                    data.port_buff.idx1 = idx[0] = 1;
                }                
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_BUF_PROF,
                                                idx, &data);
                BRCM_SAI_RV_CHK(SAI_API_PORT, "port buf prof set", rv);
            }
            break;
        }
        case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL:
        {
            uint32 port_pfc = 0;
            if (attr->value.u8 != 0)
            {
              /* If no PG has pfc on, then disable PFC */
              /* We dont have rx or tx separate SAI params */
              port_pfc = 1;
            }
            if (DEV_IS_TH2())
            {
              rv = bcm_port_control_set(0, port, bcmPortControlPFCTransmit,
                                            port_pfc);  
            }
            else{
              /* For TD2, send bitmap */
              rv = bcm_port_control_set(0, port, bcmPortControlPFCTransmit,
                                            attr->value.u8);  
            }
            BRCM_SAI_API_CHK(SAI_API_PORT, "port control set", rv);
            /* FIXME: retreive PG to PFC PRI map values */
            rv = _brcm_sai_set_port_attribute(port_id, attr, 0, 0, 0);
            BRCM_SAI_API_CHK(SAI_API_PORT, "PFC enable set", rv);
            rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_INFO, 
                                            &port, &data);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port info data get", rv);
            rv = _brcm_sai_port_pfc_queue_map_set(0, port, 
                                                  data.port_info.pfc_q_map, 
                                                  attr->value.u8);           
            break;
        }
        case SAI_PORT_ATTR_PKT_TX_ENABLE:
        {
            if (DEV_IS_TH2())
            {
                int sval = (attr->value.booldata)? 0x0 : 0x3ff;
                SOC_IF_ERROR_RETURN(READ_Q_SCHED_L0_NODE_CONFIGr(0, port, &rval));
                soc_reg_field_set(0, Q_SCHED_L0_NODE_CONFIGr, &rval, MASKf, sval);
                SOC_IF_ERROR_RETURN(WRITE_Q_SCHED_L0_NODE_CONFIGr(0, port, rval));
 	
                SOC_IF_ERROR_RETURN(READ_Q_SCHED_L1_MC_QUEUE_CONFIGr(0, port, &rval));
                soc_reg_field_set(0, Q_SCHED_L1_MC_QUEUE_CONFIGr, &rval, MASKf, sval);
                SOC_IF_ERROR_RETURN(WRITE_Q_SCHED_L1_MC_QUEUE_CONFIGr(0, port, rval));
 
               SOC_IF_ERROR_RETURN(READ_Q_SCHED_L1_UC_QUEUE_CONFIGr(0, port, &rval));
               soc_reg_field_set(0, Q_SCHED_L1_UC_QUEUE_CONFIGr, &rval, MASKf, sval);
               SOC_IF_ERROR_RETURN(WRITE_Q_SCHED_L1_UC_QUEUE_CONFIGr(0, port, rval));

            }
            else
            {
                rv = driverPortPktTxEnableSet(0, port, attr->value.booldata);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                          attr->id);
            }
            break;
        }
        default:
            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, 
                              "Unknown port attribute %d passed\n", attr->id);
            rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }
    
    BRCM_SAI_FUNCTION_EXIT(SAI_API_PORT);
    return rv;
#undef _SET_PORT
}

/*
* Routine Description:
*   Get port attribute value.
*
* Arguments:
*    [in] port_id - port id
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_port_attribute(_In_ sai_object_id_t port_id,
                            _In_ uint32_t attr_count,
                            _Inout_ sai_attribute_t *attr_list)
{
#define _GET_PORT "Get port"
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, val;
    bcm_port_t port;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_PORT);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    if (BRCM_SAI_CHK_OBJ_MISMATCH(port_id, SAI_OBJECT_TYPE_PORT))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                          "Invalid object type 0x%16lx passed\n",
                          port_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    
    port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, port_id);
    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_PORT_ATTR_TYPE:
            {

                if (0 == port)
                {
                    attr_list[i].value.u32 = SAI_PORT_TYPE_CPU;
                }
                else
                {
                    attr_list[i].value.u32 = SAI_PORT_TYPE_LOGICAL;
                }
                break;
            }
            case SAI_PORT_ATTR_OPER_STATUS:
                rv = bcm_port_link_status_get(0, port, &val);
                if (BCM_E_DISABLED == rv || BCM_E_UNAVAIL == rv ||
                    BCM_E_PORT == rv)
                {
                    attr_list[i].value.u32 = SAI_PORT_OPER_STATUS_NOT_PRESENT;
                    rv = SAI_STATUS_SUCCESS;
                    break;
                }
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                attr_list[i].value.u32 = (val == BCM_PORT_LINK_STATUS_UP) ?
                                         SAI_PORT_OPER_STATUS_UP : 
                                         SAI_PORT_OPER_STATUS_DOWN;
                break;
            case SAI_PORT_ATTR_HW_LANE_LIST:
            {
                int l, lanes, pp, limit;
                
                if ((0 == BRCM_SAI_ATTR_LIST_OBJ_COUNT(i)) || (0 == port))
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }
                
                rv = _brcm_get_hardware_lane_count(0, port, &lanes);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                if (rv == SAI_STATUS_SUCCESS)
                {
                    limit = lanes;
                    if (attr_list[i].value.u32list.count < lanes)
                    {
                        limit = attr_list[i].value.u32list.count;
                        rv = SAI_STATUS_BUFFER_OVERFLOW;
                    }
                    attr_list[i].value.u32list.count = lanes;
                    pp = driverA2BGet(port);
                    for (l=0; l<limit; l++)
                    {
                        attr_list[i].value.u32list.list[l] = pp + l;
                    }
                }
                break;
            }
            case SAI_PORT_ATTR_SUPPORTED_BREAKOUT_MODE_TYPE:
            case SAI_PORT_ATTR_CURRENT_BREAKOUT_MODE_TYPE:
                rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
                break;
            case SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES:
                /* Note: this is device dependent */
                attr_list[i].value.u32 = _BRCM_SAI_IS_CPU_PORT(port) ? 
                                         _brcm_sai_get_port_max_queues(TRUE) : 
                                         _brcm_sai_get_port_max_queues(FALSE); /* Includes UC + MC for non cpu ports */
                break;
            case SAI_PORT_ATTR_QOS_QUEUE_LIST:
            {
                sai_status_t rv1;
                _brcm_sai_indexed_data_t data;
                int idx[2], type, q, numq, limit, numq_uc;

                /* NOTE: We assume that the host adapter will always query the 
                 *       port queue list. Thus we are using this call to populate
                 *       the port queue object map, which will be used later.
                 */
                numq = _BRCM_SAI_IS_CPU_PORT(port) ? 
                           _brcm_sai_get_port_max_queues(TRUE) :
                           _brcm_sai_get_port_max_queues(FALSE);
                numq_uc = _brcm_sai_get_num_queues();
                
                if (0 == BRCM_SAI_ATTR_LIST_OBJ_COUNT(i))
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }
                limit = numq - 1;
                /* Note: this could be device dependent */
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < numq)
                {
                    limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i)-1;
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = numq; /* Includes UC + MC queues for non CPU ports */

                for (q=0; q<=limit; q++) 
                {
                    if (_BRCM_SAI_IS_CPU_PORT(port) == TRUE)
                    {
                        type = SAI_QUEUE_TYPE_MULTICAST;
                    }
                    else
                    {
                        if (q < numq_uc)
                        {
                            type = SAI_QUEUE_TYPE_UNICAST;
                        }
                        else
                        {
                            type = SAI_QUEUE_TYPE_MULTICAST;
                        }
                    }
                    /* Using q+1 to have non-zero object-ids */
                    BRCM_SAI_ATTR_LIST_OBJ_LIST(i, (q)) = 
                      BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_QUEUE,
                                                  type, port, (q+1)); 
                    idx[0] = port;
                    idx[1] = q;
                    data.port_qid.idx1 = idx[0];
                    data.port_qid.idx2 = idx[1];
                    data.port_qid.qoid = BRCM_SAI_ATTR_LIST_OBJ_LIST(i, q);
                    rv1 = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_PORT_QID, 
                                                     idx, &data);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qid data set", rv1);
                }
                break;
            }
            case SAI_PORT_ATTR_QOS_NUMBER_OF_SCHEDULER_GROUPS:
                /* Note: determine count per device */
                attr_list[i].value.u32 = _brcm_sai_get_num_scheduler_groups(port);
                break;
            case SAI_PORT_ATTR_QOS_SCHEDULER_GROUP_LIST:
            {
                /* Note: this is device specific */
                bcm_gport_t gport;
                _brcm_sai_table_data_t data;
                _brcm_sai_scheduler_group_t sched_group;
                sai_object_id_t oids[TOTAL_SCHED_NODES_PER_PORT];
                int n, p, total, count, offset = 1, limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i);
                
                if (0 == limit)
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }

                total = _brcm_sai_get_num_scheduler_groups(port);
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < total)
                {
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < total ? 
                        BRCM_SAI_ATTR_LIST_OBJ_COUNT(i)-1 : total-1;
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = total;
                /* Fetch root gport and prepare object */
                gport = _brcm_sai_switch_port_queue_get(port, 0,
                                                        _BRCM_SAI_PORT_SCHEDULER_TYPE);
                if (-1 == gport)
                {
                    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                    rv = SAI_STATUS_FAILURE;
                    break;
                }
                oids[0] = 
                    BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SCHEDULER_GROUP,
                                                _BRCM_SAI_PORT_SCHEDULER_TYPE,
                                                port, gport);
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_DEBUG, "Scheduler group child "
                                  "port:%d, root obj:0x%8x\n", port, gport);
                
                /* Get L0 */
                count = _brcm_sai_get_num_l0_nodes(port);
                if (DEV_IS_TD2() || DEV_IS_HX4())
                {
                    for (n=0; n<count; n++)
                    {
                        gport = _brcm_sai_switch_port_queue_get(port, n,
                                                                _BRCM_SAI_L0_SCHEDULER_TYPE);
                        if (-1 == gport)
                        {
                            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                            rv = SAI_STATUS_FAILURE;
                            break;
                        }
                        oids[offset+n] = 
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SCHEDULER_GROUP,
                                                        (_BRCM_SAI_L0_SCHEDULER_TYPE | n<<3),
                                                        port, gport);
                        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_DEBUG, "Scheduler group child[%d] "
                                          "port:%d, level 0, obj:0x%8x\n",
                                          n, port, gport);
                        DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
                        sched_group.port = port;
                        sched_group.level = _BRCM_SAI_L0_SCHEDULER_TYPE;
                        sched_group.index = n;
                        data.sched_group = &sched_group;
                        /* Lookup current id */
                        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
                        {
                            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry lookup.", rv);
                        }
                        sched_group.parent_oid = oids[0];
                        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry add.", rv);
                    }
                    if (SAI_STATUS_ERROR(rv))
                    {
                        break;
                    }
                    /* Get L1 */
                    offset = count+1;
                    count = _brcm_sai_get_num_l1_nodes(port);
                    for (n=0; n<count; n++)
                    {
                        gport = _brcm_sai_switch_port_queue_get(port, n,
                                                                _BRCM_SAI_L1_SCHEDULER_TYPE);
                        if (-1 == gport)
                        {
                            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                            rv = SAI_STATUS_FAILURE;
                            break;
                        }
                        oids[offset+n] =
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SCHEDULER_GROUP,
                                                        (_BRCM_SAI_L1_SCHEDULER_TYPE | n<<3),
                                                        port, gport);
                        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_DEBUG, "Scheduler group child[%d] "
                                          "port:%d, level 1, obj:0x%8x\n",
                                          n, port, gport);
                        DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
                        sched_group.port = port;
                        sched_group.level = _BRCM_SAI_L1_SCHEDULER_TYPE;
                        sched_group.index = n;
                        data.sched_group = &sched_group;
                        /* Lookup current id */
                        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
                        {
                            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry lookup.", rv);
                        }
                        p = _BRCM_SAI_IS_CPU_PORT(port) ? 1 : n > 7 ? 2 : 1;
                        sched_group.parent_oid = oids[p];
                        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry add.", rv);
                    }
                    if (SAI_STATUS_ERROR(rv))
                    {
                        break;
                    }
                }
                else /* TH */
                {
                    for (n=0; n<count; n++)
                    {
                        gport = _brcm_sai_switch_port_queue_get(port, n,
                                                                _BRCM_SAI_L0_SCHEDULER_TYPE);
                        if (-1 == gport)
                        {
                            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                            rv = SAI_STATUS_FAILURE;
                            break;
                        }
                        oids[offset+n] = 
                            BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_SCHEDULER_GROUP,
                                                        (_BRCM_SAI_L0_SCHEDULER_TYPE | n<<3),
                                                        port, gport);
                        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_DEBUG, "Scheduler group child[%d] "
                                          "port:%d, level 0, obj:0x%8x\n",
                                          n, port, gport);
                        DATA_CLEAR(sched_group, _brcm_sai_scheduler_group_t);
                        sched_group.port = port;
                        sched_group.level = _BRCM_SAI_L0_SCHEDULER_TYPE;
                        sched_group.index = n;
                        data.sched_group = &sched_group;
                        /* Lookup current id */
                        rv = _brcm_sai_db_table_entry_lookup(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        if (SAI_STATUS_SUCCESS != rv && SAI_STATUS_ITEM_NOT_FOUND != rv)
                        {
                            BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry lookup.", rv);
                        }
                        sched_group.parent_oid = oids[0];
                        rv = _brcm_sai_db_table_entry_add(_BRCM_SAI_TABLE_SCHED_GRP, &data);
                        BRCM_SAI_RV_CHK(SAI_API_SCHEDULER_GROUP, "Sched group DB table entry add.", rv);
                    }
                    if (SAI_STATUS_ERROR(rv))
                    {
                        break;
                    }
                }
                for (n=0; n<=limit; n++)
                {
                    BRCM_SAI_ATTR_LIST_OBJ_LIST(i, n) = oids[n];
                }
                break;
            }
            case SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS:
                attr_list[i].value.u32 = 8;
                break;
            case SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST:
            {
                int pg, limit = 8;
                
                if (0 == BRCM_SAI_ATTR_LIST_OBJ_COUNT(i))
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                    break;
                }
                if (BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) < limit)
                {
                    limit = BRCM_SAI_ATTR_LIST_OBJ_COUNT(i)-1;
                    rv = SAI_STATUS_BUFFER_OVERFLOW;
                }
                
                BRCM_SAI_ATTR_LIST_OBJ_COUNT(i) = 8;
                for (pg=0; pg<limit; pg++) /* Starting with 1 to have non-zero object-ids */
                {
                    BRCM_SAI_ATTR_LIST_OBJ_LIST(i, pg) = 
                        BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_INGRESS_PRIORITY_GROUP,
                                                    0, port, (pg+1));
                }
                break;
            }
            case SAI_PORT_ATTR_SPEED:
                rv = bcm_port_speed_get(0, port, &val);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                attr_list[i].value.u32 = val;
                break;
            case SAI_PORT_ATTR_FULL_DUPLEX_MODE:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0+i;
                break;
            case SAI_PORT_ATTR_AUTO_NEG_MODE:
                rv = bcm_port_autoneg_get(0, port, &val);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                attr_list[i].value.booldata = val;
                break;
            case SAI_PORT_ATTR_ADMIN_STATE:
                rv = bcm_port_enable_get(0, port, &val);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                attr_list[i].value.booldata = val;
                break;
            case SAI_PORT_ATTR_MEDIA_TYPE:
            case SAI_PORT_ATTR_SUPPORTED_SPEED:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0+i;
                break;
            case SAI_PORT_ATTR_PORT_VLAN_ID:
            {
                bcm_vlan_t vid;
                rv = bcm_port_untagged_vlan_get(0, port, &vid);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                attr_list[i].value.oid = BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_VLAN, vid);
                break;
            }
            case SAI_PORT_ATTR_DEFAULT_VLAN_PRIORITY:
            {
                int pri;
                rv = bcm_port_untagged_priority_get(0, port, &pri);
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                attr_list[i].value.u8 = pri;
                break;
            }
            case SAI_PORT_ATTR_DROP_UNTAGGED:
            case SAI_PORT_ATTR_DROP_TAGGED:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0+i;
                break;
            case SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE:
                rv = _brcm_sai_get_port_attribute(port_id, &attr_list[i]);
                break;
            case SAI_PORT_ATTR_UPDATE_DSCP:
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0+i;
                break;
            case SAI_PORT_ATTR_MTU:
                rv = bcm_port_frame_max_get(0, port, 
                                                (int*)&(attr_list[i].value.u32));
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                break;
            case SAI_PORT_ATTR_FLOOD_STORM_CONTROL_POLICER_ID:
            case SAI_PORT_ATTR_BROADCAST_STORM_CONTROL_POLICER_ID:
            case SAI_PORT_ATTR_MULTICAST_STORM_CONTROL_POLICER_ID:
            case SAI_PORT_ATTR_GLOBAL_FLOW_CONTROL_MODE:
                rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0+i;
                break;
            case SAI_PORT_ATTR_FEC_MODE:
            {
                /* Only 1 FEC mode will be active at any time or off */
                uint32 val;
                attr_list[i].value.u32 = SAI_PORT_FEC_MODE_NONE;
                rv = bcm_port_phy_control_get(0, port, 
                                              BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION, 
                                              &val);
                if (BCM_SUCCESS(rv) && 
                    (val ==  BCM_PORT_PHY_CONTROL_FEC_ON))
                {
                    attr_list[i].value.u32 = SAI_PORT_FEC_MODE_FC ;
                }
                else
                {
                    /* If FEC is returned as off or error is seen, check cl91 */
                    rv = bcm_port_phy_control_get(0, port, 
                                                  BCM_PORT_PHY_CONTROL_FORWARD_ERROR_CORRECTION_CL91, 
                                                  &val);
                    if (BCM_SUCCESS(rv) &&
                        (val == BCM_PORT_PHY_CONTROL_FEC_ON))
                    {
                        attr_list[i].value.u32 = SAI_PORT_FEC_MODE_RS ;
                    }
                }
                BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                          attr_list[i].id);
                break;
            }
            case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL:
            {
                bcm_port_priority_group_config_t prigrp_config;
                int j;

                memset(&prigrp_config, 0, sizeof(prigrp_config));
                attr_list[i].value.u8 = 0;
                for (j = 0; j < 8; j++)
                {
                    prigrp_config.pfc_transmit_enable = 0;
                    rv = bcm_port_priority_group_config_get(0, port, j, &prigrp_config);
                    BRCM_SAI_API_CHK(SAI_API_PORT, "PFC get", rv);
                    if (1 == prigrp_config.pfc_transmit_enable)
                    {
                        attr_list[i].value.u8 |= (1 << j);
                    }
                }
              break;
            }
            default:
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                                  "Unknown port attribute %d passed\n",
                                  attr_list[i].id);
                rv = SAI_STATUS_ATTR_NOT_SUPPORTED_0+i;
                break;
        }
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_INFO,
                              "Error processing port attributes\n");
            return rv;
        }
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_PORT);
    return rv;
#undef _GET_PORT
}

/*
* Routine Description:
*   Get port statistics counters.
*
* Arguments:
*    [in] port_id - port id
*    [in] number_of_counters - number of counters in the array
*    [in] counter_ids - specifies the array of counter ids
*    [out] counters - array of resulting counter values.
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_get_port_stats(_In_ sai_object_id_t port_id,
                        _In_ uint32_t number_of_counters,
                        _In_ const sai_port_stat_t *counter_ids,
                        _Out_ uint64_t* counters)
{
    int i, j, counter_idx;
    sai_status_t rv;
    bcm_port_t port;
    bcm_stat_val_t *stats;
    struct {
        sai_port_stat_t stat;
        int offset;
    } skip_stats[] = {
        { SAI_PORT_STAT_IP_IN_OCTETS, -1 },
        { SAI_PORT_STAT_IP_OUT_OCTETS, -1 },
        { SAI_PORT_STAT_IPV6_IN_OCTETS, -1 },
        { SAI_PORT_STAT_IPV6_OUT_OCTETS, -1 }
    };
    bool request_tx_count = false;
    uint64 tx_count;
    struct {
        sai_port_stat_t stat;
        int offset;
    } pfc_onoff_stats[] = {
        { SAI_PORT_STAT_PFC_0_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_1_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_2_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_3_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_4_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_5_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_6_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_7_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_0_ON2OFF_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_1_ON2OFF_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_2_ON2OFF_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_3_ON2OFF_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_4_ON2OFF_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_5_ON2OFF_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_6_ON2OFF_RX_PKTS, -1 },
        { SAI_PORT_STAT_PFC_7_ON2OFF_RX_PKTS, -1 }
    };
    bcm_stat_val_t pfc_stats[16];
    uint64 pfc_counters[16];
    int num_pfc_counters = 0;
    int cosq_stats = 0; /* non multi stat counters */

    BRCM_SAI_FUNCTION_ENTER(SAI_API_PORT);
    BRCM_SAI_SWITCH_INIT_CHECK;

    stats = (bcm_stat_val_t*)
            ALLOC(number_of_counters * sizeof(bcm_stat_val_t));
    if (IS_NULL(stats))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_CRITICAL,
                          "Error allocating memory for port stats.\n");
        return SAI_STATUS_NO_MEMORY;
    }
    for(i=0; i<number_of_counters; i++)
    {
        if ((SAI_PORT_STAT_WRED_DROPPED_PACKETS == counter_ids[i]) ||
            (SAI_PORT_STAT_WRED_DROPPED_BYTES == counter_ids[i]))
        {
            /* Need to accumulate counters from UC queues on port */
            sai_status_t rv1;
            bcm_port_t port;
            bcm_gport_t gport;
            uint64 port_stat[3];

            port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, port_id);

            rv = bcm_port_gport_get(0, port, &gport);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "port gport get", rv);

            COMPILER_64_ZERO(port_stat[0]);
            COMPILER_64_ZERO(port_stat[1]);
            COMPILER_64_ZERO(port_stat[2]);
            /* need to use cosq stat calls for port stats cosq = -1 */
            if (SAI_PORT_STAT_WRED_DROPPED_PACKETS == counter_ids[i])
            {
                rv1 = bcm_cosq_stat_get(0, gport, -1,
                                        bcmCosqStatGreenDiscardDroppedPackets,
                                        &port_stat[0]);
                if (BCM_E_NONE == rv1)
                {
                    COMPILER_64_ADD_64(counters[i], port_stat[0]);
                }
                rv1 = bcm_cosq_stat_get(0, gport, -1,
                                        bcmCosqStatYellowDiscardDroppedPackets,
                                        &port_stat[1]);
                if (BCM_E_NONE == rv1)
                {
                    COMPILER_64_ADD_64(counters[i], port_stat[1]);
                }
                rv1 = bcm_cosq_stat_get(0, gport, -1,
                                        bcmCosqStatRedDiscardDroppedPackets,
                                        &port_stat[2]);
                if (BCM_E_NONE == rv1)
                {
                    COMPILER_64_ADD_64(counters[i], port_stat[2]);
                }
                cosq_stats++;
            }
            else
            {
                rv1 = bcm_cosq_stat_get(0, gport, -1,
                                        bcmCosqStatGreenDiscardDroppedBytes,
                                        &port_stat[0]);
                if (BCM_E_NONE == rv1)
                {
                    COMPILER_64_ADD_64(counters[i], port_stat[0]);
                }
                rv1 = bcm_cosq_stat_get(0, gport, -1,
                                        bcmCosqStatYellowDiscardDroppedBytes,
                                        &port_stat[1]);
                if (BCM_E_NONE == rv1)
                {
                    COMPILER_64_ADD_64(counters[i], port_stat[1]);
                }
                rv1 = bcm_cosq_stat_get(0, gport, -1,
                                        bcmCosqStatRedDiscardDroppedBytes,
                                        &port_stat[2]);
                if (BCM_E_NONE == rv1)
                {
                    COMPILER_64_ADD_64(counters[i], port_stat[2]);
                }
                cosq_stats++;
            }
            
            if (BCM_E_NONE != rv1)
            {
                /* non-fatal failure */
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_DEBUG,
                                  "Failure in getting WRED value for port %d\n",
                                  port);
            }

            continue;            
        }

        if (SAI_PORT_STAT_ETHER_STATS_PKTS_1519_TO_2047_OCTETS == counter_ids[i])
        {
            /* If the requested counter is the Rx/Tx version of the
             * 1519_to_2047 counter, we are only getting the "In" version
             * in the "multi get", below. We need to get the "Out"
             * version separately as well.
             */
            request_tx_count = true;
            counter_idx = i;
        }
        rv = BRCM_STAT_SAI_TO_BCM(counter_ids[i], &stats[i]);
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_INFO,
                              "Unsupported or unimplemented stat[%d] type: %d.\n", i, counter_ids[i]);
            return rv;
        }
        for(j=0; j<COUNTOF(skip_stats); j++)
        {
            if(counter_ids[i] == skip_stats[j].stat)
            {
                skip_stats[j].offset = i;
                break;
            }
        }
        if ((counter_ids[i] >= pfc_onoff_stats[0].stat) &&
            (counter_ids[i] <= pfc_onoff_stats[COUNTOF(pfc_onoff_stats)-1].stat))
        {
            for(j=0; j<COUNTOF(pfc_onoff_stats); j++)
            {
                if(counter_ids[i] == pfc_onoff_stats[j].stat)
                {
                    pfc_onoff_stats[j].offset = i;
                    BRCM_STAT_SAI_TO_BCM(counter_ids[i], &pfc_stats[num_pfc_counters++]);
                    break;
                }
            }
        }
    } /* ends for */

    number_of_counters-= cosq_stats;    
    if (number_of_counters > 0)
    {
        port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, port_id);
        /* See if app is only requesting PFC counters. */
        if (number_of_counters == num_pfc_counters)
        {
            if (!DEV_IS_HX4())
            {
                rv = bcm_stat_direct_multi_get(0, port, number_of_counters, stats,
                                               (uint64*)counters);
                BRCM_SAI_API_CHK(SAI_API_PORT, "Multi stats direct get", rv);
            }
            else
            {
                rv = bcm_stat_sync_multi_get(0, port, number_of_counters, stats,
                                             (uint64*)counters);
                BRCM_SAI_API_CHK(SAI_API_PORT, "Multi stats sync get", rv);
            }
        }
        else 
        {
            /* If the following test passes, app has requested 
             * a mixed set of PFC and non-PFC counters. Ideally, 
             * this will not occur.
             */
            if (number_of_counters > num_pfc_counters)
            {
                rv = bcm_stat_multi_get(0, port, number_of_counters, stats,
                                        (uint64*)counters);
                BRCM_SAI_API_CHK_FREE(SAI_API_PORT, "Multi stats get", rv, stats);
                if (TRUE == request_tx_count)
                {
                    rv = _brcm_sai_stat_get(port, 1, &tx_count);
                    BRCM_SAI_RV_CHK_FREE(SAI_API_PORT, "Stat get", rv, stats);
                    counters[counter_idx] += tx_count;
                }
            }
            if (num_pfc_counters)
            {
                if (!DEV_IS_HX4())
                {
                    rv = bcm_stat_direct_multi_get(0, port, num_pfc_counters, pfc_stats,
                                                   pfc_counters);
                    BRCM_SAI_API_CHK(SAI_API_PORT, "Multi stats direct get", rv);
                }
                else
                {
                    rv = bcm_stat_sync_multi_get(0, port, number_of_counters, stats,
                                                 (uint64*)counters);
                    BRCM_SAI_API_CHK(SAI_API_PORT, "Multi stats sync get", rv);
                }
                num_pfc_counters = 0;
                for(i=0; i<COUNTOF(pfc_onoff_stats); i++)
                {
                    if (pfc_onoff_stats[i].offset >= 0)
                    {
                        counters[pfc_onoff_stats[i].offset] = pfc_counters[num_pfc_counters++];
                    }
                }
            }
            for(i=0; i<COUNTOF(skip_stats); i++)
            {
                if (skip_stats[i].offset >= 0)
                {
                    counters[skip_stats[i].offset] = 0;
                }
            }
        }
    }

    CHECK_FREE(stats);
    BRCM_SAI_FUNCTION_EXIT(SAI_API_PORT);
    return rv;
}

/*
* Routine Description:
*   Clear port statistics counters.
*
* Arguments:
*    [in] port_id - port id
*    [in] number_of_counters - number of counters in the array
*    [in] counter_ids - specifies the array of counter ids
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
STATIC sai_status_t
brcm_sai_clear_port_stats(_In_ sai_object_id_t port_id,
                          _In_ uint32_t number_of_counters,
                          _In_ const sai_port_stat_t *counter_ids)
{
    int i;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_port_t port;
    bcm_stat_val_t stat;


    BRCM_SAI_FUNCTION_ENTER(SAI_API_PORT);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_SET_OBJ_STATS_PARAM_CHK(port_id, SAI_OBJECT_TYPE_PORT);
    
    port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, port_id);
    for(i=0; i<number_of_counters; i++)
    {
        switch(counter_ids[i])
        {
            case SAI_PORT_STAT_IN_WATERMARK_BYTES:
            case SAI_PORT_STAT_IN_SHARED_WATERMARK_BYTES:
            case SAI_PORT_STAT_OUT_WATERMARK_BYTES:
            case SAI_PORT_STAT_OUT_SHARED_WATERMARK_BYTES:
                rv = SAI_STATUS_NOT_IMPLEMENTED;
                break;

            default:
                rv = SAI_STATUS_NOT_IMPLEMENTED;
                break;
        }

        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_MMU_BUFFER(SAI_LOG_LEVEL_INFO,
                                    "Error clearing port stat\n");
            return rv;
        }

        rv = BRCM_STAT_SAI_TO_BCM(counter_ids[i], &stat);
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                              "Unknown stat[%d] type: %d.\n", i, counter_ids[i]);
            return rv;
        }

        rv = bcm_stat_clear_single(0, port, stat);
        BRCM_SAI_API_CHK(SAI_API_PORT, "Port stats clear", rv);
    }


    BRCM_SAI_FUNCTION_EXIT(SAI_API_PORT);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
sai_status_t
_brcm_sai_alloc_port_info()
{
    sai_status_t rv;

    if (DEV_IS_TD2())
    {
        BCM_PBMP_CLEAR(_rx_los_pbm);
    }
    rv =  _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_PORT_QID,
                                       _BRCM_SAI_MAX_PORTS, 
                                       _brcm_sai_get_max_queues());
    BRCM_SAI_RV_LVL_CHK(SAI_API_PORT, SAI_LOG_LEVEL_CRITICAL,
                        "initializing port qid data", rv);
    rv =  _brcm_sai_indexed_data_init(_BRCM_SAI_INDEXED_PORT_INFO,
                                      _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_PORT, SAI_LOG_LEVEL_CRITICAL,
                        "initializing port info data", rv);
    rv =  _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_PORT_BUF_PROF,
                                       2, _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_PORT, SAI_LOG_LEVEL_CRITICAL,
                        "initializing port buff profile data", rv);

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_port_info()
{
    sai_status_t rv;

    rv =  _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_PORT_QID,
                                       _BRCM_SAI_MAX_PORTS, 
                                       _brcm_sai_get_max_queues());
    BRCM_SAI_RV_LVL_CHK(SAI_API_PORT, SAI_LOG_LEVEL_CRITICAL,
                        "freeing port qid data", rv);
    rv = _brcm_sai_indexed_data_free1(_BRCM_SAI_INDEXED_PORT_INFO, 
                                      0, _BRCM_SAI_MAX_PORTS, -1);
    BRCM_SAI_RV_LVL_CHK(SAI_API_PORT, SAI_LOG_LEVEL_CRITICAL,
                        "freeing port info data", rv);
    rv =  _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_PORT_BUF_PROF,
                                       2, _BRCM_SAI_MAX_PORTS);
    BRCM_SAI_RV_LVL_CHK(SAI_API_PORT, SAI_LOG_LEVEL_CRITICAL,
                        "freeing port buff profile data", rv);

    return SAI_STATUS_SUCCESS;
}

STATIC sai_status_t
_brcm_sai_port_dot1p_tc_map_set(int unit, int port, uint32_t map_id)
{
    int i;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_ingress_map_t map_list;
    bcm_gport_t gport;
    sai_uint32_t flags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L2;
    int qmapid;
    bcm_qos_map_t qosmap_array[_BRCM_SAI_MAX_QOS_MAPS];
    bool color_map_found = FALSE;
    
    if (SAI_STATUS_SUCCESS != _brcm_sai_ingress_qosmap_get(SAI_QOS_MAP_TYPE_DOT1P_TO_TC,
                                                           map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get ingress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (0 == map_id)
    {
        BCM_GPORT_MODPORT_SET(gport, 0, port);
        rv = bcm_qos_port_map_type_get(0, gport, flags, &qmapid);
        if (BCM_E_NONE == rv)
        {      
            rv = _brcm_sai_port_qos_map_array_get(qmapid, qosmap_array);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map array get", rv);
            /* Check to see if there are any non-default color mappings.
             * If so, just set mappings to default; otherwise, can destroy
             * the map.
             */
            for (i=0; i<8; i++)
            {
                if (bcmColorGreen != qosmap_array[i*2].color)
                {
                    color_map_found = TRUE;
                    break;
                }
            }
            if (TRUE == color_map_found)
            {
                for (i=0; i<8; i++)
                {
                    rv = _brcm_sai_port_qos_map_add(qmapid, i, i, qosmap_array[i*2].color);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map add", rv);
                }
                /* Apply mappings to the port */
                rv = bcm_qos_port_map_set(0, gport, qmapid, -1);
                BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map set", rv);
            }
            else
            {
                /* Set tc/color mappings to default */
                rv = bcm_qos_port_map_set(0, gport, 0, -1);
                BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map set", rv);
                /* Release hardware resources for QoS mapping */
                rv = bcm_qos_map_destroy(0, qmapid);
                BRCM_SAI_API_CHK(SAI_API_PORT, "qos map destroy", rv);
            }
        }
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Ingress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<map_list.count; i++)
    {
        rv = __brcm_sai_port_dot1p_tc_map_set(port, map_list.map[i].key.dot1p,
                                              map_list.map[i].value.tc);
        BRCM_SAI_RV_CHK(SAI_API_PORT, "map set", rv);
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_port_dot1p_color_map_set(int unit, int port, uint32_t map_id)
{
    int i;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_ingress_map_t map_list;
    bcm_gport_t gport;
    sai_uint32_t flags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L2;
    int qmapid;
    bcm_qos_map_t qosmap_array[_BRCM_SAI_MAX_QOS_MAPS];
    bool tc_map_found = FALSE;
    
    if (SAI_STATUS_SUCCESS != _brcm_sai_ingress_qosmap_get(SAI_QOS_MAP_TYPE_DOT1P_TO_COLOR,
                                                           map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get ingress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (0 == map_id)
    {
        BCM_GPORT_MODPORT_SET(gport, 0, port);
        rv = bcm_qos_port_map_type_get(0, gport, flags, &qmapid);
        if (BCM_E_NONE == rv)
        {      
            rv = _brcm_sai_port_qos_map_array_get(qmapid, qosmap_array);
            BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map array get", rv);
            /* Check to see if there are any non-default tc mappings.
             * If so, just set mappings to default; otherwise, can destroy
             * the map.
             */
            for (i=0; i<8; i++)
            {
                if (i != qosmap_array[i*2].int_pri)
                {
                    tc_map_found = TRUE;
                    break;
                }
            }
            if (TRUE == tc_map_found)
            {
                for (i=0; i<8; i++)
                {
                    rv = _brcm_sai_port_qos_map_add(qmapid, i, qosmap_array[i*2].int_pri, bcmColorGreen);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map add", rv);
                }
                /* Apply mappings to the port */
                rv = bcm_qos_port_map_set(0, gport, qmapid, -1);
                BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map set", rv);
            }
            else
            {
                /* Set tc/color mappings to default */
                rv = bcm_qos_port_map_set(0, gport, 0, -1);
                BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map set", rv);
                /* Release hardware resources for QoS mapping */
                rv = bcm_qos_map_destroy(0, qmapid);
                BRCM_SAI_API_CHK(SAI_API_PORT, "qos map destroy", rv);
            }
        }
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Ingress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<map_list.count; i++)
    {
        rv = __brcm_sai_port_dot1p_color_map_set(port, map_list.map[i].key.dot1p,
                                                 map_list.map[i].value.color);
        BRCM_SAI_RV_CHK(SAI_API_PORT, "map set", rv);
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_port_dscp_tc_map_set(int unit, int port, uint32_t map_id)
{
    int i, mapcp, pri;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_ingress_map_t map_list;

    if (SAI_STATUS_SUCCESS != _brcm_sai_ingress_qosmap_get(SAI_QOS_MAP_TYPE_DSCP_TO_TC,
                                                           map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get ingress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* FIXME - For now, handle a NULL map_id. But need to
     * map back to defaults in future release.
     */
    if (0 == map_id)
    {
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Ingress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    rv = bcm_port_dscp_map_mode_set(0, port, BCM_PORT_DSCP_MAP_ALL);
    BRCM_SAI_API_CHK(SAI_API_PORT, "port dscp map mode set", rv);
    for (i=0; i<map_list.count; i++)
    {
        rv = bcm_port_dscp_map_get(0, port, map_list.map[i].key.dscp,
                                       &mapcp, &pri);
        BRCM_SAI_API_CHK(SAI_API_PORT, "dscp map get", rv);
        rv = bcm_port_dscp_map_set(0, port, map_list.map[i].key.dscp,
                                       map_list.map[i].key.dscp,
                                       map_list.map[i].value.tc | (pri & 0xff));
        BRCM_SAI_API_CHK(SAI_API_PORT, "dscp map set", rv);
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_port_dscp_color_map_set(int unit, int port, uint32_t map_id)
{
    int i, mapcp, pri;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_ingress_map_t map_list;

    if (SAI_STATUS_SUCCESS != _brcm_sai_ingress_qosmap_get(SAI_QOS_MAP_TYPE_DSCP_TO_COLOR,
                                                           map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get ingress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* FIXME - For now, handle a NULL map_id. But need to
     * map back to defaults in future release.
     */
    if (0 == map_id)
    {
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Ingress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<map_list.count; i++)
    {
        rv = bcm_port_dscp_map_get(0, port, map_list.map[i].key.dscp,
                                       &mapcp, &pri);
        BRCM_SAI_API_CHK(SAI_API_PORT, "dscp map get", rv);
        pri &= 0xff;
        switch (map_list.map[i].value.color)
        {
            case SAI_PACKET_COLOR_GREEN:
                /* Default color GREEN is 0. */
                pri |= 0;
                break;
            case SAI_PACKET_COLOR_YELLOW:
                pri |= BCM_PRIO_YELLOW;
                break;
            case SAI_PACKET_COLOR_RED:
                pri |= BCM_PRIO_RED;
                break;
            default:
                break;
        }
        rv = bcm_port_dscp_map_set(0, port, map_list.map[i].key.dscp,
                                       map_list.map[i].key.dscp, pri);
        BRCM_SAI_API_CHK(SAI_API_PORT, "dscp map set", rv);
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_port_tc_queue_map_set(int unit, int port, uint32_t map_id)
{
    int i, idx[2], qidx;
    uint32 flags;
    bcm_gport_t qgport;
    _brcm_sai_indexed_data_t data;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_egress_map_t map_list;
    int num_queues = _brcm_sai_get_num_queues();
    
    if (-1 == _brcm_sai_egress_qosmap_get(SAI_QOS_MAP_TYPE_TC_TO_QUEUE, map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get egress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* FIXME - For now, handle a NULL map_id. But need to 
     * map back to defaults in future release.
     */
    if (0 == map_id)
    {
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Egress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    idx[0] = port;
    for (i=0; i<map_list.count; i++)
    {
        flags = 0;
        qidx = map_list.map[i].value.queue_index;
        idx[1] = qidx;
        rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_QID,
                                        idx, &data);
        BRCM_SAI_RV_CHK(SAI_API_PORT, "port qid data get", rv);
        if (SAI_QUEUE_TYPE_UNICAST ==
            BRCM_SAI_GET_OBJ_SUB_TYPE(data.port_qid.qoid))
        {
            flags |= BCM_COSQ_GPORT_UCAST_QUEUE_GROUP;
            qgport = _brcm_sai_switch_port_queue_get(port, qidx,
                                                     _BRCM_SAI_QUEUE_TYPE_UCAST);
            if (-1 == qgport)
            {
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                return SAI_STATUS_FAILURE;
            }
        }
        else if (SAI_QUEUE_TYPE_MULTICAST ==
                 BRCM_SAI_GET_OBJ_SUB_TYPE(data.port_qid.qoid))
        {
            flags |= BCM_COSQ_GPORT_MCAST_QUEUE_GROUP;
            qgport = _brcm_sai_switch_port_queue_get(port, qidx-num_queues,
                                                     _BRCM_SAI_QUEUE_TYPE_MULTICAST);
            if (-1 == qgport)
            {
                BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                return SAI_STATUS_FAILURE;
            }
        }
        if (!DEV_IS_TD2())
        {
            /* TH: takes the cos/queue value instead of -1 */
            rv = bcm_cosq_gport_mapping_set(0, port, map_list.map[i].key.tc,
                                            flags, qgport, qidx);
        }
        else
        {
            rv = bcm_cosq_gport_mapping_set(0, port, map_list.map[i].key.tc,
                                            flags, qgport, -1);
        }

        BRCM_SAI_API_CHK(SAI_API_PORT, "cosq port map set", rv);
        rv = _brcm_sai_port_update_queue_tc_mapping(port, qidx, qgport);
        BRCM_SAI_API_CHK(SAI_API_PORT, "update port queue tc mapping", rv);
    }

    return rv;
}

sai_status_t
_brcm_sai_port_update_queue_tc_mapping(int port, int qid, bcm_gport_t gport)
{
    sai_status_t rv;
    bcm_cos_t prio = 0;
    bcm_gport_t cur_gport;
    bcm_cos_queue_t cosq;
    int num = 0;
    for (prio = 0; prio < _BRCM_SAI_MAX_TC; prio++)
    {
        rv = bcm_cosq_gport_mapping_get(0, port, prio,
                BCM_COSQ_GPORT_UCAST_QUEUE_GROUP, &cur_gport, &cosq);
        BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq gport mapping get", rv);
        if (cur_gport == gport)
        {
           port_tc_queue_map_cache[port][qid].tc[num] = prio;
           num++;
        }

        port_tc_queue_map_cache[port][qid].size = num;
    }

    return rv;
}

STATIC sai_status_t
_brcm_sai_port_tc_color_dot1p_map_set(int unit, int port, uint32_t map_id)
{
    int i;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_egress_map_t map_list;
    
    if (-1 == _brcm_sai_egress_qosmap_get(SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DOT1P, 
                                          map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get egress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* FIXME - For now, handle a NULL map_id. But need to 
     * map back to defaults in future release.
     */
    if (0 == map_id)
    {
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Egress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<map_list.count; i++)
    {
        rv = __brcm_sai_port_tc_color_dot1p_map_set(port, map_list.map[i].key.tc,
                                                    map_list.map[i].key.color,
                                                    map_list.map[i].value.dot1p);
        BRCM_SAI_RV_CHK(SAI_API_PORT, "unmap set", rv);
    }
    return rv;
}

STATIC sai_status_t
_brcm_sai_port_tc_color_dscp_map_set(int unit, int port, uint32_t map_id)
{
    int i;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_egress_map_t map_list;
    
    if (-1 == _brcm_sai_egress_qosmap_get(SAI_QOS_MAP_TYPE_TC_AND_COLOR_TO_DSCP,
                                          map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get egress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* FIXME - For now, handle a NULL map_id. But need to 
     * map back to defaults in future release.
     */
    if (0 == map_id)
    {
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Egress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<map_list.count; i++)
    {
        rv = __brcm_sai_port_tc_color_dscp_map_set(port, map_list.map[i].key.tc,
                                                   map_list.map[i].key.color,
                                                   map_list.map[i].value.dscp);
        BRCM_SAI_RV_CHK(SAI_API_PORT, "unmap set", rv);
    }
    return rv;
}

STATIC sai_status_t
_brcm_sai_port_tc_pg_map_set(int unit, int port, uint32_t map_id)
{
    int i;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_egress_map_t map_list;
    
    if (-1 == _brcm_sai_egress_qosmap_get(SAI_QOS_MAP_TYPE_TC_TO_PRIORITY_GROUP,
                                          map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get egress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* FIXME - For now, handle a NULL map_id. But need to 
     * map back to defaults in future release.
     */
    if (0 == map_id)
    {
        return SAI_STATUS_SUCCESS;
    }
    if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Egress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<map_list.count; i++)
    {
        rv = driverTcPgMapSet(port, map_list.map[i].key.tc,
                              map_list.map[i].value.pg);
        BRCM_SAI_API_CHK(SAI_API_PORT, "tc to pg map set", rv);
    }
    return rv;
}

STATIC sai_status_t
_brcm_sai_port_pfc_queue_map_set(int unit, int port, uint32_t map_id, 
                                 uint8 pfc_class)
{
    int i, j;
    sai_status_t rv = SAI_STATUS_FAILURE;
    _brcm_sai_qos_egress_map_t map_list;
    bcm_gport_t qgport[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

    if (-1 == _brcm_sai_egress_qosmap_get(SAI_QOS_MAP_TYPE_PFC_PRIORITY_TO_QUEUE,
                                          map_id, &map_list))
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Failed to get egress qos map.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    /* If map has not been assigned, use default 1:1 mapping. */
    if (0 == map_id)
    {
        sal_memcpy(&map_list, &default_pfc_q_map, sizeof(_brcm_sai_qos_egress_map_t));
    }
    else if (FALSE == map_list.valid)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Egress qos map invalid.\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    for (i=0; i<8; i++)
    {
        for (j=0; j < map_list.count; j++)
        {
            if ((i == map_list.map[j].key.prio) && ((1<<i) & pfc_class))
            {
                qgport[i] = _brcm_sai_switch_port_queue_get(port, 
                             map_list.map[j].value.queue_index,
                             _BRCM_SAI_QUEUE_TYPE_UCAST);
                if (-1 == qgport[i])
                {
                    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                    return SAI_STATUS_FAILURE;
                }
            }
        }
    }
    rv = __brcm_sai_port_pfc_queue_map_set(port, qgport);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port pfc queue map set", rv);
    return rv;
}

sai_status_t 
_brcm_sai_port_ingress_buffer_config_set(uint8_t port, uint8_t pg, uint8_t pool_id,
                                         int max, int min, int resume)
{  
    sai_status_t rv;
    int idx[2] = { 0, port };
    _brcm_sai_indexed_data_t data;

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_BUF_PROF,
                                                idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port buf prof set", rv);

    if (TRUE == data.port_buff.prof_applied)
    {
        return rv;
    }
    if (-1 == max && !DEV_IS_THX())
    {
        max = driverPortPoolMaxLimitBytesMaxGet();
    }
    rv = driverPortSPSet(port, pool_id, 3, max);
    BRCM_SAI_API_CHK(SAI_API_PORT, "port SP Max Limit Set", rv);
    rv = driverPortSPSet(port, pool_id, 2, min);
    BRCM_SAI_API_CHK(SAI_API_PORT, "port SP Min Limit Set", rv);
    rv = driverPortSPSet(port, pool_id, 1, resume);
    BRCM_SAI_API_CHK(SAI_API_PORT, "port SP Resume Limit Set", rv);
    return rv;
}

sai_status_t 
_brcm_sai_port_egress_buffer_config_set(uint8_t port, uint8_t pool_id,
                                        int mode, int limit, int resume)
{
    sai_status_t rv;
    int idx[2] = { 1, port };
    _brcm_sai_indexed_data_t data;

    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_BUF_PROF,
                                                idx, &data);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port buf prof set", rv);

    if (TRUE == data.port_buff.prof_applied)
    {
        return rv;
    }    
    rv = driverEgressPortPoolLimitSet(port, pool_id, limit);
    BRCM_SAI_API_CHK(SAI_API_PORT, "port egress pool Limit Set", rv);
    if (SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC == mode)
    {
        rv = driverEgressPortPoolResumeSet(port, pool_id, resume);
        BRCM_SAI_API_CHK(SAI_API_PORT, "port egress pool Resume Set", rv);
    }
    return rv;
}

sai_status_t
_brcm_sai_port_set_rxlos(int port, int val)
{
    sai_status_t rv;
    rv = bcm_port_phy_control_set(0, port,
                                  BCM_PORT_PHY_CONTROL_SOFTWARE_RX_LOS, val);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port phy control set", rv);
    
    return BRCM_RV_BCM_TO_SAI(rv);
}

sai_status_t
_brcm_sai_port_sched_prof_set(_brcm_sai_qos_scheduler_t *scheduler,
                              bcm_gport_t scheduler_gport, bool all_cosq, int cos)
{
    sai_status_t rv;
    sai_uint32_t flags = 0;
        
    if (SAI_METER_TYPE_PACKETS == scheduler->shaper_type)
    {
        flags = BCM_COSQ_BW_PACKET_MODE;
    }
    if (all_cosq)
    {
        /* TH does not allow cosq=-1 when gport is used. 
         * Iterate over all applicable cosqs. 
         */
        for (cos = 0; cos < 8; cos++)
        {
            rv = bcm_cosq_gport_bandwidth_set(0, scheduler_gport, cos,
                                              (uint32)0,
                                              (uint32)scheduler->maximum_bandwidth,
                                              flags);
            BRCM_SAI_API_CHK(SAI_API_PORT, "cosq gport bandwidth set", rv);
        
            if (scheduler->maximum_burst)
            {
                rv = bcm_cosq_control_set(0, scheduler_gport, cos,
                                          bcmCosqControlBandwidthBurstMax,
                                          (uint32)scheduler->maximum_burst);
        
                BRCM_SAI_API_CHK(SAI_API_PORT, "cosq port control set "
                                 "bcmCosqControlBandwidthBurstMax", rv);
            }
        }
    }
    else
    {
        rv = bcm_cosq_gport_bandwidth_set(0, scheduler_gport, cos, 
                                          (uint32)0, 
                                          (uint32)scheduler->maximum_bandwidth,
                                          flags);
        BRCM_SAI_API_CHK(SAI_API_PORT, "cosq gport bandwidth set", rv);
        if (scheduler->maximum_burst)
        {
        
            rv = bcm_cosq_control_set(0, scheduler_gport, cos,
                                      bcmCosqControlBandwidthBurstMax,
                                      (uint32)scheduler->maximum_burst);
        
            BRCM_SAI_API_CHK(SAI_API_PORT, "cosq port control set "
                             "bcmCosqControlBandwidthBurstMax", rv);
        }
    }
    
    return BRCM_RV_BCM_TO_SAI(rv);
}

sai_status_t
__brcm_sai_port_dot1p_tc_map_set(int port, int lval, int rval)
{
    sai_status_t rv;
    bcm_gport_t gport;
    sai_uint32_t flags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L2;
    int qmapid;
    bcm_qos_map_t qosmap_array[_BRCM_SAI_MAX_QOS_MAPS];
    bcm_color_t color = bcmColorGreen;

    BCM_GPORT_MODPORT_SET(gport, 0, port);
    rv = bcm_qos_port_map_type_get(0, gport, flags, &qmapid);
    if (BCM_E_NOT_FOUND == rv)
    {
        /* Reserve hardware resources for QoS mapping */
        rv = bcm_qos_map_create(0, flags, &qmapid);
        BRCM_SAI_API_CHK(SAI_API_PORT, "qos map create", rv);
    }
    else
    {
        BRCM_SAI_API_CHK(SAI_API_PORT, "qos map type get", rv);
        rv = _brcm_sai_port_qos_map_array_get(qmapid, qosmap_array);
        BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map array get", rv);
        color = qosmap_array[lval*2].color;
    }
    rv = _brcm_sai_port_qos_map_add(qmapid, lval, rval, color);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map add", rv);
    /* Apply mapping to the port */
    rv = bcm_qos_port_map_set(0, gport, qmapid, -1);
    BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map set", rv);

    return rv;
}

sai_status_t
__brcm_sai_port_dot1p_color_map_set(int port, int lval, int rval)
{
    sai_status_t rv;
    bcm_gport_t gport;
    sai_uint32_t flags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L2;
    int qmapid = 0;
    bcm_qos_map_t qosmap_array[_BRCM_SAI_MAX_QOS_MAPS];
    int tc = 0;

    BCM_GPORT_MODPORT_SET(gport, 0, port);
    rv = bcm_qos_port_map_type_get(0, gport, flags, &qmapid);
    if (BCM_E_NOT_FOUND == rv)
    {
        /* Reserve hardware resources for QoS mapping */
        rv = bcm_qos_map_create(0, flags, &qmapid);
        BRCM_SAI_API_CHK(SAI_API_PORT, "qos map create", rv);
    }
    else
    {
        BRCM_SAI_API_CHK(SAI_API_PORT, "qos map type get", rv);
        rv = _brcm_sai_port_qos_map_array_get(qmapid, qosmap_array);
        BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map array get", rv);
        tc = qosmap_array[lval*2].int_pri;
    }
    rv = _brcm_sai_port_qos_map_add(qmapid, lval, tc, rval);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map add", rv);
    /* Apply mapping to the port */
    rv = bcm_qos_port_map_set(0, gport, qmapid, -1);
    BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map set", rv);

    return rv;
}

sai_status_t
__brcm_sai_port_tc_color_dot1p_map_set(int port, int int_pri, int color, 
                                       int pkt_pri)
{
    sai_status_t rv;
    bcm_qos_map_t egr_map;
    bcm_gport_t gport;
    sai_uint32_t flags = BCM_QOS_MAP_EGRESS | BCM_QOS_MAP_L2;
    int qmapid = 0;
    
    BCM_GPORT_MODPORT_SET(gport, 0, port);
    rv = bcm_qos_port_map_type_get(0, gport, flags, 
                                   &qmapid);
    if ((BCM_E_NONE != rv) &&
        ((BCM_E_NOT_FOUND == rv) ||
         (BCM_E_MEMORY == rv)))
      
    {
        /* Reserve hardware resources for QoS mapping */
        rv = bcm_qos_map_create(0, flags, &qmapid);
        BRCM_SAI_API_CHK(SAI_API_PORT, "qos map create", rv);
    }
    else
    {
        BRCM_SAI_API_CHK(SAI_API_PORT, "qos map type get", rv);
    }
    bcm_qos_map_t_init(&egr_map);
    egr_map.color = color;
    egr_map.int_pri = int_pri;

    egr_map.pkt_pri = pkt_pri;
    egr_map.pkt_cfi = (color == bcmColorGreen) ? 0 : 1;
    rv = bcm_qos_map_add(0, flags, &egr_map, qmapid);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map add", rv);

    egr_map.pkt_cfi = (color == bcmColorRed) ? 1 : 0;
    egr_map.color =   (color == bcmColorGreen) ? bcmColorRed : bcmColorGreen;
    rv = bcm_qos_map_add(0, flags, &egr_map, qmapid);
    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qos map add", rv);

    /* Apply mapping to the port */
    BCM_GPORT_MODPORT_SET(gport, 0, port);
    rv = bcm_qos_port_map_set(0, gport, -1, qmapid);
    BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map set", rv);
    return rv;
}

sai_status_t
__brcm_sai_port_tc_color_dscp_map_set(int port, int lval1, int lval2, int rval)
{
    sai_status_t rv;

    rv = bcm_port_dscp_unmap_set(0, port, lval1, lval2, rval);
    BRCM_SAI_API_CHK(SAI_API_PORT, "port dscp unmap set", rv);
    rv = bcm_port_control_set(0, port, bcmPortControlEgressModifyDscp, TRUE);
    BRCM_SAI_API_CHK(SAI_API_PORT, "port control set", rv);

    return rv;
}

sai_status_t
__brcm_sai_port_pfc_queue_map_set(int port, bcm_gport_t gports[8])
{
    int i;
    sai_status_t rv = SAI_STATUS_FAILURE;
    bcm_cosq_pfc_class_mapping_t mapping[8];

    for (i=0; i<8; i++)
    {
        bcm_cosq_pfc_class_mapping_t_init(&mapping[i]);
        mapping[i].class_id = i;
        mapping[i].gport_list[0] = gports[i];
    }
    rv = bcm_cosq_pfc_class_mapping_set(0, port, 8, mapping);
    BRCM_SAI_API_CHK(SAI_API_PORT, "cosq pfc class mapping set", rv);
    return rv;
}

sai_status_t
_brcm_sai_set_port_attribute(_In_ sai_object_id_t port_id,
                             _In_ const sai_attribute_t *attr,
                             bool pkt_mode, int cir, int cbs)
{
#define _SET_PORT "Set port"
    int val;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    bcm_port_t port;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_PORT);
    if (NULL == attr)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, port_id);
    switch(attr->id)
    {
        case SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE:
            switch (attr->value.u32)
            {
                case SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE:
                    val = BCM_PORT_LOOPBACK_NONE;
                    break;
                case SAI_PORT_INTERNAL_LOOPBACK_MODE_MAC:
                    val = BCM_PORT_LOOPBACK_MAC;
                    break;
                case SAI_PORT_INTERNAL_LOOPBACK_MODE_PHY:
                    val = BCM_PORT_LOOPBACK_PHY;
                    break;
                default: val = BCM_PORT_LOOPBACK_NONE;
            }
            rv = bcm_port_loopback_set(0, port, val);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        case SAI_PORT_ATTR_FLOOD_STORM_CONTROL_POLICER_ID:
        case SAI_PORT_ATTR_BROADCAST_STORM_CONTROL_POLICER_ID:
        case SAI_PORT_ATTR_MULTICAST_STORM_CONTROL_POLICER_ID:
        {
            int flags = 0;

            if (pkt_mode)
            {
                flags |= BCM_RATE_MODE_PACKETS;
            }
            switch (attr->id)
            {
                case SAI_PORT_ATTR_FLOOD_STORM_CONTROL_POLICER_ID:
                    flags |= BCM_RATE_DLF;
                    break;
                case SAI_PORT_ATTR_BROADCAST_STORM_CONTROL_POLICER_ID:
                    flags |= BCM_RATE_BCAST;
                    break;
                case SAI_PORT_ATTR_MULTICAST_STORM_CONTROL_POLICER_ID:
                    flags |= BCM_RATE_MCAST;
                    break;
            }
            /* Need to disable everything for the current mode since
             * bcm won't allow changing between packet and byte
             * modes if any Storm Control feature is enabled.
             */
            rv = driverDisableStormControl(port);
            BRCM_SAI_API_CHK(SAI_API_PORT, "Disable Storm Control", rv);
            if (pkt_mode)
            {
                if (flags & BCM_RATE_DLF)
                {
                    rv = bcm_rate_dlfbc_set(0, cir, flags, port);
                }
                else if (flags & BCM_RATE_BCAST)
                {
                    rv = bcm_rate_bcast_set(0, cir, flags, port);
                }
                else
                {
                    rv = bcm_rate_mcast_set(0, cir, flags, port);
                }
            }
            else
            {
                rv = bcm_rate_bandwidth_set(0, port, flags, cir, cbs);
            }
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _SET_PORT, port, rv,
                                      attr->id);
            break;
        }
        case SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL:
        {
            bcm_port_priority_group_config_t prigrp_config;
            int i;

            memset(&prigrp_config, 0, sizeof(prigrp_config));
            for (i = 0; i < 8; i++)
            {
                prigrp_config.pfc_transmit_enable = 0;
                if (attr->value.u8 & (1 << i))
                {
                    prigrp_config.pfc_transmit_enable = 1;
                }
                rv = bcm_port_priority_group_config_set(0, port, i, &prigrp_config);
                BRCM_SAI_API_CHK(SAI_API_PORT, "PFC set", rv);
            }
            /* In TH2, we need this timer set to generate PFC
               frames. This is the default value  */
            if (SOC_IS_TOMAHAWK2(0))
            {
              rv = bcm_port_control_set(0, port, bcmPortControlPFCRefreshTime,
                                        0xC000);
            }
            break;
        }
        default:
            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                              "Unknown port attribute %d passed\n", attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_PORT);
    return rv;
#undef _SET_PORT
}

/*
* Routine Description:
*   Get port attribute value.
*
* Arguments:
*    [in] port_id - port id
*    [in] attr_count - number of attributes
*    [inout] attr_list - array of attributes
*
* Return Values:
*    SAI_STATUS_SUCCESS on success
*    Failure status code on error
*/
sai_status_t
_brcm_sai_get_port_attribute(_In_ sai_object_id_t port_id,
                             _Inout_ sai_attribute_t *attr)
{
#define _GET_PORT "Get port"
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int val;
    bcm_port_t port;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_PORT);

    port = BRCM_SAI_GET_OBJ_VAL(bcm_port_t, port_id);
    switch(attr->id)
    {
        case SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE:
            rv = bcm_port_loopback_get(0, port, &val);
            BRCM_SAI_NUM_ATTR_API_CHK(SAI_API_PORT, _GET_PORT, port, rv,
                                      attr->id);
            switch (val)
            {
                case BCM_PORT_LOOPBACK_NONE:
                    attr->value.u32 = SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE;
                    break;
                case BCM_PORT_LOOPBACK_MAC:
                    attr->value.u32 = SAI_PORT_INTERNAL_LOOPBACK_MODE_MAC;
                    break;
                case BCM_PORT_LOOPBACK_PHY:
                    attr->value.u32 = SAI_PORT_INTERNAL_LOOPBACK_MODE_PHY;
                    break;
                default:
                    attr->value.u32 = SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE;
            }
            break;
        default:
            BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR,
                              "Unknown port attribute %d passed\n",
                              attr->id);
            break;
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_PORT);
    return rv;
#undef _GET_PORT
}

STATIC sai_status_t
_brcm_sai_port_qos_map_array_get(int qmapid, bcm_qos_map_t qosmap_array[_BRCM_SAI_MAX_QOS_MAPS])
{
    sai_status_t rv = SAI_STATUS_FAILURE;
    int array_size = 0, array_count;
    sai_uint32_t flags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L2;

    /* First get the array count */
    rv = bcm_qos_map_multi_get(0, flags, qmapid, array_size, qosmap_array, &array_count);
    BRCM_SAI_API_CHK(SAI_API_PORT, "qos map multi get", rv);
    if (array_count > _BRCM_SAI_MAX_QOS_MAPS)
    {
        BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_ERROR, "Buffer overflow of qos map\n");
        return SAI_STATUS_BUFFER_OVERFLOW;
    }
    array_size = array_count;
    /* Now get the array */
    rv = bcm_qos_map_multi_get(0, flags, qmapid, array_size, qosmap_array, &array_count);
    BRCM_SAI_API_CHK(SAI_API_PORT, "qos map multi get", rv);

    return rv;
}

STATIC sai_status_t
_brcm_sai_port_qos_map_add(int qmapid, int pkt_pri, int int_pri, bcm_color_t color)
{
    sai_status_t rv = SAI_STATUS_FAILURE;
    bcm_qos_map_t qosmap;
    sai_uint32_t flags = BCM_QOS_MAP_INGRESS | BCM_QOS_MAP_L2;

    bcm_qos_map_t_init(&qosmap);
    qosmap.pkt_pri = pkt_pri;
    qosmap.pkt_cfi = 0;
    qosmap.color = color;
    qosmap.int_pri = int_pri;
    rv = bcm_qos_map_add(0, flags, &qosmap, qmapid);
    BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map add", rv);
    /* Setup both CFI cases */
    qosmap.pkt_cfi = 1;
    qosmap.color = bcmColorRed; /* Default color for CFI = 1 */
    rv = bcm_qos_map_add(0, flags, &qosmap, qmapid);
    BRCM_SAI_API_CHK(SAI_API_PORT, "qos port map add", rv);

    return rv;
}

/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_port_api_t port_apis = {
    NULL,
    NULL,
    brcm_sai_set_port_attribute,
    brcm_sai_get_port_attribute,
    brcm_sai_get_port_stats,
    NULL,
    brcm_sai_clear_port_stats
};
