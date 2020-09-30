/*********************************************************************
 *
 * Copyright: (c) 2018 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#ifndef PLATFORM_CONFIG__H
#define PLATFORM_CONFIG__H

#include <stdint.h>
#include <soc/property.h>
#include "driver_util.h"

typedef enum
{
  PLATFORM_CONFIG_ENV         = 0x01,  /* Config file from environment
                                        * variable */
  PLATFORM_CONFIG_INIT_PARAM  = 0x02,  /* Config file passed as param
                                        * to driver init */
  PLATFORM_CONFIG_LINUX       = 0x03,  /* Config file from standard
                                        * Linux location */
  PLATFORM_CONFIG_DEFAULT     = 0x04,  /* System default config */
  PLATFORM_CONFIG_LAST        = 0x05,  /* Invalid */
} platform_config_methods_t;

typedef struct pc_s {
  char    *pc_name;
  char    *pc_value;
} pc_t;

#define FREE_PC(_p) if (_p) {                   \
  if ((_p)->pc_name) sal_free((_p)->pc_name);   \
  if ((_p)->pc_value) sal_free((_p)->pc_value); \
  sal_free(_p);               \
}

#define PLATFORM_CONFIG_STR_MAX 256

#define PLATFORM_SOC_PROP_NAMES_INITIALIZER \
{ \
  spn_PORTMAP, \
  spn_PHY_XAUI_RX_POLARITY_FLIP, \
  spn_PHY_XAUI_TX_POLARITY_FLIP, \
  spn_L2_MEM_ENTRIES, \
  spn_L3_MEM_ENTRIES, \
  spn_SERDES_DRIVER_CURRENT, \
  spn_SERDES_PREEMPHASIS, \
  spn_SERDES_PRE_DRIVER_CURRENT, \
  spn_STABLE_FILENAME, \
  spn_STABLE_LOCATION, \
  spn_STABLE_SIZE, \
  "scache_filename", \
  spn_XGXS_RX_LANE_MAP, \
  spn_XGXS_TX_LANE_MAP, \
  spn_PBMP_XPORT_XE, \
  spn_PBMP_OVERSUBSCRIBE, \
  spn_SERDES_RX_LOS, \
  spn_RATE_EXT_MDIO_DIVISOR, \
  spn_BCM_NUM_COS, \
  spn_BCM_STAT_INTERVAL, \
  spn_CDMA_TIMEOUT_USEC, \
  spn_TDMA_TIMEOUT_USEC, \
  spn_PHY_AN_C37, \
  spn_PHY_AN_C73, \
  spn_MIIM_INTR_ENABLE, \
  spn_TDMA_INTR_ENABLE, \
  spn_SCHAN_INTR_ENABLE, \
  spn_TRUNK_EXTEND, \
  spn_XGXS_LCPLL_XTAL_REFCLK, \
  spn_MODULE_64PORTS, \
  spn_LPM_SCALING_ENABLE, \
  spn_LPM_IPV6_128B_RESERVED, \
  spn_NUM_IPV6_LPM_128B_ENTRIES, \
  spn_IPV6_LPM_128B_ENABLE, \
  spn_L3_ALPM_ENABLE, \
  spn_MAX_VP_LAGS, \
  spn_SERDES_FIRMWARE_MODE, \
  spn_SERDES_IF_TYPE, \
  spn_SKIP_L2_USER_ENTRY, \
  spn_PORT_INIT_CL72, \
  spn_MMU_LOSSLESS, \
  spn_L2XMSG_HOSTBUF_SIZE, \
  spn_L2XMSG_MODE, \
  spn_LLS_NUM_L2UC, \
  spn_OVERSUBSCRIBE_MODE, \
  spn_DPORT_MAP_PORT, \
  spn_DPORT_MAP_DIRECT, \
  spn_DPORT_MAP_INDEXED, \
  spn_DPORT_MAP_ENABLE, \
  spn_FPEM_MEM_ENTRIES, \
  spn_MEM_CACHE_ENABLE, \
  spn_PHY_84752, \
  spn_PHY_EXT_ROM_BOOT, \
  spn_PHY_GEARBOX_ENABLE, \
  spn_PORT_PHY_ADDR, \
  spn_SERDES_FIBER_PREF, \
  spn_SWITCH_BYPASS_MODE, \
  spn_BCM_TUNNEL_TERM_COMPATIBLE_MODE, \
  spn_TSLAM_INTR_ENABLE, \
  spn_TSLAM_TIMEOUT_USEC, \
  spn_MDIO_OUTPUT_DELAY, \
  spn_SERDES_AUTOMEDIUM, \
  spn_L3_INTF_VLAN_SPLIT_EGRESS, \
  spn_ARL_CLEAN_TIMEOUT_USEC, \
  spn_ASF_MEM_PROFILE, \
  spn_BCM_LINKSCAN_INTERVAL, \
  spn_BCM_STAT_FLAGS, \
  spn_BCM_STAT_JUMBO, \
  spn_BCM_STAT_PBMP, \
  spn_BCM_STAT_SYNC_TIMEOUT, \
  spn_BCM_XLATE_PORT_ENABLE, \
  spn_DMA_DESC_TIMEOUT_USEC, \
  spn_HIGIG2_HDR_MODE, \
  spn_IPMC_DO_VLAN, \
  spn_KNET_FILTER_PERSIST, \
  spn_L2DELETE_CHUNKS, \
  spn_L2MOD_DMA_INTR_ENABLE, \
  spn_L3_INTF_VLAN_SPLIT_EGRESS, \
  spn_L3_MAX_ECMP_MODE, \
  spn_LOAD_FIRMWARE, \
  spn_MEMCMD_INTR_ENABLE, \
  spn_MEM_CHECK_MAX_OVERRIDE, \
  spn_MEM_CHECK_NOCACHE_OVERRIDE, \
  spn_MEM_CLEAR_CHUNK_SIZE, \
  spn_MEM_CLEAR_HW_ACCELERATION, \
  spn_MEM_NOCACHE, \
  spn_MIIM_TIMEOUT_USEC, \
  spn_MULTICAST_L2_RANGE, \
  spn_MULTICAST_L3_RANGE, \
  spn_PARITY_CORRECTION, \
  spn_PARITY_ENABLE, \
  spn_PCI2EB_OVERRIDE, \
  spn_PHY_AN_ALLOW_PLL_CHANGE, \
  spn_PHY_AN_FEC, \
  spn_PHY_AUX_VOLTAGE_ENABLE, \
  spn_PHY_EXT_AN_FEC, \
  spn_PHY_PCS_RX_POLARITY_FLIP, \
  spn_PHY_PCS_TX_POLARITY_FLIP, \
  spn_PHY_PORT_PRIMARY_AND_OFFSET, \
  spn_PHY_RX_POLARITY_FLIP, \
  spn_PHY_TX_POLARITY_FLIP, \
  spn_PORT_INIT_ADV, \
  spn_PORT_INIT_AUTONEG, \
  spn_PORT_INIT_DUPLEX, \
  spn_PORT_INIT_SPEED, \
  spn_PORT_MAX_SPEED, \
  spn_PORT_PHY_CLAUSE, \
  spn_PORT_PHY_ID0, \
  spn_PORT_PHY_ID1, \
  spn_PORT_PHY_PRECONDITION_BEFORE_PROBE, \
  spn_PTP_BS_FREF, \
  spn_PTP_TS_PLL_FREF, \
  spn_ROBUST_HASH_SEED_EGRESS_VLAN, \
  spn_ROBUST_HASH_SEED_MPLS, \
  spn_ROBUST_HASH_SEED_VLAN, \
  spn_SCHAN_TIMEOUT_USEC, \
  spn_SERDES_SGMII_MASTER, \
  spn_STAT_IF_PARITY_ENABLE, \
  spn_TABLE_DMA_ENABLE, \
  spn_TSLAM_DMA_ENABLE, \
  spn_XGXS_PDETECT_10G, \
  "ctr_evict_enable", \
  "phy_system_tx_mode", \
  "phy_line_tx_mode", \
  "phy_84328", \
  "os", \
  spn_BCM886XX_IP4_TUNNEL_TERMINATION_MODE, \
  spn_BCM886XX_L2GRE_ENABLE, \
  spn_BCM886XX_VXLAN_ENABLE, \
  spn_BCM886XX_ETHER_IP_ENABLE, \
  spn_BCM886XX_VXLAN_TUNNEL_LOOKUP_MODE, \
  spn_SPLIT_HORIZON_FORWARDING_GROUPS_MODE, \
  spn_PHY_LED1_MODE, \
  spn_PHY_LED2_MODE, \
  spn_PHY_LED3_MODE, \
  spn_PHY_LED4_MODE, \
  spn_PHY_LED_CTRL, \
  spn_PHY_LED_SELECT, \
  spn_PHY_LED_LINK_SPEED_MODE, \
  spn_PHY_LED3_OUTPUT_DISABLE, \
  spn_PHY_FIBER_PREF, \
  spn_PHY_AUTOMEDIUM, \
  spn_PHY_SGMII_AUTONEG, \
  spn_PHY_5464S, \
  spn_PORTGROUP, \
  spn_MEMLIST_ENABLE, \
  spn_REGLIST_ENABLE, \
  spn_HELP_CLI_ENABLE, \
  spn_BCM56340_4X10, \
  spn_BCM56340_2X10, \
  spn_PBMP_GPORT_STACK, \
  spn_CORE_CLOCK_FREQUENCY, \
  spn_DPP_CLOCK_RATIO, \
  spn_PHY_CHAIN_RX_LANE_MAP_PHYSICAL, \
  spn_PHY_CHAIN_TX_LANE_MAP_PHYSICAL, \
  spn_PHY_CHAIN_RX_POLARITY_FLIP_PHYSICAL, \
  spn_PHY_CHAIN_TX_POLARITY_FLIP_PHYSICAL, \
  spn_PORT_FLEX_ENABLE, \
  spn_PORT_UC_MC_ACCOUNTING_COMBINE, \
  spn_MEM_SCAN_ENABLE, \
  spn_SRAM_SCAN_ENABLE, \
  "robust_hash_disable_egress_vlan", \
  "robust_hash_disable_mpls", \
  "robust_hash_disable_vlan", \
  "mmu_init_config",        \
  spn_HOST_AS_ROUTE_DISABLE, \
  "", \
}

#define SAI_BOOT_F_DEFAULT 0x000000

/*****************************************************************//**
 * \brief Identify the method used for platform customization
 *
 * \param init      [IN]    SAI driver initialization structure containing
 *                          config file path, boot flags, etc.
 *
 * \return -1               Illegal parameter
 * \return  0               Success
 *
 * \notes   Makes a copy of the input string.
 ********************************************************************/
int platform_config_init(sai_init_t *init);

/*****************************************************************//**
 * \brief To customize the platform using post init configuration file.
 *        The diag commands listed in this file are applied after 
 *        the system is initialized.
 *
 * \param init      [IN]    SAI driver initialization structure containing
 *                          config file path, boot flags, etc.
 *
 * \return -1               Failure
 * \return  0               Success
 *
 * \notes   None
 ********************************************************************/
int platform_config_post_init(sai_init_t *init);

/*****************************************************************//**
 * \brief Apply platform customization properties provided through config file
 *
 * \param init      [IN]    SAI driver initialization structure containing
 *                          config file path, boot flags, etc.
 *
 * \return  0               configuration applied successfully
 * \return -1               Illegal parameter
 *
 ********************************************************************/
int platform_config_apply(sai_init_t *init);

/*****************************************************************//**
 * \brief Return the method used to take platform configuration
 *
 * \param mode        [IN/OUT]  To store platform config method
 *
 * \return  0         Success.
 * \return -1         Otherwise.
 *
 ********************************************************************/
int platform_config_method_get(int *mode);

#endif /* PLATFORM_CONFIG_H */
