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
sai_queue_pfc_deadlock_notification_fn _pfc_deadlock_event = NULL;
_brcm_sai_qmin_cache_t _sai_queue_min[_BRCM_SAI_MAX_PORTS][_BRCM_SAI_PORT_MAX_QUEUES];

/*
################################################################################
#                               Event handlers                                 #
################################################################################
*/
int _brcm_cosq_pfc_deadlock_recovery_event_cb(int unit, bcm_port_t port, 
        bcm_cos_queue_t cosq,
        bcm_cosq_pfc_deadlock_recovery_event_t recovery_state, void *userdata)
{
    sai_queue_deadlock_notification_data_t q_dl;

    if (IS_NULL(_pfc_deadlock_event))
    {
        return 0;
    }

    if (bcmCosqPfcDeadlockRecoveryEventBegin == recovery_state)
    {
        q_dl.event = SAI_QUEUE_PFC_DEADLOCK_EVENT_TYPE_DETECTED;
    }
    else if (bcmCosqPfcDeadlockRecoveryEventEnd == recovery_state)
    {
        q_dl.event = SAI_QUEUE_PFC_DEADLOCK_EVENT_TYPE_RECOVERED;
    }
    else
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                           "Unknown deadlock recovery state event "
                           "(port: %d, queue %d).\n", port, cosq);
        return 0;
    }
    /* Reconstruct front panel unicast queue obj */
    q_dl.queue_id = BRCM_SAI_CREATE_OBJ_SUB_MAP(SAI_OBJECT_TYPE_QUEUE,
                                                SAI_QUEUE_TYPE_UNICAST,
                                                port, (cosq+1));

    _pfc_deadlock_event(1, &q_dl);
    return q_dl.app_managed_recovery;
}




/*
################################################################################
#                             QOS Queue functions                              #
################################################################################
*/

sai_status_t
_brcm_sai_qos_queue_tail_drop_set(int port, int qid,
                                  _brcm_sai_qos_wred_t* wred_p);
/*
  Wrapper to finally call BCM WRED APIs.
*/
sai_status_t
_brcm_sai_qos_queue_wred_set(int port, int qid, 
                             _brcm_sai_qos_wred_t* wred_p)
{
    _brcm_sai_cosq_gport_discard_t sai_discard;
    bcm_gport_t gport;
    sai_status_t rv;
    uint8 type = wred_p->valid;
    
    rv = bcm_port_gport_get(0, port, &gport);
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "port gport get", rv);
    
    if (type & _BRCM_SAI_WRED_ATTR_GREEN)
    {
        sal_memcpy(&sai_discard, &wred_p->discard_g,
                   sizeof(_brcm_sai_cosq_gport_discard_t));
        sai_discard.discard.flags |= (BCM_COSQ_DISCARD_NONTCP |
                                      BCM_COSQ_DISCARD_TCP);
        rv = _brcm_sai_cosq_gport_discard_set(gport, qid, wred_p->ect, 
                                              wred_p->gn, &sai_discard);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "sai cosq gport discard set", rv);
    }
    if (type & _BRCM_SAI_WRED_ATTR_YELLOW)
    {
        sal_memcpy(&sai_discard, &wred_p->discard_y,
                   sizeof(_brcm_sai_cosq_gport_discard_t));
        sai_discard.discard.flags |= (BCM_COSQ_DISCARD_NONTCP |
                                      BCM_COSQ_DISCARD_TCP);
        rv = _brcm_sai_cosq_gport_discard_set(gport, qid, wred_p->ect, 
                                              wred_p->gn, &sai_discard);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "sai cosq gport discard set", rv);
    }
    if (type & _BRCM_SAI_WRED_ATTR_RED)
    {
        sal_memcpy(&sai_discard, &wred_p->discard_r,
                   sizeof(_brcm_sai_cosq_gport_discard_t));
        sai_discard.discard.flags |= (BCM_COSQ_DISCARD_NONTCP |
                                      BCM_COSQ_DISCARD_TCP);
        rv = _brcm_sai_cosq_gport_discard_set(gport, qid, wred_p->ect, 
                                              wred_p->gn, &sai_discard);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "sai cosq gport discard set", rv);
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_qos_queue_tail_drop_set(int port, int qid, 
                                  _brcm_sai_qos_wred_t* wred_p)
{
    _brcm_sai_cosq_gport_discard_t sai_discard;
    bcm_gport_t gport;
    sai_status_t rv;
    uint8 type = wred_p->valid;
    
    rv = bcm_port_gport_get(0, port, &gport);
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "port gport get", rv);
    
    if (type & _BRCM_SAI_WRED_ATTR_GREEN)
    {
        sal_memcpy(&sai_discard, &wred_p->discard_g,
                   sizeof(_brcm_sai_cosq_gport_discard_t));
        if (BCM_COSQ_DISCARD_ENABLE & sai_discard.discard.flags)
        {
            sai_discard.discard.flags &= ~BCM_COSQ_DISCARD_ENABLE;
            rv = bcm_port_gport_get(0, port, &gport);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "port gport get.", rv);
            rv = _brcm_sai_cosq_gport_discard_set(gport, qid, wred_p->ect,
                                                  wred_p->gn, &sai_discard);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "sai cosq gport discard set.", rv);
            type &= ~_BRCM_SAI_WRED_ATTR_GREEN;
        }
    }
    if (type & _BRCM_SAI_WRED_ATTR_YELLOW)
    {
        sal_memcpy(&sai_discard, &wred_p->discard_y,
                   sizeof(_brcm_sai_cosq_gport_discard_t));
        if (BCM_COSQ_DISCARD_ENABLE & sai_discard.discard.flags)
        {
            sai_discard.discard.flags &= ~BCM_COSQ_DISCARD_ENABLE;
            rv = bcm_port_gport_get(0, port, &gport);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "port gport get.", rv);
            rv = _brcm_sai_cosq_gport_discard_set(gport, qid, wred_p->ect,
                                                  wred_p->gn, &sai_discard);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "sai cosq gport discard set.", rv);
            type &= ~_BRCM_SAI_WRED_ATTR_YELLOW;
        }
    }
    if (type & _BRCM_SAI_WRED_ATTR_RED)
    {
        sal_memcpy(&sai_discard, &wred_p->discard_r,
                   sizeof(_brcm_sai_cosq_gport_discard_t));
        if (BCM_COSQ_DISCARD_ENABLE & sai_discard.discard.flags)
        {
            sai_discard.discard.flags &= ~BCM_COSQ_DISCARD_ENABLE;
            rv = bcm_port_gport_get(0, port, &gport);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "port gport get.", rv);
            rv = _brcm_sai_cosq_gport_discard_set(gport, qid, wred_p->ect,
                                                  wred_p->gn, &sai_discard);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "sai cosq gport discard set.", rv);
            type &= ~_BRCM_SAI_WRED_ATTR_RED;
        }
    }
    return SAI_STATUS_SUCCESS;
}


/**
 * @brief Set attribute to Queue
 * @param[in] queue_id queue id to set the attribute
 * @param[in] attr attribute to set
 *
 * @return  SAI_STATUS_SUCCESS on success
 *           Failure status code on error
 */
STATIC sai_status_t
brcm_sai_set_queue_attribute(_In_ sai_object_id_t queue_id,
                             _In_ const sai_attribute_t *attr)
{
    bcm_gport_t gport;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int qid, port, qtype = _BRCM_SAI_QUEUE_TYPE_UCAST;
    int num_queues = _brcm_sai_get_num_queues();

    BRCM_SAI_FUNCTION_ENTER(SAI_API_QUEUE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    if (IS_NULL(attr))
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "NULL params passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    qid = BRCM_SAI_GET_OBJ_VAL(bcm_cos_queue_t, queue_id) - 1; /* rebase to 0 */
    /* Retrieve the port number associated with this "queue_id". */
    port = BRCM_SAI_GET_OBJ_MAP(queue_id);

    switch(attr->id)
    {
        case SAI_QUEUE_ATTR_WRED_PROFILE_ID:
        {
            int idx[2];
            uint8_t type = 0;
            _brcm_sai_indexed_data_t data;
            _brcm_sai_qos_wred_t wred_p;
            int wred_id;
            
            idx[0] = port;
            idx[1] = qid;
            if (SAI_QUEUE_TYPE_MULTICAST == BRCM_SAI_GET_OBJ_SUB_TYPE(queue_id))
            {
                qtype = _BRCM_SAI_QUEUE_TYPE_MULTICAST;
                idx[1] += num_queues;
            }
            
            if (SAI_OBJECT_TYPE_NULL != BRCM_SAI_GET_OBJ_TYPE(attr->value.oid)) /* WRED drop */
            {
                wred_id = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
                rv = _brcm_sai_wred_discard_get(wred_id,
                                                &type, &wred_p);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "wred discard get", rv);
                if (!type)
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "Invalid wred id passed..\n");
                    return SAI_STATUS_INVALID_PARAMETER;
                }
                rv = _brcm_sai_qos_queue_wred_set(port, qid, &wred_p);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "wred discard set", rv);
                
                data.queue_wred.idx1 = idx[0];
                data.queue_wred.idx2 = idx[1];
                data.queue_wred.wred = wred_id;
                rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_QUEUE_WRED,
                                                idx, &data);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "queue wred data set", rv);

                /* set queue'th bit */
                wred_p.port_data[port] |= (0x1 << qid);
                BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_DEBUG,
                                  "Updated wred profile %d on port %d qid %d\n",
                                  wred_id, port, qid);
                rv = _brcm_sai_wred_discard_update(wred_id, &wred_p);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "wred profile update", rv);
            }
            else /* TAIL drop */
            {               
                rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_QUEUE_WRED,
                                                idx, &data);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "queue wred data get", rv);
                if (0 >= data.queue_wred.wred)
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG, "Skip wred disable.\n");
                    return SAI_STATUS_SUCCESS;
                }
                wred_id = data.queue_wred.wred;
                if (_brcm_sai_wred_discard_get(wred_id, &type,
                                               &wred_p))
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG, "Skip wred disable..\n");
                    return SAI_STATUS_SUCCESS;
                }
                if (!type)
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG, "Skip wred disable...\n");
                    return SAI_STATUS_SUCCESS;
                }
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG, "Disable wred id: %d\n",
                                   data.queue_wred.wred);

                rv = _brcm_sai_qos_queue_tail_drop_set(port, qid,  &wred_p);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "wred tail drop set", rv);

                /* unset queue'th bit */
                wred_p.port_data[port] &= ~(0x1 << qid);
                BRCM_SAI_LOG_WRED(SAI_LOG_LEVEL_DEBUG,
                                  "Disabled wred profile %d on port %d qid %d\n",
                                  wred_id, port, qid);
                rv = _brcm_sai_wred_discard_update(wred_id, &wred_p);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "wred profile update", rv);
                
                if (!type)
                {
                    data.queue_wred.idx1 = 0;
                    data.queue_wred.idx2 = 0;
                    data.queue_wred.wred = -1;
                    rv = _brcm_sai_indexed_data_set(_BRCM_SAI_INDEXED_QUEUE_WRED,
                                                    idx, &data);
                    BRCM_SAI_RV_CHK(SAI_API_QUEUE, "queue wred data set", rv);
                }
            }
            break;
        }
        case SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID:
        {
            bcm_cos_queue_t cosq;
            bcm_gport_t parent_gport;
            _brcm_sai_qos_scheduler_t scheduler;
            int id, mode = _BRCM_SAI_SCHEDULER_ATTR_INVALID;

            id = BRCM_SAI_GET_OBJ_VAL(int, attr->value.oid);
            rv = _brcm_sai_scheduler_get(id, &scheduler);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "sched prof data get", rv);
            /* Attach queue object to scheduler list. */
            if (SAI_STATUS_SUCCESS != 
                    _brcm_sai_scheduler_attach_object(id, &scheduler, queue_id))
            {
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, 
                                   "Unable to attach queue object %lx to scheduler %lx.\n",
                                   queue_id, attr->value.oid);
                return SAI_STATUS_FAILURE;
            }
            mode = _brcm_sai_scheduler_mode_get(&scheduler);
            if (SAI_QUEUE_TYPE_MULTICAST == BRCM_SAI_GET_OBJ_SUB_TYPE(queue_id))
            {
                qtype = _BRCM_SAI_QUEUE_TYPE_MULTICAST;
            }

            /* Retrieve gport queue ID. */
            gport = _brcm_sai_switch_port_queue_get(port, 
                        _BRCM_SAI_QUEUE_TYPE_MULTICAST == qtype ? 
                        qid-num_queues : qid, qtype);
            if (-1 == gport)
            {
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                return SAI_STATUS_FAILURE;
            }
            rv = bcm_cosq_gport_attach_get(0, gport, &parent_gport, &cosq);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq gport attach get", rv);

            if (_BRCM_SAI_SCHEDULER_ATTR_INVALID != mode)
            {
                rv = bcm_cosq_gport_sched_set(0, parent_gport, cosq, mode, 
                                                  scheduler.weight);
                if (BCM_E_UNAVAIL == rv)
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG, 
                                       "Trying to set schedule mode on port:%d, "
                                       "queue:%d and OPENNSL returned UNAVAIL.\n"
                                       "Check that all SP queues are consecutive offsets "
                                       "on this scheduler node.\n",
                                       port, qid);
                }
                BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq gport sched set", rv);
            }

            rv = _brcm_sai_cosq_bandwidth_set(parent_gport, cosq, &scheduler);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cosq gport bandwidth set", rv);

            if (scheduler.minimum_burst)
            {
                rv = _brcm_sai_cosq_config(parent_gport, cosq, 10, 
                                              (uint32) scheduler.minimum_burst);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cosq port control set "
                                 "(BandwidthBurstMin)", rv);
            }
            if (scheduler.maximum_burst)
            {
                rv = _brcm_sai_cosq_config(parent_gport, cosq, 11, 
                                              (uint32) scheduler.maximum_burst);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cosq port control set "
                                "(BandwidthBurstMax)", rv);
            }
        
            break;
        }
        case SAI_QUEUE_ATTR_BUFFER_PROFILE_ID:
        {
            int rem, pool_idx, index = 0;
            _brcm_sai_buf_pool_t *buf_pool = NULL;
            sai_buffer_pool_threshold_mode_t mode;
            _brcm_sai_buf_profile_t *buf_profile = NULL;

            if (SAI_OBJECT_TYPE_NULL == BRCM_SAI_GET_OBJ_TYPE(attr->value.oid))
            {
                /* Unset this queue in buf_profile? */                
                rv = SAI_STATUS_SUCCESS;   
                break;
            }
            rv = _brcm_sai_buffer_profile_get(attr->value.oid, &buf_profile);
            if (SAI_STATUS_NOT_EXECUTED == rv)
            {
                /* FIXME: Restore switch defaults.
                 *        Note: Can these values be modified on the fly ?
                 *              Can we assume there will be no traffic running/queued ?
                 */
                rv = SAI_STATUS_SUCCESS;               
                break;
            }

            if (SAI_STATUS_SUCCESS != rv || IS_NULL(buf_profile))
            {
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                                   "Unable to retreive buffer profile object info.\n");
                return rv;
            }
            rv = _brcm_sai_buffer_pool_get(buf_profile->pool_id, &buf_pool);
            if (SAI_STATUS_SUCCESS != rv || IS_NULL(buf_pool))
            {
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                                   "Unable to retreive buffer pool object info.\n");
                return rv;
            }
            if (SAI_BUFFER_POOL_TYPE_EGRESS != buf_pool->type)
            {
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                                   "Buffer pool is not of type EGRESS.\n");
                return SAI_STATUS_INVALID_PARAMETER;
            }
            pool_idx = BRCM_SAI_GET_OBJ_VAL(int, buf_profile->pool_id) - 1; /* rebase to 0 */
            rv = _brcm_sai_cosq_config(port, qid, 12, pool_idx);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE,
                            "cosq control set (CosqControlUCEgressPool)", rv);
            mode = buf_profile->mode;
            if (-1 == mode)
            {
                mode = buf_pool->mode;
            }
            rv = _brcm_sai_cosq_config(port, qid, 13,
                     (SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC == mode) ?
                     TRUE : FALSE);
            BRCM_SAI_API_CHK(SAI_API_QUEUE,
                             "cosq control set (EgressUCSharedDynamicEnable)", rv);
            if (SAI_BUFFER_POOL_THRESHOLD_MODE_DYNAMIC == mode)
            {
                rv = driverEgressQueueSharedAlphaSet(port, qid,
                                                     buf_profile->shared_val);
                BRCM_SAI_API_CHK(SAI_API_QUEUE, "queue shared alpha set", rv);
            }
            else
            {
                rv = _brcm_sai_cosq_config(port, qid, 14, buf_profile->shared_val);
                BRCM_SAI_API_CHK(SAI_API_QUEUE,
                                 "cosq control set (EgressUCQueueSharedLimitBytes)",
                                 rv);
            }
#ifdef SDK_API
            rv = bcm_cosq_control_set(0, port, qid,
                     bcmCosqControlEgressUCQueueMinLimitBytes,
                     buf_profile->size);
            BRCM_SAI_API_CHK(SAI_API_QUEUE,
                             "cosq control set (EgressUCQueueMinLimitBytes)", rv);
#else
            rv = driverEgressQueueMinLimitSet(port, qid, buf_profile->size);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "queue egress Min limit set", rv);
#endif
            _sai_queue_min[port][qid].valid = TRUE;
            _sai_queue_min[port][qid].val = buf_profile->size;
            rem = buf_profile->size % BRCM_MMU_B_PER_C;
            if (rem)
            {
                _sai_queue_min[port][qid].val += (BRCM_MMU_B_PER_C - rem);
            }
            _sai_queue_min[port][qid].buff_prof = attr->value.oid;
            /* set to 2xMTU */
            rv = driverEgressQueueResetOffsetSet(port, qid, buf_profile->size * 2);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "queue egress Reset offset set", rv);

            /* Make port config in-effective */
            rv = _brcm_sai_port_egress_buffer_config_set(port, pool_idx,
                                                         mode, -1, -1);
            if (SAI_STATUS_SUCCESS != rv)
            {
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                                   "Error calling port egress buffer config set.\n");
                return rv;
            }
            /* Set this queue in buf_profile */
            buf_profile->queue_data[port] |= (0x1 << qid);

            /* Save port gport for bst use */
            rv = bcm_port_gport_get(0, port, &gport);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "port gport get", rv);
            rv = driverMMUInstMapIndexGet(0, gport, bcmBstStatIdEgrPool, &index);
            BRCM_SAI_RV_CHK(SAI_API_QUEUE, "mmu instance map index get", rv);
            buf_pool->bst_rev_gport_maps[index] = gport;
            buf_pool->bst_rev_cos_maps[index] = qid;
            
            break;
        }
        case SAI_QUEUE_ATTR_ENABLE_PFC_DLDR:
        {
            bcm_cosq_pfc_deadlock_queue_config_t dl_qconfig;

            if (SAI_QUEUE_TYPE_MULTICAST == BRCM_SAI_GET_OBJ_SUB_TYPE(queue_id))
            {
                qtype = _BRCM_SAI_QUEUE_TYPE_MULTICAST;
            }
            /* Retrieve gport queue ID. */
            gport = _brcm_sai_switch_port_queue_get(port, 
                        _BRCM_SAI_QUEUE_TYPE_MULTICAST == qtype ? 
                        qid-num_queues : qid, qtype);
            if (-1 == gport)
            {
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                return SAI_STATUS_FAILURE;
            }
            bcm_cosq_pfc_deadlock_queue_config_t_init(&dl_qconfig);
            dl_qconfig.enable = attr->value.booldata;
            rv = bcm_cosq_pfc_deadlock_queue_config_set(0, gport, &dl_qconfig);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq pfc deadlock queue config set", rv);
            break;
        }
        case SAI_QUEUE_ATTR_PFC_DLR_INIT:
        {
            if (DEV_IS_THX())
            {
                if (TRUE == attr->value.booldata)
                {
                    rv = bcm_cosq_pfc_deadlock_recovery_start(0, port, qid); 
                    BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq pfc deadlock recovery start", rv);
                }
                else
                {
                    rv = bcm_cosq_pfc_deadlock_recovery_exit(0, port, qid);
                    BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq pfc deadlock recovery exit", rv);
                }
            }
            else
            {
                return SAI_STATUS_NOT_SUPPORTED;
            }
            break;
        }
        default:
            BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, 
                               "Unknown qos queue attribute %d passed\n", attr->id);
            rv = SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_QUEUE);
    return rv;
}

/**
 * @brief Get attribute to Queue
 * @param[in] queue_id queue id to set the attribute
 * @param[in] attr_count number of attributes
 * @param[inout] attr_list Array of attributes
 *
 * @return  SAI_STATUS_SUCCESS on success
 *           Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_queue_attribute(_In_ sai_object_id_t queue_id,
                             _In_ uint32_t        attr_count,
                             _Inout_ sai_attribute_t *attr_list)
{
    int val, i, type;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int qid, port, qtype = _BRCM_SAI_QUEUE_TYPE_UCAST;
    int num_queues = _brcm_sai_get_num_queues();

    BRCM_SAI_FUNCTION_ENTER(SAI_API_QUEUE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_GET_ATTRIB_PARAM_CHK;
    BRCM_SAI_GET_OBJ_ATTRIB_PARAM_CHK(queue_id, SAI_OBJECT_TYPE_QUEUE);
    
    qid = BRCM_SAI_GET_OBJ_VAL(bcm_cos_queue_t, queue_id) - 1; /* rebase to 0 */
    port = BRCM_SAI_GET_OBJ_MAP(queue_id);

    for (i = 0; i<attr_count; i++)
    {
        switch(attr_list[i].id)
        {
            case SAI_QUEUE_ATTR_TYPE:
                type = BRCM_SAI_GET_OBJ_SUB_TYPE(queue_id);
                switch (type)
                {
                    case SAI_QUEUE_TYPE_ALL:
                    case SAI_QUEUE_TYPE_UNICAST:
                    case SAI_QUEUE_TYPE_MULTICAST:
                        attr_list[i].value.u32 = type;
                        break;
                    default: rv = SAI_STATUS_INVALID_PARAMETER;
                        break;
                }
                break;
            case SAI_QUEUE_ATTR_PORT:
                BRCM_SAI_ATTR_LIST_OBJ(i) = 
                    BRCM_SAI_CREATE_OBJ(SAI_OBJECT_TYPE_PORT,
                                        BRCM_SAI_GET_OBJ_MAP(queue_id));
                break;
            case SAI_QUEUE_ATTR_INDEX:
            {
                bool found = FALSE;
                int q, numq, idx[2];
                _brcm_sai_indexed_data_t data;

                numq = _BRCM_SAI_IS_CPU_PORT(port) ? 
                           _brcm_sai_get_port_max_queues(TRUE) :
                           _brcm_sai_get_port_max_queues(FALSE);
                idx[0] = port;
                for (q=0; q<numq; q++)
                {
                    idx[1] = q;
                    rv = _brcm_sai_indexed_data_get(_BRCM_SAI_INDEXED_PORT_QID,
                                                    idx, &data);
                    BRCM_SAI_RV_CHK(SAI_API_PORT, "port qid data get", rv);
                    if ((BRCM_SAI_GET_OBJ_VAL(bcm_cos_queue_t, data.port_qid.qoid) - 1)
                         == qid)
                    {
                        found = TRUE;
                        break;
                    }
                }
                if (!found)
                {
                    rv = SAI_STATUS_INVALID_PARAMETER;
                }
                else
                {
                    attr_list[i].value.u8 = q;
                }
                break;
            }
            case SAI_QUEUE_ATTR_WRED_PROFILE_ID:                
            case SAI_QUEUE_ATTR_BUFFER_PROFILE_ID:
            case SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID:
            case SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE:
                rv = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
                break;
            case SAI_QUEUE_ATTR_PAUSE_STATUS:
                rv = _brcm_sai_cosq_state_get(0, port, qid, attr_list[i].id,
                                              &val);
                BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cosq state get", rv);
                attr_list[i].value.booldata = val ? TRUE : FALSE;
                break;
            case SAI_QUEUE_ATTR_ENABLE_PFC_DLDR:
            {
                bcm_gport_t gport;
                bcm_cosq_pfc_deadlock_queue_config_t dl_qconfig;

                if (SAI_QUEUE_TYPE_MULTICAST == BRCM_SAI_GET_OBJ_SUB_TYPE(queue_id))
                {
                    qtype = _BRCM_SAI_QUEUE_TYPE_MULTICAST;
                }
                /* Retrieve gport queue ID. */
                gport = _brcm_sai_switch_port_queue_get(port, 
                            _BRCM_SAI_QUEUE_TYPE_MULTICAST == qtype ? 
                            qid-num_queues : qid, qtype);
                if (-1 == gport)
                {
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
                    return SAI_STATUS_FAILURE;
                }
                rv = bcm_cosq_pfc_deadlock_queue_config_get(0, gport, &dl_qconfig);
                BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq pfc deadlock queue config set", rv);
                attr_list[i].value.booldata = dl_qconfig.enable;
                break;
            }
            default:
                BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                                   "Unknown qos queue attribute %d passed\n",
                                   attr_list[i].id);
                rv = SAI_STATUS_INVALID_PARAMETER;
                break;
        }
        if (SAI_STATUS_SUCCESS != rv)
        {
            BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_INFO,
                               "Error processing qos queue attributes\n");
            return rv;
        }
    }
    BRCM_SAI_FUNCTION_EXIT(SAI_API_QUEUE);
    return rv;
}

/**
 * @brief   Get queue statistics counters.
 *
 * @param[in] queue_id Queue id
 * @param[in] counter_ids specifies the array of counter ids
 * @param[in] number_of_counters number of counters in the array
 * @param[out] counters array of resulting counter values.
 *
 * @return SAI_STATUS_SUCCESS on success
 *         Failure status code on error
 */
STATIC sai_status_t
brcm_sai_get_queue_stats(_In_ sai_object_id_t queue_id,
                         _In_ uint32_t number_of_counters,
                         _In_ const sai_queue_stat_t *counter_ids,
                         _Out_ uint64_t* counters)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, qid, port, qtype = _BRCM_SAI_QUEUE_TYPE_UCAST;
    bcm_gport_t gport;
    int num_queues = _brcm_sai_get_num_queues();
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_QUEUE);
    BRCM_SAI_SWITCH_INIT_CHECK;

    qid = BRCM_SAI_GET_OBJ_VAL(bcm_cos_queue_t, queue_id) - 1; /* rebase to 0 */
    if (SAI_QUEUE_TYPE_MULTICAST == BRCM_SAI_GET_OBJ_SUB_TYPE(queue_id))
    {
        qtype = _BRCM_SAI_QUEUE_TYPE_MULTICAST;
    }
    port = BRCM_SAI_GET_OBJ_MAP(queue_id);
    if (_BRCM_SAI_IS_CPU_PORT(port))
    {
        gport = port;
    }
    else
    {
        gport = _brcm_sai_switch_port_queue_get(port, 
                    _BRCM_SAI_QUEUE_TYPE_MULTICAST == qtype ? 
                    qid-num_queues : qid, qtype);
    }
    if (-1 == gport)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
        return SAI_STATUS_FAILURE;
    }
    for(i=0; i<number_of_counters; i++)
    {
        rv = _brcm_sai_cosq_stat_get(port, qid, gport, counter_ids[i], &counters[i]);
        if (SAI_STATUS_ERROR(rv))
        {
            BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_NOTICE,
                               "cosq stat get failed with error %d for port %d qid %d\n",
                               rv, port, qid);
            return rv;
        }
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_QUEUE);
    return rv;
}


/**
 * @brief Clear queue statistics counters.
 *
 * @param[in] queue_id Queue id
 * @param[in] number_of_counters Number of counters in the array
 * @param[in] counter_ids Specifies the array of counter ids
 *
 * @return #SAI_STATUS_SUCCESS on success, failure status code on error
 */
STATIC sai_status_t
brcm_sai_clear_queue_stats(_In_ sai_object_id_t queue_id,
                           _In_ uint32_t number_of_counters,
                           _In_ const sai_queue_stat_t *counter_ids)
{
    sai_status_t rv = SAI_STATUS_SUCCESS;
    int i, qid, port, qtype = _BRCM_SAI_QUEUE_TYPE_UCAST;
    bcm_gport_t gport;
    int num_queues = _brcm_sai_get_num_queues();
    
    BRCM_SAI_FUNCTION_ENTER(SAI_API_QUEUE);
    BRCM_SAI_SWITCH_INIT_CHECK;
    BRCM_SAI_SET_STATS_PARAM_CHK;

    qid = BRCM_SAI_GET_OBJ_VAL(bcm_cos_queue_t, queue_id) - 1; /* rebase to 0 */
    if (SAI_QUEUE_TYPE_MULTICAST == BRCM_SAI_GET_OBJ_SUB_TYPE(queue_id))
    {
        qtype = _BRCM_SAI_QUEUE_TYPE_MULTICAST;
    }
    port = BRCM_SAI_GET_OBJ_MAP(queue_id);
    if (_BRCM_SAI_IS_CPU_PORT(port))
    {
        gport = port;
    }
    else
    {
        gport = _brcm_sai_switch_port_queue_get(port, 
                    _BRCM_SAI_QUEUE_TYPE_MULTICAST == qtype ? 
                    qid-num_queues : qid, qtype);
    }
    if (-1 == gport)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "port gport get failed.\n");
        return SAI_STATUS_FAILURE;
    }
    for(i=0; i<number_of_counters; i++)
    {
        rv = _brcm_sai_cosq_stat_set(port, qid, gport, counter_ids[i], 0);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "cosq stat set", rv);
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_QUEUE);
    return rv;
}

/*
################################################################################
#                                Internal functions                            #
################################################################################
*/
sai_status_t 
_brcm_sai_alloc_queue_wred() 
{
    int p, q;
    sai_status_t rv;

    if ((rv = _brcm_sai_indexed_data_init2(_BRCM_SAI_INDEXED_QUEUE_WRED,
                                           _BRCM_SAI_MAX_PORTS,
                                           _brcm_sai_get_max_queues()))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_CRITICAL,
                           "Error initializing queue wred data !!\n");
        return rv;
    }
    for (p=0; p<_BRCM_SAI_MAX_PORTS; p++)
    {
        for (q=0; q<_BRCM_SAI_PORT_MAX_QUEUES; q++)
        {
            _sai_queue_min[p][q].valid = FALSE;
        }
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_free_queue_wred()
{
    sai_status_t rv;

    if ((rv = _brcm_sai_indexed_data_free2(_BRCM_SAI_INDEXED_QUEUE_WRED,
                                           _BRCM_SAI_MAX_PORTS,
                                           _brcm_sai_get_max_queues()))
            != SAI_STATUS_SUCCESS)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_CRITICAL,
                           "Error freeing queue wred data !!\n");
        return rv;
    }
    return SAI_STATUS_SUCCESS;
}

int
_brcm_sai_get_port_max_queues(bool cpu)
{
    if (cpu)
    {        
        if (DEV_IS_THX() || DEV_IS_TD3())
        {
            /* TD3 has the same number of CPU queues as THx */
            return NUM_TH_CPU_MC_QUEUES;
        }
        else if (DEV_IS_HX4())
        {
            return NUM_HX4_CPU_MC_QUEUES;
        }
        /* TD2 */
        return NUM_TD2_CPU_MC_QUEUES;
    }
    return _brcm_sai_get_num_queues() + _brcm_sai_get_num_mc_queues();
}

int
_brcm_sai_get_max_queues()
{
    int cq, pq;
    
    cq = _brcm_sai_get_port_max_queues(TRUE);
    pq = _brcm_sai_get_port_max_queues(FALSE);
    return (cq > pq) ? cq : pq;
}

/* UC queues */
int
_brcm_sai_get_num_queues()
{
    int rv, mc_q_mode, ucq;

    if (DEV_IS_HX4())
    {
        return NUM_HX4_QUEUES;
    }
    if (DEV_IS_TH3())
    {
        rv = driverPropertyCheck(3, &mc_q_mode);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "mc q mode", rv);
        ucq = (mc_q_mode == 0) ? 12 : (mc_q_mode == 1) ? 10 :
              (mc_q_mode == 2) ? 8  : (mc_q_mode == 3) ? 6 : 8;
        return ucq;
    }
    /* TH, TH2, TDx */
    return NUM_TH_QUEUES;
}

int
_brcm_sai_get_num_mc_queues()
{
    int rv, mc_q_mode, mcq;

    if (DEV_IS_HX4())
    {
        return NUM_HX4_QUEUES;
    }
    if (DEV_IS_TH3())
    {
        rv = driverPropertyCheck(3, &mc_q_mode);
        BRCM_SAI_RV_CHK(SAI_API_QUEUE, "mc q mode", rv);
        mcq = (mc_q_mode == 0) ? 0 : (mc_q_mode == 1) ? 2 :
              (mc_q_mode == 2) ? 4 : (mc_q_mode == 3) ? 6 : 4;
        return mcq;
    }
    /* TH, TH2, TDx */
    return NUM_TH_QUEUES;
}

int
_brcm_sai_get_num_l0_nodes(bcm_port_t port)
{
    if (DEV_IS_TD2())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_TD2_CPU_L0_NODES;
        }
        else
        {
            return NUM_TD2_L0_NODES;
        }
    }
    else if (DEV_IS_HX4())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_HX4_CPU_L0_NODES;
        }
        else
        {
            return NUM_HX4_L0_NODES;
        }
    }
    else if (DEV_IS_TH3())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_TH3_CPU_L0_NODES;
        }
        else
        {
            return NUM_TH3_L0_NODES;
        }
    }
    else
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_TH_CPU_L0_NODES;
        }
        else
        {
            return NUM_TH_L0_NODES;
        }
    }
}

int
_brcm_sai_get_num_l1_nodes(bcm_port_t port)
{
    if (DEV_IS_TD2())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_CPU_L1_NODES;
        }
        else
        {
            return NUM_L1_NODES;
        }
    }
    else if (DEV_IS_HX4())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_CPU_L1_NODES;
        }
        else
        {
            return NUM_HX4_L1_NODES;
        }
    }
    else if (DEV_IS_TH3())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_TH3_CPU_L1_NODES;
        }
        else
        {
            return NUM_TH3_L1_NODES;
        }
    }
    else /* TH */
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return NUM_CPU_L1_NODES;
        }
        else
        {
            return NUM_L1_NODES;
        }
    }
}

int
_brcm_sai_get_num_scheduler_groups(bcm_port_t port)
{
    if (DEV_IS_TD2())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return (NUM_CPU_L1_NODES + NUM_TD2_CPU_L0_NODES + 1);
        }
        else
        {
            return (NUM_L1_NODES + NUM_TD2_L0_NODES + 1); /* Including root/port sched */
        }
    }
    else if (DEV_IS_HX4())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return (NUM_CPU_L1_NODES + NUM_HX4_CPU_L0_NODES + 1);
        }
        else
        {
            return (NUM_HX4_L1_NODES + NUM_HX4_L0_NODES + 1);
        }
    }
    else if (DEV_IS_TH3())
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return (NUM_TH3_CPU_L0_NODES + 1);
        }
        else
        {
            return (NUM_TH3_L0_NODES + 1);
        }
    }
    else /* TH,TH2,TD3 */
    {
        if(_BRCM_SAI_IS_CPU_PORT(port))
        {
            return (NUM_TH_CPU_L0_NODES + 1);
        }
        else
        {
            return (NUM_TH_L0_NODES + 1);
        }
    }
}


/*
################################################################################
#                                Functions map                                 #
################################################################################
*/
const sai_queue_api_t qos_apis = {
    NULL,
    NULL,
    brcm_sai_set_queue_attribute,
    brcm_sai_get_queue_attribute,
    brcm_sai_get_queue_stats,
    NULL,
    brcm_sai_clear_queue_stats
};
