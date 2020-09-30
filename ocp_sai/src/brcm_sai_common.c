/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <sai.h>
#include <brcm_sai_common.h>
#include <soc/drv.h>
#if defined (BCM_TOMAHAWK2_SUPPORT)
#include <soc/tomahawk2.h>
#endif
#include <string.h>
#include <bcm_int/bst.h>


extern uint32 egress_port_flex_counter_id_map[_BRCM_SAI_MAX_PORTS];
extern _brcm_sai_queue_tc_mapping_t 
       port_tc_queue_map_cache[_BRCM_SAI_MAX_PORTS][_BRCM_SAI_PORT_MAX_QUEUES];
extern _brcm_sai_qmin_cache_t
       _sai_queue_min[_BRCM_SAI_MAX_PORTS][_BRCM_SAI_PORT_MAX_QUEUES];

/*
################################################################################
#                          Non persistent local state                          #
################################################################################
*/
#define _L3_CONFIGS_MAX 8
static int __brcm_sai_total_alloc = 0;
static int _l3_config_info[_L3_CONFIGS_MAX+1] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };

int _brcm_sai_cpu_pg_id = 0;
int _brcm_sai_cpu_ingress_pool_id = 0;
int _brcm_sai_cpu_pool_config = 0;


char *_brcm_sai_api_type_strings[] =  {
    /* Note: Keep in sync with sai_api_map defined in init_api code */
    "",
    "SWITCH",
    "PORT",
    "FDB",
    "VLAN",
    "VIRTUAL_ROUTER",
    "ROUTE",
    "NEXT_HOP",
    "NEXT_HOP_GROUP",
    "ROUTER_INTERFACE",
    "NEIGHBOR",
    "ACL",
    "HOST_INTERFACE",
    "MIRROR",
    "SAMPLEPACKET",
    "STP",
    "LAG",
    "POLICER",
    "WRED",
    "QOSMAPS",
    "QUEUES",
    "SCHEDULER",
    "SCH_GROUP",
    "MMU_BUFFERS",
    "HASH",
    "UDF",
    "TUNNEL",
    "L2MC",
    "IPMC",
    "RPF_GROUP",
    "L2MC_GROUP",
    "IPMC_GROUP",
    "MCAST_FDB",
    "BRIDGE"
};

void *ALLOC(int size)
{
    void *ptr = malloc(size+8);
#ifdef SAI_DEBUG
    void *r  = __builtin_return_address(0);
    void *p  = (char *)ptr + 8;
    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG,
                       "(caller %p) Allocing %p\n", r, p);
    _brcm_sai_trace_alloc_memory((uintptr_t)p, (uintptr_t)r);
#endif
    *(int*)ptr = SAI_MARKER;
    *((int*)(ptr+4)) = size;
    __brcm_sai_total_alloc += size;
    return ptr+8;
}

void *ALLOC_CLEAR(int n, int size)
{
    void *ptr = calloc(1, size+8);
#ifdef SAI_DEBUG
    void *r  = __builtin_return_address(0);
    void *p  = (char *)ptr + 8;
    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG,
                       "(caller %p) Allocing %p\n", r, p);
    _brcm_sai_trace_alloc_memory((uintptr_t)p, (uintptr_t)r);
#endif
    *(int*)ptr = SAI_MARKER;
    *((int*)(ptr+4)) = size;
    __brcm_sai_total_alloc += size;
    return ptr+8;
}

void FREE(void *ptr)
{
    int size = *((int*)(ptr-4));
#ifdef SAI_DEBUG
    void *r  = __builtin_return_address(0);
    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG,
                       "(caller %p) Freeing %p\n", r, ptr);
    _brcm_sai_trace_free_memory((uintptr_t)ptr, (uintptr_t)r);
#endif
    _verify(SAI_MARKER, ptr);
    __brcm_sai_total_alloc -= size;
    *((int*)(ptr-8)) = 0;
    *((int*)(ptr-4)) = 0;
    free(ptr-8);
}

int FREE_SIZE(void *ptr)
{
    int size = *((int*)(ptr-4));
#ifdef SAI_DEBUG
    void *r  = __builtin_return_address(0);
    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG,
                       "(caller %p) Freeing %p\n", r, ptr);
    _brcm_sai_trace_free_memory((uintptr_t)ptr, (uintptr_t)r);
#endif
    _verify(SAI_MARKER, ptr);
    __brcm_sai_total_alloc -= size;
    *((int*)(ptr-8)) = 0;
    *((int*)(ptr-4)) = 0;
    free(ptr-8);
    return size;
}

int CHECK_FREE_SIZE(void *ptr)
{
    if (ptr)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG,
                           "(caller %p) Freeing %p\n", __builtin_return_address(0), ptr);
        return FREE_SIZE(ptr);
    }
    return 0;
}

int CHECK_FREE_CLEAR_SIZE(void *ptr)
{
    if (ptr)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_DEBUG,
                           "(caller %p) Freeing %p\n", __builtin_return_address(0), ptr);
        int size = FREE_SIZE(ptr);
        ptr = NULL;
        return size;
    }
    return 0;
}

sai_status_t
BRCM_RV_BCM_TO_SAI(int rv)
{
    switch(rv)
    {
        case BCM_E_NONE:
            return SAI_STATUS_SUCCESS;
        case BCM_E_PARAM:
            return SAI_STATUS_INVALID_PARAMETER;
        case BCM_E_FAIL:
            return SAI_STATUS_FAILURE;
        case BCM_E_UNAVAIL:
            return SAI_STATUS_NOT_SUPPORTED;
        case BCM_E_NOT_FOUND:
            return SAI_STATUS_ITEM_NOT_FOUND;
        case BCM_E_MEMORY:
            return SAI_STATUS_NO_MEMORY;
        case BCM_E_EXISTS:
            return SAI_STATUS_ITEM_ALREADY_EXISTS;
        case BCM_E_PORT:
            return SAI_STATUS_INVALID_PORT_NUMBER;
        case BCM_E_INIT:
            return SAI_STATUS_UNINITIALIZED;
        case BCM_E_FULL:
            return SAI_STATUS_TABLE_FULL;
        case BCM_E_RESOURCE:
            return SAI_STATUS_INSUFFICIENT_RESOURCES;
        case BCM_E_BUSY:
            return SAI_STATUS_OBJECT_IN_USE;
        default:
            return SAI_STATUS_FAILURE;
    }
}

sai_status_t
BRCM_PORT_POOL_STAT_SAI_TO_BCM(sai_port_pool_stat_t sai_stat, bcm_stat_val_t *stat)
{
    switch(sai_stat)
    {
        case SAI_PORT_POOL_STAT_WATERMARK_BYTES:
            *stat = 0; //bcmBstStatIdPortPool;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_PORT_POOL_STAT_SHARED_WATERMARK_BYTES:
            *stat = bcmCosqStatIngressPortPoolSharedBytesPeak;
            break;
        default:
            return SAI_STATUS_NOT_SUPPORTED;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
BRCM_QOS_STAT_SAI_TO_BCM(sai_queue_stat_t sai_stat, bcm_cosq_stat_t *stat)
{
    switch(sai_stat)
    {
        case SAI_QUEUE_STAT_PACKETS: *stat = bcmCosqStatOutPackets;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_BYTES: *stat = bcmCosqStatOutBytes;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_DROPPED_PACKETS: *stat = bcmCosqStatDroppedPackets;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_DROPPED_BYTES: *stat = bcmCosqStatDroppedBytes;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_GREEN_PACKETS: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_GREEN_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_GREEN_DROPPED_PACKETS: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_GREEN_DROPPED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_YELLOW_PACKETS: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_YELLOW_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_YELLOW_DROPPED_PACKETS: *stat = bcmCosqStatYellowCongestionDroppedPackets;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_YELLOW_DROPPED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_RED_PACKETS: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_RED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_RED_DROPPED_PACKETS: *stat = bcmCosqStatRedCongestionDroppedPackets;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_RED_DROPPED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_GREEN_WRED_DROPPED_PACKETS: *stat = bcmCosqStatGreenDiscardDroppedPackets;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_GREEN_WRED_DROPPED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_YELLOW_WRED_DROPPED_PACKETS: *stat = bcmCosqStatYellowDiscardDroppedPackets;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_YELLOW_WRED_DROPPED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_RED_WRED_DROPPED_PACKETS: *stat = bcmCosqStatRedDiscardDroppedPackets;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_RED_WRED_DROPPED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_WRED_DROPPED_PACKETS: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_WRED_DROPPED_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        case SAI_QUEUE_STAT_CURR_OCCUPANCY_BYTES: *stat = bcmCosqStatEgressUCQueueBytesCurrent;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_WATERMARK_BYTES: *stat = bcmBstStatIdUcast;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_SHARED_WATERMARK_BYTES: *stat = bcmBstStatIdUcast;
            return SAI_STATUS_SUCCESS;
        case SAI_QUEUE_STAT_SHARED_CURR_OCCUPANCY_BYTES: 
        case SAI_QUEUE_STAT_GREEN_WRED_ECN_MARKED_PACKETS: 
        case SAI_QUEUE_STAT_GREEN_WRED_ECN_MARKED_BYTES: 
        case SAI_QUEUE_STAT_YELLOW_WRED_ECN_MARKED_PACKETS: 
        case SAI_QUEUE_STAT_YELLOW_WRED_ECN_MARKED_BYTES: 
        case SAI_QUEUE_STAT_RED_WRED_ECN_MARKED_PACKETS: 
        case SAI_QUEUE_STAT_RED_WRED_ECN_MARKED_BYTES: 
        case SAI_QUEUE_STAT_WRED_ECN_MARKED_PACKETS: 
        case SAI_QUEUE_STAT_WRED_ECN_MARKED_BYTES: 
            *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        default:
            return SAI_STATUS_NOT_SUPPORTED;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
BRCM_INGRESS_PG_STAT_SAI_TO_BCM(sai_ingress_priority_group_stat_t sai_stat, bcm_cosq_stat_t *stat)
{
    switch(sai_stat)
    {
        case SAI_INGRESS_PRIORITY_GROUP_STAT_SHARED_WATERMARK_BYTES: 
            *stat = bcmBstStatIdPriGroupShared;
            return SAI_STATUS_SUCCESS;
        case SAI_INGRESS_PRIORITY_GROUP_STAT_XOFF_ROOM_WATERMARK_BYTES: 
            *stat = bcmBstStatIdPriGroupHeadroom;
            return SAI_STATUS_SUCCESS;
        default: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
BRCM_BUFFER_POOL_STAT_SAI_TO_BCM(sai_buffer_pool_stat_t sai_stat, bcm_cosq_stat_t *stat)
{
    switch(sai_stat)
    {
        case SAI_BUFFER_POOL_STAT_WATERMARK_BYTES: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
        default: *stat = 0;
            return SAI_STATUS_NOT_SUPPORTED;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_32bit_size_check(sai_meter_type_t meter_type,
                           sai_uint64_t test_var)
{
    if (SAI_METER_TYPE_PACKETS == meter_type)
    {
        if (test_var & _BRCM_SAI_MASK_64_UPPER_32)
        {
            return SAI_STATUS_FAILURE;
        }
    }
    else
    {
        if (((test_var * 8) / 1000) & _BRCM_SAI_MASK_64_UPPER_32)
        {
            return SAI_STATUS_FAILURE;
        }
    }
    return SAI_STATUS_SUCCESS;
}

/*******************************Closed code************************************/

void _verify(int marker, void *ptr)
{
    assert(marker == *((int*)(ptr-8)));
}

uint8_t
_brcm_sai_to_syslog(uint8_t _sai_level)
{
    switch(_sai_level)
    {
        case SAI_LOG_LEVEL_DEBUG:
            return LOG_DEBUG;
            break;
        case SAI_LOG_LEVEL_INFO:
            return LOG_INFO;
            break;
        case SAI_LOG_LEVEL_WARN:
            return LOG_WARNING;
            break;
        case SAI_LOG_LEVEL_ERROR:
            return LOG_ERR;
            break;
        case SAI_LOG_LEVEL_CRITICAL:
            return LOG_CRIT;
            break;
        default:
            return LOG_DEBUG;
            break;
    }
    return LOG_DEBUG;
}

sai_status_t
BRCM_STAT_SAI_TO_BCM(sai_port_stat_t sai_stat, bcm_stat_val_t *stat)
{
    switch(sai_stat)
    {
        case SAI_PORT_STAT_IF_IN_OCTETS:
            *stat = snmpIfInOctets;
            break;
        case SAI_PORT_STAT_IF_IN_UCAST_PKTS:
            *stat = snmpIfInUcastPkts;
            break;
        case SAI_PORT_STAT_IF_IN_NON_UCAST_PKTS:
            *stat = snmpIfInNUcastPkts;
            break;
        case SAI_PORT_STAT_IF_IN_DISCARDS:
            *stat = snmpIfInDiscards;
            break;
        case SAI_PORT_STAT_IF_IN_ERRORS:
            *stat = snmpIfInErrors;
            break;
        case SAI_PORT_STAT_IF_IN_UNKNOWN_PROTOS:
            *stat = snmpIfInUnknownProtos;
            break;
        case SAI_PORT_STAT_IF_IN_BROADCAST_PKTS:
            *stat = snmpIfInBroadcastPkts;
            break;
        case SAI_PORT_STAT_IF_IN_MULTICAST_PKTS:
            *stat = snmpIfInMulticastPkts;
            break;
        case SAI_PORT_STAT_IF_IN_VLAN_DISCARDS:
            *stat = snmpBcmCustomReceive3;
            break;
        case SAI_PORT_STAT_IF_OUT_OCTETS:
            *stat = snmpIfOutOctets;
            break;
        case SAI_PORT_STAT_IF_OUT_UCAST_PKTS:
            *stat = snmpIfOutUcastPkts;
            break;
        case SAI_PORT_STAT_IF_OUT_NON_UCAST_PKTS:
            *stat = snmpIfOutNUcastPkts;
            break;
        case SAI_PORT_STAT_IF_OUT_DISCARDS:
            *stat = snmpIfOutDiscards;
            break;
        case SAI_PORT_STAT_IF_OUT_ERRORS:
            *stat = snmpIfOutErrors;
            break;
        case SAI_PORT_STAT_IF_OUT_QLEN:
            *stat = snmpIfOutQLen;
            break;
        case SAI_PORT_STAT_IF_OUT_BROADCAST_PKTS:
            *stat = snmpIfOutBroadcastPkts;
            break;
        case SAI_PORT_STAT_IF_OUT_MULTICAST_PKTS:
            *stat = snmpIfOutMulticastPkts;
            break;
        case SAI_PORT_STAT_ETHER_STATS_DROP_EVENTS:
            *stat = snmpEtherStatsDropEvents;
            break;
        case SAI_PORT_STAT_ETHER_STATS_MULTICAST_PKTS:
            *stat = snmpEtherStatsMulticastPkts;
            break;
        case SAI_PORT_STAT_ETHER_STATS_BROADCAST_PKTS:
            *stat = snmpEtherStatsBroadcastPkts;
            break;
        case SAI_PORT_STAT_ETHER_STATS_UNDERSIZE_PKTS:
            *stat = snmpEtherStatsUndersizePkts;
            break;
        case SAI_PORT_STAT_ETHER_STATS_FRAGMENTS:
            *stat = snmpEtherStatsFragments;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_64_OCTETS:
            *stat = snmpEtherStatsPkts64Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_65_TO_127_OCTETS:
            *stat = snmpEtherStatsPkts65to127Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_128_TO_255_OCTETS:
            *stat = snmpEtherStatsPkts128to255Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_256_TO_511_OCTETS:
            *stat = snmpEtherStatsPkts256to511Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_512_TO_1023_OCTETS:
            *stat = snmpEtherStatsPkts512to1023Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_1024_TO_1518_OCTETS:
            *stat = snmpEtherStatsPkts1024to1518Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_1519_TO_2047_OCTETS:
            *stat = snmpBcmReceivedPkts1519to2047Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_2048_TO_4095_OCTETS:
            *stat = snmpBcmEtherStatsPkts2048to4095Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_4096_TO_9216_OCTETS:
            *stat = snmpBcmEtherStatsPkts4095to9216Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS_9217_TO_16383_OCTETS:
            *stat = snmpBcmEtherStatsPkts9217to16383Octets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_OVERSIZE_PKTS:
            *stat = snmpEtherStatsOversizePkts;
            break;
        case SAI_PORT_STAT_ETHER_RX_OVERSIZE_PKTS:
            *stat = snmpEtherRxOversizePkts;
            break;
        case SAI_PORT_STAT_ETHER_TX_OVERSIZE_PKTS:
            *stat = snmpEtherTxOversizePkts;
            break;
        case SAI_PORT_STAT_ETHER_STATS_JABBERS:
            *stat = snmpEtherStatsJabbers;
            break;
        case SAI_PORT_STAT_ETHER_STATS_OCTETS:
            *stat = snmpEtherStatsOctets;
            break;
        case SAI_PORT_STAT_ETHER_STATS_PKTS:
            *stat = snmpEtherStatsPkts;
            break;
        case SAI_PORT_STAT_ETHER_STATS_COLLISIONS:
            *stat = snmpEtherStatsCollisions;
            break;
        case SAI_PORT_STAT_ETHER_STATS_CRC_ALIGN_ERRORS:
            *stat = snmpEtherStatsCRCAlignErrors;
            break;
        case SAI_PORT_STAT_ETHER_STATS_TX_NO_ERRORS:
            *stat = snmpEtherStatsTXNoErrors;
            break;
        case SAI_PORT_STAT_ETHER_STATS_RX_NO_ERRORS:
            *stat = snmpEtherStatsRXNoErrors;
            break;
        case SAI_PORT_STAT_IP_IN_RECEIVES:
            *stat = snmpBcmCustomReceive4;
            break;
        case SAI_PORT_STAT_IP_IN_OCTETS:
            *stat = snmpIfInOctets; /* Not supported */
            break;
        case SAI_PORT_STAT_IP_IN_UCAST_PKTS:
            *stat = snmpIpInReceives;
            break;
        case SAI_PORT_STAT_IP_IN_NON_UCAST_PKTS:
            *stat = snmpBcmCustomReceive5;
            break;
        case SAI_PORT_STAT_IP_IN_DISCARDS:
            *stat = snmpIpInDiscards;
            break;
        case SAI_PORT_STAT_IP_OUT_OCTETS:
            *stat = snmpIfOutOctets; /* Not supported */
            break;
        case SAI_PORT_STAT_IP_OUT_UCAST_PKTS:
            *stat = snmpBcmCustomTransmit6;
            break;
        case SAI_PORT_STAT_IP_OUT_NON_UCAST_PKTS:
            *stat = snmpBcmCustomTransmit7;
            break;
        case SAI_PORT_STAT_IP_OUT_DISCARDS:
            *stat = snmpBcmCustomTransmit8;
            break;
        case SAI_PORT_STAT_IPV6_IN_RECEIVES:
            *stat = snmpIpv6IfStatsInReceives;
            break;
        case SAI_PORT_STAT_IPV6_IN_OCTETS:
            *stat = snmpIfInOctets; /* Not supported */
            break;
        case SAI_PORT_STAT_IPV6_IN_UCAST_PKTS:
            *stat = snmpBcmCustomReceive6;
            break;
        case SAI_PORT_STAT_IPV6_IN_NON_UCAST_PKTS:
            *stat = snmpIpv6IfStatsInMcastPkts;
            break;
        case SAI_PORT_STAT_IPV6_IN_MCAST_PKTS:
            *stat = snmpIpv6IfStatsInMcastPkts;
            break;
        case SAI_PORT_STAT_IPV6_IN_DISCARDS:
            *stat = snmpIpv6IfStatsInDiscards;
            break;
        case SAI_PORT_STAT_IPV6_OUT_OCTETS:
            *stat = snmpIfOutOctets; /* Not supported */
            break;
        case SAI_PORT_STAT_IPV6_OUT_UCAST_PKTS:
            *stat = snmpBcmCustomTransmit9;
            break;
        case SAI_PORT_STAT_IPV6_OUT_NON_UCAST_PKTS:
            *stat = snmpIpv6IfStatsOutMcastPkts;
            break;
        case SAI_PORT_STAT_IPV6_OUT_MCAST_PKTS:
            *stat = snmpIpv6IfStatsOutMcastPkts;
            break;
        case SAI_PORT_STAT_IPV6_OUT_DISCARDS:
            *stat = snmpIpv6IfStatsOutDiscards;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_64_OCTETS:
            *stat = snmpBcmReceivedPkts64Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_65_TO_127_OCTETS:
            *stat = snmpBcmReceivedPkts65to127Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_128_TO_255_OCTETS:
            *stat = snmpBcmReceivedPkts128to255Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_256_TO_511_OCTETS:
            *stat = snmpBcmReceivedPkts256to511Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_512_TO_1023_OCTETS:
            *stat = snmpBcmReceivedPkts512to1023Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_1024_TO_1518_OCTETS:
            *stat = snmpBcmReceivedPkts1024to1518Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_1519_TO_2047_OCTETS:
            *stat = snmpBcmReceivedPkts1519to2047Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_2048_TO_4095_OCTETS:
            *stat = snmpBcmReceivedPkts2048to4095Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_4096_TO_9216_OCTETS:
            *stat = snmpBcmReceivedPkts4095to9216Octets;
            break;
        case SAI_PORT_STAT_ETHER_IN_PKTS_9217_TO_16383_OCTETS:
            *stat = snmpBcmReceivedPkts9217to16383Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_64_OCTETS:
            *stat = snmpBcmTransmittedPkts64Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_65_TO_127_OCTETS:
            *stat = snmpBcmTransmittedPkts65to127Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_128_TO_255_OCTETS:
            *stat = snmpBcmTransmittedPkts128to255Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_256_TO_511_OCTETS:
            *stat = snmpBcmTransmittedPkts256to511Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_512_TO_1023_OCTETS:
            *stat = snmpBcmTransmittedPkts512to1023Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_1024_TO_1518_OCTETS:
            *stat = snmpBcmTransmittedPkts1024to1518Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_1519_TO_2047_OCTETS:
            *stat = snmpBcmTransmittedPkts1519to2047Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_2048_TO_4095_OCTETS:
            *stat = snmpBcmTransmittedPkts2048to4095Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_4096_TO_9216_OCTETS:
            *stat = snmpBcmTransmittedPkts4095to9216Octets;
            break;
        case SAI_PORT_STAT_ETHER_OUT_PKTS_9217_TO_16383_OCTETS:
            *stat = snmpBcmTransmittedPkts9217to16383Octets;
            break;
        case SAI_PORT_STAT_PFC_0_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority0;
            break;
        case SAI_PORT_STAT_PFC_0_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority0;
            break;
        case SAI_PORT_STAT_PFC_1_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority1;
            break;
        case SAI_PORT_STAT_PFC_1_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority1;
            break;
        case SAI_PORT_STAT_PFC_2_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority2;
            break;
        case SAI_PORT_STAT_PFC_2_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority2;
            break;
        case SAI_PORT_STAT_PFC_3_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority3;
            break;
        case SAI_PORT_STAT_PFC_3_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority3;
            break;
        case SAI_PORT_STAT_PFC_4_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority4;
            break;
        case SAI_PORT_STAT_PFC_4_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority4;
            break;
        case SAI_PORT_STAT_PFC_5_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority5;
            break;
        case SAI_PORT_STAT_PFC_5_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority5;
            break;
        case SAI_PORT_STAT_PFC_6_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority6;
            break;
        case SAI_PORT_STAT_PFC_6_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority6;
            break;
        case SAI_PORT_STAT_PFC_7_RX_PKTS:
            *stat = snmpBcmRxPFCFramePriority7;
            break;
        case SAI_PORT_STAT_PFC_7_TX_PKTS:
            *stat = snmpBcmTxPFCFramePriority7;
            break;
        case SAI_PORT_STAT_PFC_0_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority0;
            break;
        case SAI_PORT_STAT_PFC_1_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority1;
            break;
        case SAI_PORT_STAT_PFC_2_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority2;
            break;
        case SAI_PORT_STAT_PFC_3_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority3;
            break;
        case SAI_PORT_STAT_PFC_4_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority4;
            break;
        case SAI_PORT_STAT_PFC_5_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority5;
            break;
        case SAI_PORT_STAT_PFC_6_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority6;
            break;
        case SAI_PORT_STAT_PFC_7_ON2OFF_RX_PKTS:
            *stat = snmpBcmRxPFCFrameXonPriority7;
            break;

        case SAI_PORT_STAT_IN_WATERMARK_BYTES:
        case SAI_PORT_STAT_IN_SHARED_WATERMARK_BYTES:
        case SAI_PORT_STAT_OUT_WATERMARK_BYTES:
        case SAI_PORT_STAT_OUT_SHARED_WATERMARK_BYTES:
        
        default:
            return SAI_STATUS_NOT_SUPPORTED;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_stat_get(int port, int type, uint64 *count)
{
        sai_status_t rv;

        if (1 == type) /*Note: convert to switch case in the future */
        {
            rv = bcm_stat_get(0, port,
                              snmpBcmTransmittedPkts1519to2047Octets, count);
            BRCM_SAI_API_CHK(SAI_API_HASH, "stat get", rv);
        }
        else
        {
            return SAI_STATUS_FAILURE;
        }
        return rv;
}

sai_int32_t
BRCM_IPTYPE_SAI_TO_BCM(sai_int32_t iptype)
{
    switch(iptype)
    {
        case SAI_ACL_IP_TYPE_ANY:
            return bcmFieldIpTypeAny;
        case SAI_ACL_IP_TYPE_IP:
            return bcmFieldIpTypeIp;
        case SAI_ACL_IP_TYPE_NON_IP:
            return bcmFieldIpTypeNonIp;
        case SAI_ACL_IP_TYPE_IPV4ANY:
            return bcmFieldIpTypeIpv4Any;
        case SAI_ACL_IP_TYPE_NON_IPV4:
            return bcmFieldIpTypeIpv4Not;
        case SAI_ACL_IP_TYPE_IPV6ANY:
            return bcmFieldIpTypeIpv6;
        case SAI_ACL_IP_TYPE_NON_IPV6:
            return bcmFieldIpTypeIpv6Not;
        case SAI_ACL_IP_TYPE_ARP:
            return bcmFieldIpTypeArp;
        case SAI_ACL_IP_TYPE_ARP_REQUEST:
            return bcmFieldIpTypeArpRequest;
        case SAI_ACL_IP_TYPE_ARP_REPLY:
            return bcmFieldIpTypeArpReply;
        default:
            return bcmFieldIpTypeAny;
    }
}

sai_int32_t
BRCM_IPTYPE_BCM_TO_SAI(sai_int32_t iptype)
{
    switch(iptype)
    {
        case bcmFieldIpTypeAny:
            return SAI_ACL_IP_TYPE_ANY;
        case bcmFieldIpTypeIp:
            return SAI_ACL_IP_TYPE_IP;
        case bcmFieldIpTypeNonIp:
            return SAI_ACL_IP_TYPE_NON_IP;
        case bcmFieldIpTypeIpv4Any:
            return SAI_ACL_IP_TYPE_IPV4ANY;
        case bcmFieldIpTypeIpv4Not:
            return SAI_ACL_IP_TYPE_NON_IPV4;
        case bcmFieldIpTypeIpv6:
            return SAI_ACL_IP_TYPE_IPV6ANY;
        case bcmFieldIpTypeIpv6Not:
            return SAI_ACL_IP_TYPE_NON_IPV6;
        case bcmFieldIpTypeArp:
            return SAI_ACL_IP_TYPE_ARP;
        case bcmFieldIpTypeArpRequest:
            return SAI_ACL_IP_TYPE_ARP_REQUEST;
        case bcmFieldIpTypeArpReply:
            return SAI_ACL_IP_TYPE_ARP_REPLY;
        default:
            return SAI_ACL_IP_TYPE_ANY;
    }
}

sai_int32_t
BRCM_IPFRAG_SAI_TO_BCM(sai_int32_t ipfrag)
{
    switch(ipfrag)
    {
        case SAI_ACL_IP_FRAG_ANY:
            return bcmFieldIpFragAny;
        case SAI_ACL_IP_FRAG_NON_FRAG:
            return bcmFieldIpFragNon;
        case SAI_ACL_IP_FRAG_NON_FRAG_OR_HEAD:
            return bcmFieldIpFragNonOrFirst;
        case SAI_ACL_IP_FRAG_HEAD:
            return bcmFieldIpFragFirst;
        case SAI_ACL_IP_FRAG_NON_HEAD:
            return bcmFieldIpFragNotFirst;
        default:
            return bcmFieldIpFragAny;
    }
}

sai_int32_t
BRCM_IPFRAG_BCM_TO_SAI(sai_int32_t ipfrag)
{
    switch(ipfrag)
    {
        case bcmFieldIpFragAny:
            return SAI_ACL_IP_FRAG_ANY;
        case bcmFieldIpFragNon:
            return SAI_ACL_IP_FRAG_NON_FRAG;
        case bcmFieldIpFragNonOrFirst:
            return SAI_ACL_IP_FRAG_NON_FRAG_OR_HEAD;
        case bcmFieldIpFragFirst:
            return SAI_ACL_IP_FRAG_HEAD;
        case bcmFieldIpFragNotFirst:
            return SAI_ACL_IP_FRAG_NON_HEAD;
        default:
            return SAI_ACL_IP_FRAG_ANY;
    }
}

sai_status_t
_brcm_udf_hash_config_add(int id, int len, uint8_t *mask)
{
    int m;
    bcm_udf_hash_config_t config;
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_HASH);

    bcm_udf_hash_config_t_init(&config);
    config.udf_id = id;
    config.mask_length = len;
    for (m = 0; m < config.mask_length; m++)
    {
        config.hash_mask[m] = mask[m];
    }
    rv = bcm_udf_hash_config_add(0, 0, &config);
    BRCM_SAI_API_CHK(SAI_API_HASH, "udf hash config add", rv);

    BRCM_SAI_FUNCTION_EXIT(SAI_API_HASH);

    return rv;
}

sai_status_t
_brcm_sai_l3_config_get(int id, int *max)
{
    sai_status_t rv;
    bcm_l3_info_t l3_info;

    if (_L3_CONFIGS_MAX < id)
    {
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if (-1 != _l3_config_info[id])
    {
        *max = _l3_config_info[id];
        return SAI_STATUS_SUCCESS;
    }
    bcm_l3_info_t_init(&l3_info);
    rv = bcm_l3_info(0, &l3_info);
    BRCM_SAI_API_CHK(SAI_API_SWITCH, "L3 info", rv);

    switch (id)
    {
        case 1: *max = l3_info.l3info_max_ecmp;
            break;
        case 2: *max = l3_info.l3info_max_nexthop;
            break;
        case 3: *max = l3_info.l3info_max_tunnel_init;
            break;
        case 4: *max = l3_info.l3info_used_tunnel_init;
            break;
        case 5: *max = l3_info.l3info_max_ecmp_groups;
            break;
        case 6: *max = l3_info.l3info_max_route;
            break;
        case 7: *max = l3_info.l3info_max_intf;
            break;
        case 8: *max = l3_info.l3info_max_host;
            break;
        default:
            return SAI_STATUS_FAILURE;
    }
    _l3_config_info[id] = *max;

    return rv;
}

sai_status_t
_brcm_sai_get_local_mac(sai_mac_t local_mac_address)
{
    sai_status_t rv;
    _brcm_sai_data_t data;

    rv = _brcm_sai_global_data_get(_BRCM_SAI_SYSTEM_MAC_SET, &data);
    BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "getting system mac state global data", rv);

    if (data.bool_data)
    {
        rv = _brcm_sai_global_data_get(_BRCM_SAI_SYSTEM_MAC, &data);
        BRCM_SAI_RV_CHK(SAI_API_TUNNEL, "getting system mac global data", rv);

        sal_memcpy(local_mac_address, data.mac, sizeof(data.mac));
    }
    else
    {
        return SAI_STATUS_FAILURE;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t
_brcm_sai_tunnel_encap(bcm_l3_intf_t *l3_intf, bcm_tunnel_initiator_t *tunnel_init)
{
    sai_status_t rv;
    bcm_tunnel_initiator_t bcm_tunnel_init;

    /* Create initiator using this new intf */
    bcm_tunnel_initiator_t_init(&bcm_tunnel_init);
    bcm_tunnel_init.sip = tunnel_init->sip;
    bcm_tunnel_init.dip = tunnel_init->dip;
    bcm_tunnel_init.type = tunnel_init->type;
    bcm_tunnel_init.ttl = tunnel_init->ttl;
    bcm_tunnel_init.dscp = tunnel_init->dscp;
    bcm_tunnel_init.dscp_sel = tunnel_init->dscp_sel;
    rv = bcm_tunnel_initiator_set(0, (bcm_l3_intf_t*)l3_intf, &bcm_tunnel_init);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "tunnel initiator set", rv);

    return rv;
}

sai_status_t
_brcm_l3_egress_ecmp_add(bcm_l3_egress_ecmp_t *ecmp_object, bcm_if_t if_id)
{
    sai_status_t rv;
    bcm_l3_egress_ecmp_t bcm_ecmp_object;

    bcm_l3_egress_ecmp_t_init(&bcm_ecmp_object);
    bcm_ecmp_object.ecmp_intf = ecmp_object->ecmp_intf;
    bcm_ecmp_object.ecmp_group_flags |= BCM_L3_ECMP_O_REPLACE;
    rv = bcm_l3_egress_ecmp_add(0, &bcm_ecmp_object, if_id);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp nh group add", rv);

    return rv;
}

sai_status_t
_brcm_l3_egress_ecmp_get(bcm_l3_egress_ecmp_t *ecmp_object, int max,
                         bcm_if_t *intfs, int *count)
{
    sai_status_t rv;
    bcm_l3_egress_ecmp_t bcm_ecmp_object;

    bcm_l3_egress_ecmp_t_init(&bcm_ecmp_object);
    bcm_ecmp_object.ecmp_intf = ecmp_object->ecmp_intf;
    rv = bcm_l3_egress_ecmp_get(0, &bcm_ecmp_object, max, (bcm_if_t*)intfs, count);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP_GROUP, "ecmp nh group get", rv);

    return rv;
}

sai_status_t
_brcm_sai_switch_config_get(int id, int *val)
{
    sai_status_t rv;

    if (1 == id) /* Note: use switch case in the future */
    {
        rv = bcm_switch_control_get(0, bcmSwitchVrfMax, val);
        BRCM_SAI_API_CHK(SAI_API_VIRTUAL_ROUTER, "Switch Vrf max", rv);
    }
    else
    {
        return SAI_STATUS_FAILURE;
    }

    return rv;
}

sai_status_t
_brcm_sai_cosq_config(int port, int id, int type, int val)
{
    sai_status_t rv;

    BRCM_SAI_FUNCTION_ENTER(SAI_API_BUFFER);

    switch (type)
    {
        case 1:
            rv = bcm_cosq_control_set(0, port, id, bcmCosqControlIngressPool,
                                      val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;

        case 2:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlIngressPortPGSharedDynamicEnable, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 3:
        {
            switch (val)
            {
                case -7:
                    val = bcmCosqControlDropLimitAlpha_1_128;
                    break;
                case -6:
                    val = bcmCosqControlDropLimitAlpha_1_64;
                    break;
                case -5:
                    val = bcmCosqControlDropLimitAlpha_1_32;
                    break;
                case -4:
                    val = bcmCosqControlDropLimitAlpha_1_16;
                    break;
                case -3:
                    val = bcmCosqControlDropLimitAlpha_1_8;
                    break;
                case -2:
                    val = bcmCosqControlDropLimitAlpha_1_4;
                    break;
                case -1:
                    val = bcmCosqControlDropLimitAlpha_1_2;
                    break;
                case 0:
                    val = bcmCosqControlDropLimitAlpha_1;
                    break;
                case 1:
                    val = bcmCosqControlDropLimitAlpha_2;
                    break;
                case 2:
                    val = bcmCosqControlDropLimitAlpha_4;
                    break;
                case 3:
                    val = bcmCosqControlDropLimitAlpha_8;
                    break;
                default:
                    BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR, "Unsupported Alpha value.\n");
                    return SAI_STATUS_INVALID_PARAMETER;
            }
            rv = bcm_cosq_control_set(0, port, id,
                                      bcmCosqControlDropLimitAlpha, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        }
        case 4:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlIngressPortPGSharedLimitBytes, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 5:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlIngressPortPGMinLimitBytes, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 6:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlIngressPortPoolMaxLimitBytes, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 7:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlIngressPortPoolMinLimitBytes, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 8:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlIngressPortPoolMaxLimitBytes, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 9:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlIngressPortPoolMinLimitBytes, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 10:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlBandwidthBurstMin, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 11:
            rv = bcm_cosq_control_set(0, port, id,
                 bcmCosqControlBandwidthBurstMax, val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 12:
            rv = bcm_cosq_control_set(0, port, id,
                                      bcmCosqControlUCEgressPool,
                                      val); 
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 13:
            rv = bcm_cosq_control_set(0, port, id,
                                      bcmCosqControlEgressUCSharedDynamicEnable, 
                                      val);
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        case 14:
            rv = bcm_cosq_control_set(0, port, id,
                                      bcmCosqControlEgressUCQueueSharedLimitBytes, 
                                      val);            
            BRCM_SAI_API_CHK(SAI_API_BUFFER, "cosq control set", rv);
            break;
        default:
            return SAI_STATUS_INVALID_PARAMETER;
    }

    BRCM_SAI_FUNCTION_EXIT(SAI_API_BUFFER);
    return rv;
}

sai_status_t
_brcm_sai_cosq_stat_get(int port, int qid, bcm_gport_t gport,
                        _In_ const sai_queue_stat_t counter_id,
                        _Out_ uint64_t* counter)
{
    sai_status_t rv;
    bcm_cosq_stat_t stat;

    rv = BRCM_QOS_STAT_SAI_TO_BCM(counter_id, &stat);
    if (SAI_STATUS_ERROR(rv))
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_NOTICE,
                           "Unknown or unsupported stat type: %d.\n", counter_id);
        return rv;
    }
    if (_BRCM_SAI_IS_CPU_PORT(port))
    {
        rv = bcm_cosq_stat_get(0, gport, qid, stat, (uint64*)counter);
    }
    else
    {
        if ((SAI_QUEUE_STAT_PACKETS == counter_id) ||
            (SAI_QUEUE_STAT_CURR_OCCUPANCY_BYTES == counter_id))
        {
            rv = bcm_cosq_stat_sync_get(0, gport, -1, stat, (uint64*)counter);
        }
        else if ((SAI_QUEUE_STAT_WATERMARK_BYTES == counter_id) ||
                 (SAI_QUEUE_STAT_SHARED_WATERMARK_BYTES == counter_id))
        {
            rv = bcm_cosq_bst_stat_sync(0, stat);
            BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq bst stat sync", rv);
            rv = bcm_cosq_bst_stat_get(0, gport, qid, stat, 0, (uint64*)counter);
            if (SAI_QUEUE_STAT_SHARED_WATERMARK_BYTES == counter_id)
            {
                if (*counter) /* if non zero */
                {
                    *counter *= BRCM_MMU_B_PER_C;
                    if (_sai_queue_min[port][qid].valid) /* check if cached */
                    {
                        if (_sai_queue_min[port][qid].val) /* if non zero */
                        {
                            if (_sai_queue_min[port][qid].val >= *counter)
                            {
                                *counter = 0;
                            }
                            else
                            {
                                *counter -= _sai_queue_min[port][qid].val;
                            }
                        }
                    }
                    else /* queue buff profile not applied yet, so cache it from h/w */
                    {
                        int val;

                        rv = driverEgressQueueMinLimitGet(0, port, qid, &val);
                        BRCM_SAI_API_CHK(SAI_API_QUEUE, "queue egress Min limit get", rv);
                        if (val)
                        {
                            if (val >= *counter)
                            {
                                *counter = 0;
                            }
                            else
                            {
                                *counter -= val;
                            }
                        }
                        _sai_queue_min[port][qid].val = val;
                        _sai_queue_min[port][qid].valid = TRUE;
                    }
                }
            }
        }
        else
        {
            rv = bcm_cosq_stat_get(0, gport, -1, stat, (uint64*)counter);
        }
    }
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq stat get", rv);

    if (!DEV_IS_HX4() &&
        (SAI_QUEUE_STAT_CURR_OCCUPANCY_BYTES == counter_id))
    {
        uint64 tmp_ctr;
        stat = bcmCosqStatEgressUCQueueMinBytesCurrent;
        rv = bcm_cosq_stat_get(0, gport, -1, stat, &tmp_ctr);
        BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq stat get", rv);
        *counter += tmp_ctr;
    }

    if (SAI_QUEUE_STAT_DROPPED_PACKETS == counter_id)
    {
        /* Only consider unicast queue*/
        int num_queues = _brcm_sai_get_num_queues();
        if (qid >= 0 && qid < num_queues)
        {
            int index;
            uint64 tmp_ctr = 0;
            for (index=0; index < port_tc_queue_map_cache[port][qid].size; index++)
            {
                uint32 prio = BRCM_SAI_GET_OBJ_VAL(uint32, port_tc_queue_map_cache[port][qid].tc[index]);
                bcm_stat_value_t stat;
                rv = bcm_stat_flex_counter_get(0, egress_port_flex_counter_id_map[port],
                        bcmStatFlexStatPackets, 1, &prio, &stat);
                BRCM_SAI_API_CHK(SAI_API_QUEUE, "get egress flex counter", rv);
                tmp_ctr += stat.packets64;
            }

            *counter += tmp_ctr;
        }
    }

    return rv;
}

sai_status_t
_brcm_sai_cosq_stat_set(int port, int qid, bcm_gport_t gport,
                        _In_ const sai_queue_stat_t counter_id,
                        _In_ uint64_t counter)

{
    sai_status_t rv;
    bcm_cosq_stat_t stat;

    rv = BRCM_QOS_STAT_SAI_TO_BCM(counter_id, &stat);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                           "Unknown or unsupported stat type: %d.\n", counter_id);
        return rv;
    }
    if (_BRCM_SAI_IS_CPU_PORT(port))
    {
        rv = bcm_cosq_stat_set(0, gport, qid, stat, counter);
    }
    else if ((SAI_QUEUE_STAT_WATERMARK_BYTES == counter_id) ||
             (SAI_QUEUE_STAT_SHARED_WATERMARK_BYTES == counter_id))
    {
        rv = bcm_cosq_bst_stat_clear(0, gport, qid, stat);
    }
    else
    {
        rv = bcm_cosq_stat_set(0, gport, -1, stat, counter);
    }
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq stat set", rv);


    return rv;
}

sai_status_t
_brcm_sai_ingress_pg_stat_get(int port, int pg, bcm_gport_t gport,
                              _In_ const sai_ingress_priority_group_stat_t counter_id,
                              _Out_ uint64_t* counter)
{
    sai_status_t rv;
    bcm_cosq_stat_t stat;

    rv = BRCM_INGRESS_PG_STAT_SAI_TO_BCM(counter_id, &stat);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                           "Unknown or unsupported stat type: %d.\n", counter_id);
        return rv;
    }
    rv = bcm_cosq_bst_stat_sync(0, stat);
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "ingress pg bst stat sync", rv);
    rv = bcm_cosq_bst_stat_get(0, gport, pg, stat, 0, (uint64*)counter);
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "ingress pg bst stat get", rv);
    *counter *= BRCM_MMU_B_PER_C;

    return rv;
}

sai_status_t
_brcm_sai_ingress_pg_stat_set(int port, int pg, bcm_gport_t gport,
                              _In_ const sai_ingress_priority_group_stat_t counter_id,
                              _In_ uint64_t counter)
{
    sai_status_t rv;
    bcm_cosq_stat_t stat;

    rv = BRCM_INGRESS_PG_STAT_SAI_TO_BCM(counter_id, &stat);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                           "Unknown or unsupported stat type: %d.\n", counter_id);
        return rv;
    }

    rv = bcm_cosq_bst_stat_clear(0, gport, pg, stat);
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "ingress pg bst stat clear", rv);

    return rv;
}

sai_status_t
_brcm_sai_buffer_pool_stat_get(int port, int pg, bcm_gport_t gport,
                              _In_ const sai_ingress_priority_group_stat_t counter_id,
                              _Out_ uint64_t* counter)
{
    sai_status_t rv;
    bcm_cosq_stat_t stat;

    rv = BRCM_BUFFER_POOL_STAT_SAI_TO_BCM(counter_id, &stat);
    if (SAI_STATUS_SUCCESS != rv)
    {
        BRCM_SAI_LOG_QUEUE(SAI_LOG_LEVEL_ERROR,
                           "Unknown or unsupported stat type: %d.\n", counter_id);
        return rv;
    }

    if (_BRCM_SAI_IS_CPU_PORT(port))
    {
        rv = bcm_cosq_stat_get(0, gport, pg, stat, (uint64*)counter);
    }
    else
    {
        rv = bcm_cosq_stat_get(0, gport, -1, stat, (uint64*)counter);
    }
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "ingress pg stat get", rv);


    return rv;
}

sai_status_t
_brcm_sai_cosq_bandwidth_set(bcm_gport_t parent_gport,
                             bcm_cos_queue_t cosq,
                             _brcm_sai_qos_scheduler_t *scheduler)
{
    uint32_t flags = 0;
    sai_status_t rv;

    if (SAI_METER_TYPE_PACKETS == scheduler->shaper_type)
    {
        flags = BCM_COSQ_BW_PACKET_MODE;
    }
    rv = bcm_cosq_gport_bandwidth_set(0, parent_gport, cosq,
                                      (uint32) scheduler->minimum_bandwidth,
                                      (uint32) scheduler->maximum_bandwidth,
                                      flags);
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "cosq gport bandwidth set", rv);

    return rv;
}

sai_status_t
_brcm_sai_cosq_state_get(int unit, int port, int qid, int attr, int *val)
{
    uint32 rval;
    sai_status_t rv;

    if (SAI_QUEUE_ATTR_PAUSE_STATUS != attr)
    {
        return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
    }
    rv = READ_XPORT_TO_MMU_BKPr(unit, port, &rval);
    BRCM_SAI_API_CHK(SAI_API_QUEUE, "reg read", rv);

    *val = (rval & (1 << qid)) ? TRUE : FALSE;

    return rv;
}

sai_status_t
_brcm_sai_l3_route_config(bool add_del, int cid, bcm_l3_route_t *l3_rt)
{
    sai_status_t rv;
    bcm_l3_route_t bcm_l3_rt;

    sal_memcpy(&bcm_l3_rt, l3_rt, sizeof(bcm_l3_rt));
    bcm_l3_rt.l3a_lookup_class = cid;
    if (add_del)
    {
        rv = bcm_l3_route_add(0, &bcm_l3_rt);
        BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 route add", rv);
    }
    else
    {
        rv = bcm_l3_route_delete(0, &bcm_l3_rt);
        BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 route delete", rv);
    }

    return rv;
}

sai_status_t
_brcm_sai_l3_host_config(bool v6_128, int cid, bcm_l3_host_t *l3_host,
                         bcm_l3_egress_t *l3_egr)
{
    bcm_l3_host_t bcm_l3_host;
    int i, count = v6_128 ? 0 : 3;
    sai_status_t rv = SAI_STATUS_SUCCESS;
    uint8 v6_15 = l3_host->l3a_ip6_addr[15];

    sal_memcpy(&bcm_l3_host, l3_host, sizeof(bcm_l3_host));
    sal_memcpy(bcm_l3_host.l3a_nexthop_mac, l3_egr->mac_addr,
               sizeof(bcm_mac_t));
    bcm_l3_host.l3a_lookup_class = cid;
    if (l3_egr->flags & BCM_L3_TGID)
    {
        bcm_l3_host.l3a_port_tgid = l3_egr->trunk;
    }
    else
    {
        bcm_l3_host.l3a_port_tgid = l3_egr->port;
    }
    for (i=0; i<=count; i++)
    {
        bcm_l3_host.l3a_ip6_addr[15] = v6_15 | i;
        rv = bcm_l3_host_add(0, &bcm_l3_host);
        BRCM_SAI_API_CHK(SAI_API_ROUTE, "L3 v6 host route add", rv);
    }

    return rv;
}

sai_status_t
_brcm_sai_l2_station_add(int tid, bcm_l2_station_t *l2_stn, int *station_id)
{
    sai_status_t rv;
    bcm_l2_station_t bcm_l2_stn;

    bcm_l2_station_t_init(&bcm_l2_stn);
    bcm_l2_stn.flags = BCM_L2_STATION_IPV4 | BCM_L2_STATION_IPV6 | BCM_L2_STATION_ARP_RARP;
    sal_memcpy(bcm_l2_stn.dst_mac, l2_stn->dst_mac, sizeof(bcm_l2_stn.dst_mac));
    sal_memcpy(bcm_l2_stn.dst_mac_mask, l2_stn->dst_mac_mask, sizeof(bcm_l2_stn.dst_mac_mask));

    bcm_l2_stn.vlan = l2_stn->vlan;
    bcm_l2_stn.vlan_mask = 0xfff;

    if (tid)
    {
        bcm_l2_stn.src_port_mask = 0x7ff;
        bcm_l2_stn.src_port = tid;
    }
    rv = bcm_l2_station_add(0, station_id, &bcm_l2_stn);
    if (rv == BCM_E_EXISTS)
    {
        /* Some devices do not add duplicate entries. */
        rv = BCM_E_NONE;
        *station_id = 0;
    }
    BRCM_SAI_API_CHK(SAI_API_ROUTER_INTERFACE, "Add my stn entry", rv);

    return rv;
}

extern int bcm_esw_stk_my_modid_get(int,int *);
extern int _bcm_esw_stk_modmap_map(int unit, int setget,
                                   bcm_module_t mod_in, bcm_port_t port_in,
                                   bcm_module_t *mod_out, bcm_port_t *port_out);
bcm_gport_t
_brcm_sai_gport_get(bcm_port_t pp)
{
    int rv;
    bcm_gport_t gport;
    bcm_module_t my_modid, modid_out;
    bcm_port_t port_out;

    if (BCM_GPORT_IS_SET(pp))
    {
        return pp;
    }

    rv = bcm_esw_stk_my_modid_get(0, &my_modid);
    if (0 > rv)
    {
        return -1;
    }
    rv = _bcm_esw_stk_modmap_map(0, BCM_STK_MODMAP_GET,
                                 my_modid, pp, &modid_out, &port_out);
    if (0 > rv)
    {
        return -1;
    }
    BCM_GPORT_MODPORT_SET(gport, modid_out, port_out);

    return gport;
}

sai_status_t
_brcm_sai_tunnel_term_add(int type, _brcm_sai_tunnel_info_t *tinfo,
                          bcm_tunnel_terminator_t *tunnel_term)
{
    sai_status_t rv;
    bcm_tunnel_terminator_t bcm_tunnel_term;

    bcm_tunnel_terminator_t_init(&bcm_tunnel_term);
    /* first copy all params */
    sal_memcpy(&bcm_tunnel_term, tunnel_term,
               sizeof(bcm_tunnel_terminator_t));
    /* now change a few based on tinfo flags */
    if (SAI_TUNNEL_TTL_MODE_UNIFORM_MODEL == tinfo->decap_ttl_mode)
    {
        bcm_tunnel_term.flags |= BCM_TUNNEL_TERM_USE_OUTER_TTL;
    }
    rv = bcm_tunnel_terminator_add(0, &bcm_tunnel_term);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "tunnel terminator add", rv);

    tunnel_term->tunnel_id = bcm_tunnel_term.tunnel_id;

    return rv;
}

sai_status_t
_brcm_sai_tunnel_term_get(int unit, bcm_tunnel_terminator_t *tunnel_term)
{
    sai_status_t rv;

    rv = bcm_tunnel_terminator_get(0, tunnel_term);
    BRCM_SAI_API_CHK(SAI_API_NEXT_HOP, "tunnel terminator get", rv);

    return rv;
}

sai_status_t
_brcm_sai_cosq_gport_discard_set(bcm_gport_t gport, int qid, bool ect, int gn,
                                 _brcm_sai_cosq_gport_discard_t *sai_discard)
{
    sai_status_t rv;
    bcm_cosq_gport_discard_t discard;

    bcm_cosq_gport_discard_t_init(&discard);
    discard.flags = sai_discard->discard.flags;
    if (ect)
    {
        discard.flags |= BCM_COSQ_DISCARD_ECT_MARKED;
    }
    discard.min_thresh = sai_discard->discard.min_thresh;
    discard.max_thresh = sai_discard->discard.max_thresh;
    discard.drop_probability = sai_discard->discard.drop_probability;
    discard.gain = gn;
    discard.ecn_thresh = sai_discard->et;
    discard.refresh_time = sai_discard->rt;
    rv = bcm_cosq_gport_discard_set(0, gport, qid, &discard);
    BRCM_SAI_API_CHK(SAI_API_PORT, "cosq gport discard set", rv);

    return rv;
}

/********************************Driver code***********************************/

int
driverPropertyCheck(int prop, int *val)
{
    switch (prop)
    {
        case 0:
            *val = soc_property_get(0, spn_L3_ALPM_ENABLE, 0);
            break;
        case 1:
            *val = soc_property_get(0, spn_IPV6_LPM_128B_ENABLE, 0);
            break;
        case 2:
            *val = soc_property_get(0, spn_SKIP_L2_USER_ENTRY, 0);
            break;
        case 3:
        {
            *val = soc_property_get(0, spn_MMU_PORT_NUM_MC_QUEUE, 0);
            break;
        }
        default:
            return -1;
    }
    return 0;
}


int DRV_MEM_UNIQUE_ACC(int mem, int pipe)
{
    return SOC_MEM_UNIQUE_ACC(0, mem)[pipe];
}

int DRV_TH_MMU_PIPED_MEM_INDEX(int port, int mem, int idx)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    return SOC_TH_MMU_PIPED_MEM_INDEX(0, port, mem, idx);
#else
    return 0;
#endif
}

int
driverMmuInfoGet(int type, uint32_t *val)
{
/* Note: This is chip specific - best to have a SDK/SOC api to retreive this from */
#define TOTAL_TD2_MMU_BUFFER_SIZE (59941*208/1000) /* In KB */
#define TOTAL_TH_MMU_BUFFER_SIZE  (16*1024*1024)   /* In KB */
#define MMU_NUM_BUFF_POOLS        4
#if defined(BCM_TOMAHAWK2_SUPPORT)
#define TOTAL_TH2_MMU_BUFFER_SIZE  (_TH2_MMU_TOTAL_CELLS * 208 / 1000 ) /* In KB */
#else
#define TOTAL_TH2_MMU_BUFFER_SIZE 0
#endif
    switch (type)
    {
        case 0: /* Total MMU buffer size */
          if (DEV_IS_TH2())
          {
            *val = TOTAL_TH2_MMU_BUFFER_SIZE;
          }
          else if (DEV_IS_TH()) 
          {
            *val  = TOTAL_TH_MMU_BUFFER_SIZE;
          }
          else if (DEV_IS_TD2())
          {
            *val  = TOTAL_TD2_MMU_BUFFER_SIZE;
          }
          else
          {
            return -1;
          }
          break;
        case 1: /* Number of ingress buffer pools */
            *val = MMU_NUM_BUFF_POOLS;
            break;
        case 2: /* Number of egress buffer pools */
            *val = MMU_NUM_BUFF_POOLS;
            break;
        default:
            return -1;
    }
    return 0;
}

int driverTcPgMapSet(uint8_t port, uint8_t tc, uint8_t pg)
{
    uint32 rval;
    soc_reg_t reg = INVALIDr;

    static const soc_reg_t prigroup_reg[] = {
        THDI_PORT_PRI_GRP0r, THDI_PORT_PRI_GRP1r
    };
    static const soc_field_t prigroup_field[] = {
        PRI0_GRPf, PRI1_GRPf, PRI2_GRPf, PRI3_GRPf,
        PRI4_GRPf, PRI5_GRPf, PRI6_GRPf, PRI7_GRPf,
        PRI8_GRPf, PRI9_GRPf, PRI10_GRPf, PRI11_GRPf,
        PRI12_GRPf, PRI13_GRPf, PRI14_GRPf, PRI15_GRPf
    };

    if (tc < _TH_MMU_NUM_PG) {
        reg = prigroup_reg[0];
    } else {
        reg = prigroup_reg[1];
    }

    SOC_IF_ERROR_RETURN
        (soc_reg32_get(0, reg, port, 0, &rval));
    soc_reg_field_set(0, reg, &rval, prigroup_field[tc], pg);
    SOC_IF_ERROR_RETURN
        (soc_reg32_set(0, reg, port, 0, rval));

    return BCM_E_NONE;
}

#define BRCM_MMU_B_TO_C(_byte_)  \
    (((_byte_) + BRCM_MMU_B_PER_C - 1) / BRCM_MMU_B_PER_C)
/* Ingress config */
static int
_driverSPLimitSet(int idx, int val)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    uint32_t max_val, rval = 0;

    max_val = (1 << soc_reg_field_length(0, THDI_BUFFER_CELL_LIMIT_SPr, LIMITf)) - 1;
    if (DEV_IS_THX())
    {
        val /= 4;
    }
    val = BRCM_MMU_B_TO_C(val);
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    soc_reg_field_set(0, THDI_BUFFER_CELL_LIMIT_SPr, &rval, LIMITf, val);
    SOC_IF_ERROR_RETURN
        (soc_tomahawk_xpe_reg32_set(0, THDI_BUFFER_CELL_LIMIT_SPr,
                                    -1, -1, idx, rval));
#endif
    return BCM_E_NONE;
}

int
driverSPLimitSet(int idx, int val)
{
    uint32_t max_val, rval = 0;

    if (DEV_IS_THX())
    {
        return _driverSPLimitSet(idx, val);
    }
    max_val = (1 << soc_reg_field_length(0, THDI_BUFFER_CELL_LIMIT_SPr, LIMITf)) - 1;
    val = BRCM_MMU_B_TO_C(val);
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    soc_reg_field_set(0, THDI_BUFFER_CELL_LIMIT_SPr, &rval, LIMITf, val);
    SOC_IF_ERROR_RETURN(WRITE_THDI_BUFFER_CELL_LIMIT_SPr(0, idx, rval));

    return BCM_E_NONE;
}

int
driverSPLimitGet(int idx, int *val)
{
    uint32_t rval = 0;

    SOC_IF_ERROR_RETURN(READ_THDI_BUFFER_CELL_LIMIT_SPr(0, idx, &rval));
    *val = soc_reg_field_get(0, THDI_BUFFER_CELL_LIMIT_SPr, rval, LIMITf) *
        BRCM_MMU_B_PER_C;

    return BCM_E_NONE;
}

int
driverSPHeadroomSet(int idx, int val)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    uint32_t max_val, rval = 0;

    max_val = (1 << soc_reg_field_length(0, THDI_HDRM_BUFFER_CELL_LIMIT_HPr,
                                         LIMITf)) - 1;
    if (-1 == val)
    {
        val = max_val;
    }
    else
    {
        if (0 != val)
        {
            val = BRCM_MMU_B_TO_C(val);
            val = val / 4;
        }
        if ((val < 0) || (val > max_val)) 
        {
            return BCM_E_PARAM;
        }
    }
    soc_reg_field_set(0, THDI_HDRM_BUFFER_CELL_LIMIT_HPr,
                      &rval, LIMITf, val);
    /* Per XPE register */
    SOC_IF_ERROR_RETURN(
        soc_tomahawk_xpe_reg32_set(0, THDI_HDRM_BUFFER_CELL_LIMIT_HPr,
                                   -1, -1, idx, rval));
#endif
    return BCM_E_NONE;
}

int
driverIngressGlobalHdrmSet(int port, int val)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    uint32_t max_val, rval = 0;

    max_val = (1 << soc_reg_field_length(0, THDI_GLOBAL_HDRM_LIMITr,
                                         GLOBAL_HDRM_LIMITf)) - 1;
    val = BRCM_MMU_B_TO_C(val);
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    soc_reg_field_set(0, THDI_GLOBAL_HDRM_LIMITr,
                      &rval, GLOBAL_HDRM_LIMITf, val);
    /* Per XPE register */
    SOC_IF_ERROR_RETURN(
        soc_tomahawk_xpe_reg32_set(0, THDI_GLOBAL_HDRM_LIMITr,
                                   -1, -1, 0, rval));
#endif
    return BCM_E_NONE;
}

int
driverPGGlobalHdrmEnable(int port, int pg, uint8_t enable)
{
    int pipe, midx;
    soc_mem_t mem;
    thdi_port_pg_config_entry_t pg_config_mem;

    if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
    {
        return BCM_E_INTERNAL;
    }
    mem = DRV_MEM_UNIQUE_ACC(THDI_PORT_PG_CONFIGm, pipe);
    if (INVALIDm == mem)
    {
        return BCM_E_PARAM;
    }
    midx = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                      THDI_PORT_PG_CONFIGm, pg);
    if (midx < 0) {
        return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, midx, &pg_config_mem));
    soc_mem_field32_set(0, mem, &pg_config_mem, PG_GBL_HDRM_ENf, enable);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, midx, &pg_config_mem));

    return BCM_E_NONE;
}

/* Egress config */
int
driverDBLimitSet(int idx, int val)
{
    uint32_t max_val, rval = 0;

    max_val = (1 << soc_reg_field_length(0, MMU_THDM_DB_POOL_SHARED_LIMITr, SHARED_LIMITf)) - 1;
    if (DEV_IS_THX())
    {
        val /= 4;
    }
    val = BRCM_MMU_B_TO_C(val);
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    soc_reg_field_set(0, MMU_THDM_DB_POOL_SHARED_LIMITr, &rval, SHARED_LIMITf, val);
    /* Set all - base and colored to the same limits */
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDM_DB_POOL_SHARED_LIMITr(0, idx, rval));
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDM_DB_POOL_YELLOW_SHARED_LIMITr(0, idx, rval));
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDM_DB_POOL_RED_SHARED_LIMITr(0, idx, rval));
    /* NOTE: program resume limit to 75% of shared limit (this is in units of 8 cells) */
    rval = (rval * 75) / 100;
    rval /= 8;
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDM_DB_POOL_RESUME_LIMITr(0, idx, rval));
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDM_DB_POOL_YELLOW_RESUME_LIMITr(0, idx, rval));
    SOC_IF_ERROR_RETURN(WRITE_MMU_THDM_DB_POOL_RED_RESUME_LIMITr(0, idx, rval));

    return BCM_E_NONE;
}

int
driverPGAttributeSet(int port, int pg, int type, int val)
{
    int pipe, midx;
    uint32 max_val;
    thdi_port_pg_config_entry_t pg_config_mem;
    soc_mem_t mem = SOC_TD2_PMEM(0, port, THDI_PORT_PG_CONFIG_Xm,
                                 THDI_PORT_PG_CONFIG_Ym);
    soc_field_t field;

    if (DEV_IS_THX())
    {
        if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
        {
            return BCM_E_INTERNAL;
        }
        mem = DRV_MEM_UNIQUE_ACC(THDI_PORT_PG_CONFIGm, pipe);
        if (INVALIDm == mem)
        {
            return BCM_E_PARAM;
        }
        midx = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                          THDI_PORT_PG_CONFIGm, pg);
    }
    else
    {
        mem = SOC_TD2_PMEM(0, port, THDI_PORT_PG_CONFIG_Xm,
                           THDI_PORT_PG_CONFIG_Ym);
        midx = SOC_TD2_MMU_PIPED_MEM_INDEX(0, port, mem, pg);
    }
    if (midx < 0) {
        return BCM_E_PARAM;
    }
    /* type == 1 is PG_SHARED_DYNAMIC
     * If PG_SHARED_DYNAMIC == 1, PG_SHARED_LIMIT contains an
     * alpha value.
     * If PG_SHARED_DYNAMIC == 0, PG_SHARED_LIMIT contains a
     * byte value.
     */
    switch (type) {
        case 1:
            field = PG_SHARED_DYNAMICf;
            break;
        case 2:
        case 3:
            field = PG_SHARED_LIMITf;
            break;
        case 4:
            field = PG_MIN_LIMITf;
            break;
        case 5:
            field = PG_HDRM_LIMITf;
            break;
        case 6:
            field = PG_RESET_FLOORf;
            break;
        case 7:
            field = PG_RESET_OFFSETf;
            break;
        default:
            return BCM_E_PARAM;
    }
    /* type == 2 means PG_SHARED_LIMIT is an alpha value.
     * type == 3 means PG_SHARED_LIMIT is a byte value.
     */
    if (2 == type)
    {
        switch (val)
        {
            case -7:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_128;
                break;
            case -6:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_64;
                break;
            case -5:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_32;
                break;
            case -4:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_16;
                break;
            case -3:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_8;
                break;
            case -2:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_4;
                break;
            case -1:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_2;
                break;
            case 0:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1;
                break;
            case 1:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_2;
                break;
            case 2:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_4;
                break;
            case 3:
                val = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_8;
                break;
            default:
                return BCM_E_PARAM;
        }
    }
    max_val = (1 << soc_mem_field_length(0, mem, field)) - 1;
    if ((3 == type) || (4 == type))
    {
        val /=  BRCM_MMU_B_PER_C;
    }
    if (4 < type)
    {
        val = BRCM_MMU_B_TO_C(val);
    }
    if ((PG_RESET_OFFSETf == field) && (0 == val))
    {
        val = max_val;
    }
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, midx, &pg_config_mem));
    soc_mem_field32_set(0, mem, &pg_config_mem, field, val);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, midx, &pg_config_mem));

    return BCM_E_NONE;
}

uint32_t driverPortPoolMaxLimitBytesMaxGet()
{
  if (DEV_IS_THX())
  {
    /* size of PORT_SP_MAX_LIMIT - 1  */
    return 0x7FFF * BRCM_MMU_B_PER_C;
  }
  else if (DEV_IS_TD2())
  {
    return 0x1FFFF * BRCM_MMU_B_PER_C;
  }
  else
  {
    /* unsupported */
    return BCM_E_UNAVAIL;
  }  
}

int
driverPortSPSet(int port, int pool, int type, int val)
{
    int pipe, midx;
    uint32 max_val;
    thdi_port_sp_config_entry_t sp_config_mem;
    soc_mem_t mem;
    soc_field_t field;

    if (DEV_IS_THX())
    {
        if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
        {
            return BCM_E_INTERNAL;
        }
        mem = DRV_MEM_UNIQUE_ACC(THDI_PORT_SP_CONFIGm, pipe);
        if (INVALIDm == mem)
        {
            return BCM_E_INTERNAL;
        }
        midx = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                          THDI_PORT_SP_CONFIGm, pool);
    }
    else
    {
        mem = SOC_TD2_PMEM(0, port, THDI_PORT_SP_CONFIG_Xm,
                           THDI_PORT_SP_CONFIG_Ym);
        midx = SOC_TD2_MMU_PIPED_MEM_INDEX(0, port, mem, pool);
    }

    switch (type) {
        case 1: 
            field = PORT_SP_RESUME_LIMITf;
            break;
        case 2: 
            field = PORT_SP_MIN_LIMITf;
            break;
        case 3: 
            field = PORT_SP_MAX_LIMITf;
            break;
        default:
            return BCM_E_PARAM;
    }

    max_val = (1 << soc_mem_field_length(0, mem, field)) - 1;
    if (-1 == val)
    {
        val = max_val;
    }
    else
    {
        if (1 == type)
        {
            val = BRCM_MMU_B_TO_C(val);
        }
        else
        {
            val /=  BRCM_MMU_B_PER_C;
        }
        if ((val < 0) || (val > max_val)) {
            return BCM_E_PARAM;
        }
    }
    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, midx, &sp_config_mem));
    soc_mem_field32_set(0, mem, &sp_config_mem, field, val);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, midx, &sp_config_mem));

    return BCM_E_NONE;
}

int
driverEgressPortPoolLimitSet(int port, int pool, int val)
{
    int pipe, midx;
    uint32 max_val;
    mmu_thdu_config_port_entry_t config_entry;
    soc_mem_t config_mem;

    if (DEV_IS_THX())
    {
        if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
        {
            return BCM_E_INTERNAL;
        }
        config_mem = DRV_MEM_UNIQUE_ACC(MMU_THDU_CONFIG_PORTm, pipe);
        if (INVALIDm == config_mem)
        {
            return BCM_E_INTERNAL;
        }
        midx = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                          MMU_THDU_CONFIG_PORTm, pool);
    }
    else
    {
        config_mem = SOC_TD2_PMEM(0, port, MMU_THDU_XPIPE_CONFIG_PORTm,
                                  MMU_THDU_YPIPE_CONFIG_PORTm);
        midx = SOC_TD2_MMU_PIPED_MEM_INDEX(0, port, config_mem, pool);
    }
    if (midx < 0) {
        return BCM_E_PARAM;
    }
    max_val = (1 << soc_mem_field_length(0, config_mem, SHARED_LIMITf)) - 1;
    if (-1 == val)
    {
        val = max_val;
    }
    else
    {
        val = BRCM_MMU_B_TO_C(val);
        if (val > max_val) {
            return BCM_E_PARAM;
        }
    }
    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, config_mem, MEM_BLOCK_ALL, midx, &config_entry));
    soc_mem_field32_set(0, config_mem, &config_entry, SHARED_LIMITf, val);
    soc_mem_field32_set(0, config_mem, &config_entry, YELLOW_LIMITf, val/8);
    soc_mem_field32_set(0, config_mem, &config_entry, RED_LIMITf, val/8);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, config_mem, MEM_BLOCK_ALL, midx, &config_entry));

    return BCM_E_NONE;
}

int
driverEgressPortPoolResumeSet(int port, int pool, int val)
{
    int pipe, midx;
    uint32 max_val;
    mmu_thdu_resume_port_entry_t resume_entry;
    soc_mem_t resume_mem;

    if (DEV_IS_THX())
    {
        if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
        {
            return BCM_E_INTERNAL;
        }
        resume_mem = DRV_MEM_UNIQUE_ACC(MMU_THDU_RESUME_PORTm, pipe);
        if (INVALIDm == resume_mem)
        {
            return BCM_E_INTERNAL;
        }
        midx = DRV_TH_MMU_PIPED_MEM_INDEX(port,
                                          MMU_THDU_RESUME_PORTm, pool);
    }
    else
    {
        resume_mem = SOC_TD2_PMEM(0, port, MMU_THDU_XPIPE_RESUME_PORTm,
                                  MMU_THDU_YPIPE_RESUME_PORTm);
        midx = SOC_TD2_MMU_PIPED_MEM_INDEX(0, port, resume_mem, pool);
    }
    if (midx < 0) {
        return BCM_E_PARAM;
    }
    max_val = (1 << soc_mem_field_length(0, resume_mem, SHARED_RESUMEf)) - 1;
    if (-1 == val)
    {
        val = max_val;
    }
    else
    {
        val = BRCM_MMU_B_TO_C(val);
        if (val > max_val) {
            return BCM_E_PARAM;
        }
    }
    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, resume_mem, MEM_BLOCK_ALL, midx, &resume_entry));
    soc_mem_field32_set(0, resume_mem, &resume_entry, SHARED_RESUMEf, val);
    soc_mem_field32_set(0, resume_mem, &resume_entry, YELLOW_RESUMEf, val);
    soc_mem_field32_set(0, resume_mem, &resume_entry, RED_RESUMEf, val);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, resume_mem, MEM_BLOCK_ALL, midx, &resume_entry));

    return BCM_E_NONE;
}

static int
_driverEgressQueueSharedAlphaSet(int port, int qid, int val)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    int pipe, startq;
    uint32 alpha;
    mmu_thdu_config_queue_entry_t entry;
    soc_mem_t mem;

    if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
    {
        return BCM_E_INTERNAL;
    }
    mem = DRV_MEM_UNIQUE_ACC(MMU_THDU_CONFIG_QUEUEm, pipe);
    if (INVALIDm == mem)
    {
        return BCM_E_INTERNAL;
    }
    switch (val) {
        case -7:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1_128;
            break;
        case -6:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1_64;
            break;
        case -5:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1_32;
            break;
        case -4:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1_16;
            break;
        case -3:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1_8;
            break;
        case -2:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1_4;
            break;
        case -1:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1_2;
            break;
        case 0:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_1;
            break;
        case 1:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_2;
            break;
        case 2:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_4;
            break;
        case 3:
            alpha = SOC_TH_COSQ_DROP_LIMIT_ALPHA_8;
            break;
        default:
            return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (_bcm_th_cosq_index_resolve
            (0, port, qid, _BCM_TH_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));
    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    soc_mem_field32_set(0, mem, &entry, Q_SHARED_ALPHA_CELLf, alpha);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, startq, &entry));
#endif
    return BCM_E_NONE;
}

int
driverEgressQueueSharedAlphaSet(int port, int qid, int val)
{
    int startq;
    uint32 alpha;
    mmu_thdu_config_queue_entry_t entry;
    soc_mem_t mem = SOC_TD2_PMEM(0, port, MMU_THDU_XPIPE_CONFIG_QUEUEm,
                                 MMU_THDU_YPIPE_CONFIG_QUEUEm);

    if (DEV_IS_THX())
    {
        return _driverEgressQueueSharedAlphaSet(port, qid, val);
    }
    switch (val) {
        case -7:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_128;
            break;
        case -6:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_64;
            break;
        case -5:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_32;
            break;
        case -4:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_16;
            break;
        case -3:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_8;
            break;
        case -2:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_4;
            break;
        case -1:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1_2;
            break;
        case 0:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_1;
            break;
        case 1:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_2;
            break;
        case 2:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_4;
            break;
        case 3:
            alpha = SOC_TD2_COSQ_DROP_LIMIT_ALPHA_8;
            break;
        default:
            return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (_bcm_td2_cosq_index_resolve
            (0, port, qid, _BCM_TD2_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));
    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    soc_mem_field32_set(0, mem, &entry, Q_SHARED_ALPHA_CELLf, alpha);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, startq, &entry));

    return BCM_E_NONE;
}

static int
_driverEgressQueueMinLimitSet(int port, int qid, int val)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    int pipe, startq;
    uint32 max_val;
    mmu_thdu_config_queue_entry_t entry;
    soc_mem_t mem;

    if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
    {
        return BCM_E_INTERNAL;
    }
    mem = DRV_MEM_UNIQUE_ACC(MMU_THDU_CONFIG_QUEUEm, pipe);
    if (INVALIDm == mem)
    {
        return BCM_E_INTERNAL;
    }
    max_val = (1 << soc_mem_field_length(0, mem, Q_MIN_LIMIT_CELLf)) - 1;
    val = BRCM_MMU_B_TO_C(val);
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (_bcm_th_cosq_index_resolve
            (0, port, qid, _BCM_TH_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));

    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    soc_mem_field32_set(0, mem, &entry, Q_MIN_LIMIT_CELLf, val);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, startq, &entry));
#endif
    return BCM_E_NONE;
}

int
driverEgressQueueMinLimitSet(int port, int qid, int val)
{
    int startq;
    uint32 max_val;
    mmu_thdu_config_queue_entry_t entry;
    soc_mem_t mem = SOC_TD2_PMEM(0, port, MMU_THDU_XPIPE_CONFIG_QUEUEm,
                                 MMU_THDU_YPIPE_CONFIG_QUEUEm);

    if (DEV_IS_THX())
    {
        return _driverEgressQueueMinLimitSet(port, qid, val);
    }
    max_val = (1 << soc_mem_field_length(0, mem, Q_MIN_LIMIT_CELLf)) - 1;
    val = BRCM_MMU_B_TO_C(val);
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (_bcm_td2_cosq_index_resolve
            (0, port, qid, _BCM_TD2_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));

    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    soc_mem_field32_set(0, mem, &entry, Q_MIN_LIMIT_CELLf, val);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, startq, &entry));

    return BCM_E_NONE;
}

static sai_status_t
_driverEgressQueueMinLimitGet(int unit, int port, int qid, int *val)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    int pipe, startq;
    mmu_thdu_config_queue_entry_t entry;
    soc_mem_t mem;

    if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
    {
        return BCM_E_INTERNAL;
    }
    mem = DRV_MEM_UNIQUE_ACC(MMU_THDU_CONFIG_QUEUEm, pipe);
    if (INVALIDm == mem)
    {
        return BCM_E_INTERNAL;
    }
    SOC_IF_ERROR_RETURN
        (_bcm_th_cosq_index_resolve
            (0, port, qid, _BCM_TH_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));

    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    *val = soc_mem_field32_get(0, mem, &entry, Q_MIN_LIMIT_CELLf);
    *val *= BRCM_MMU_B_PER_C;
#endif
    return BCM_E_NONE;
}

sai_status_t
driverEgressQueueMinLimitGet(int unit, int port, int qid, int *val)
{
    int startq;
    mmu_thdu_config_queue_entry_t entry;
    soc_mem_t mem = SOC_TD2_PMEM(0, port, MMU_THDU_XPIPE_CONFIG_QUEUEm,
                                 MMU_THDU_YPIPE_CONFIG_QUEUEm);

    if (DEV_IS_THX())
    {
        return _driverEgressQueueMinLimitGet(unit, port, qid, val);
    }
    SOC_IF_ERROR_RETURN
        (_bcm_td2_cosq_index_resolve
            (0, port, qid, _BCM_TD2_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));

    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    *val = soc_mem_field32_get(0, mem, &entry, Q_MIN_LIMIT_CELLf);
    *val *= BRCM_MMU_B_PER_C;

    return BCM_E_NONE;
}

static int
_driverEgressQueueResetOffsetSet(int port, int qid, int val)
{
#if defined(BCM_TOMAHAWK_SUPPORT)
    int pipe, startq;
    uint32 max_val;
    mmu_thdu_offset_queue_entry_t entry;
    soc_mem_t mem;

    if (soc_port_pipe_get(0, port, &pipe) != BCM_E_NONE)
    {
        return BCM_E_INTERNAL;
    }
    mem = DRV_MEM_UNIQUE_ACC(MMU_THDU_OFFSET_QUEUEm, pipe);
    if (INVALIDm == mem)
    {
        return BCM_E_INTERNAL;
    }
    max_val = (1 << soc_mem_field_length(0, mem, RESET_OFFSET_CELLf)) - 1;
    val = BRCM_MMU_B_TO_C(val) / 8;
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (_bcm_th_cosq_index_resolve
            (0, port, qid, _BCM_TH_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));

    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    soc_mem_field32_set(0, mem, &entry, RESET_OFFSET_CELLf, val);
    soc_mem_field32_set(0, mem, &entry, RESET_OFFSET_YELLOW_CELLf, val);
    soc_mem_field32_set(0, mem, &entry, RESET_OFFSET_RED_CELLf, val);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, startq, &entry));
#endif
    return BCM_E_NONE;
}

int
driverEgressQueueResetOffsetSet(int port, int qid, int val)
{
    int startq;
    uint32 max_val;
    mmu_thdu_offset_queue_entry_t entry;
    soc_mem_t mem = SOC_TD2_PMEM(0, port, MMU_THDU_XPIPE_OFFSET_QUEUEm,
                                 MMU_THDU_YPIPE_OFFSET_QUEUEm);

    if (DEV_IS_THX())
    {
        return _driverEgressQueueResetOffsetSet(port, qid, val);
    }
    max_val = (1 << soc_mem_field_length(0, mem, RESET_OFFSET_CELLf)) - 1;
    val = BRCM_MMU_B_TO_C(val) / 8;
    if ((val < 0) || (val > max_val)) {
        return BCM_E_PARAM;
    }
    SOC_IF_ERROR_RETURN
        (_bcm_td2_cosq_index_resolve
            (0, port, qid, _BCM_TD2_COSQ_INDEX_STYLE_UCAST_QUEUE,
             NULL, &startq, NULL));

    SOC_IF_ERROR_RETURN
        (soc_mem_read(0, mem, MEM_BLOCK_ALL, startq, &entry));
    soc_mem_field32_set(0, mem, &entry, RESET_OFFSET_CELLf, val);
    soc_mem_field32_set(0, mem, &entry, RESET_OFFSET_YELLOW_CELLf, val);
    soc_mem_field32_set(0, mem, &entry, RESET_OFFSET_RED_CELLf, val);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(0, mem, MEM_BLOCK_ALL, startq, &entry));

    return BCM_E_NONE;
}

int
driverDisableStormControl(int port)
{
    uint32 ctrlMeter;

    SOC_IF_ERROR_RETURN
        (READ_STORM_CONTROL_METER_CONFIGr(0, port, &ctrlMeter));

    soc_reg_field_set(0, STORM_CONTROL_METER_CONFIGr, &ctrlMeter,
                      BCAST_ENABLEf, 0);
    soc_reg_field_set(0, STORM_CONTROL_METER_CONFIGr, &ctrlMeter,
                      DLFBC_ENABLEf, 0);
    soc_reg_field_set(0, STORM_CONTROL_METER_CONFIGr, &ctrlMeter,
                      KNOWN_L2MC_ENABLEf, 0);
    soc_reg_field_set(0, STORM_CONTROL_METER_CONFIGr, &ctrlMeter,
                      UNKNOWN_L2MC_ENABLEf, 0);
    soc_reg_field_set(0, STORM_CONTROL_METER_CONFIGr, &ctrlMeter,
                      KNOWN_IPMC_ENABLEf, 0);
    soc_reg_field_set(0, STORM_CONTROL_METER_CONFIGr, &ctrlMeter,
                      UNKNOWN_IPMC_ENABLEf, 0);

    SOC_IF_ERROR_RETURN
        (WRITE_STORM_CONTROL_METER_CONFIGr(0, port, ctrlMeter));

    return BCM_E_NONE;
}

/*
 * Function:
 *      driverPortPktTxEnableSet
 * Purpose:
 *      Enable/disable packet transmission on a port.
 * Parameters:
 *      unit - StrataSwitch unit #.
 *      port - StrataSwitch port #.
 *      enable - TRUE, enable port packet transmission, FALSE, disable port packet transmission.
 * Returns:
 *      BCM_E_XXX
 */
int
driverPortPktTxEnableSet(int unit, bcm_port_t port, int enable)
{
    egr_enable_entry_t egr_en;
    soc_info_t *si = &SOC_INFO(unit);
    int epport = port;

    BRCM_SAI_LOG_PORT(SAI_LOG_LEVEL_DEBUG,
                      "Egress pkt tx enable: logical port: %d, physical port: %d, enable flag: %d\n",
                      port, si->port_l2p_mapping[port], enable);

    // TH uses logical port index to disable packet TX,
    // while td2 and th2 use physical port index
    if (!DEV_IS_TH())
    {
        epport = si->port_l2p_mapping[port];
    }
    SOC_IF_ERROR_RETURN
        (soc_mem_read(unit, EGR_ENABLEm, SOC_BLOCK_ANY, epport, &egr_en));
    soc_mem_field32_set(unit, EGR_ENABLEm, (uint32 *)&egr_en, PRT_ENABLEf, !!enable);
    SOC_IF_ERROR_RETURN
        (soc_mem_write(unit, EGR_ENABLEm, SOC_BLOCK_ANY, epport, &egr_en));

    return BCM_E_NONE;
}

/*
 * Function:
 *      driverMMUInstMapIndexGet 
 * Purpose:
 *      Return mmu instance map index for a given port
 * Parameters:
 *      unit - StrataSwitch unit #.
 *      port - StrataSwitch port #.
 *      bid  - stat id
 * Returns:
 *      BCM_E_XXX
 */
int driverMMUInstMapIndexGet(int unit, bcm_gport_t gport, bcm_bst_stat_id_t bid, int* index)
{
    _bcm_bst_cmn_unit_info_t *bst_info;

    uint32 mmu_inst_map = 0;
    if ((bst_info = _BCM_UNIT_BST_INFO(unit)) == NULL) {
        return BCM_E_INIT;
    }

    if (bst_info->port_to_mmu_inst_map) 
    {
        (void)bst_info->port_to_mmu_inst_map(unit, bid, gport,
                                             &mmu_inst_map);
    }
    if (bcmBstStatIdIngPool == bid)
    {
        *index = (0xC == mmu_inst_map)? 1:0;
    }
    else if (bcmBstStatIdEgrPool == bid)
    {
        *index = (0xA == mmu_inst_map)? 1:0;
    }
    else
    {
        *index = 0;
    }
    return BCM_E_NONE;
}


