/*********************************************************************
 *
 * Copyright: (c) 2018 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "soc/devids.h"
#include "soc/property.h"
#ifdef BCM_WARM_BOOT_SUPPORT
#include "soc/scache.h"
#include "soc/error.h"
#include "appl/diag/warmboot.h"
#endif
#include "sal/appl/config.h"
#include "sal/types.h"
#include "bcm/error.h"

#include "bcm/types.h"
#include "bcm/port.h"
#include "bcm/error.h"
#include "bcm/vlan.h"
#include "bcm/ipmc.h"
#include "bcm/switch.h"
#include "bcm/cosq.h"
#include "soc/drv.h"
#include "sal/appl/sal.h"
#include "sal/appl/config.h"
#include "appl/diag/shell.h"
#include "appl/diag/system.h"
#include "appl/diag/sysconf.h"
#include "appl/diag/diag.h"
#include "shared/bsl.h"
#include "soc/cm.h"
#include "sal/appl/pci.h"
#include "sal/appl/editline/editline.h"
#include "linux-bde.h"
#ifdef BCM_TRIDENT3_SUPPORT
#include "soc/esw/cancun.h"
#endif

#include "config.h"
#include "file.h"
#include "platform_config_api.h"
#include "driver_util.h"

#include "platform_init.h"
#include "dell_s6100.h"

extern unsigned int platform_string_find(char *file, char *id);
char *diag_user_var_get(char *varname);
int bslmgmt_init(void);
ibde_t *bde;
struct platform_data_s;

typedef int (*board_id_t)(struct platform_data_s *board);

typedef struct platform_data_s
{
  platform_brd_id_t board_id;	
  uint32_t  dev_id;
  char *onie_platform;
  board_id_t board_id_f;
  char *description;
  char **platform_script;
} platform_data_t;

static switch_config_variables_t common_variables[] =
{
  { spn_BCM_STAT_INTERVAL, "2000000" },
  { spn_SCHAN_INTR_ENABLE, "0" },
  { spn_MIIM_INTR_ENABLE, "0" },
  { spn_L2_MEM_ENTRIES, "32768" },
  { spn_L3_MEM_ENTRIES, "16384" },
  { spn_BCM_NUM_COS, "8" },
  { spn_MODULE_64PORTS, "1" },
#ifdef BCM_WARM_BOOT_SUPPORT
  { "scache_filename", PLAT_SCACHE_FILENAME},
  { spn_STABLE_SIZE,   PLAT_STABLE_SIZE},
#endif
};

#ifdef BCM_TRIDENT3_SUPPORT
typedef enum cancun_load_mode_
{
  CANCUN_PRE_MISC_INIT,
  CANCUN_POST_MISC_INIT,
  NUM_CANCUN_LOAD_MODES
} cancun_load_mode_t;

#define CANCUN_LOAD_COMMAND_LEN (64 + CANCUN_FILENAME_SIZE)

#define CANCUN_PRE_MISC_INIT_STR "cancun pre-misc-init"
#define CANCUN_POST_MISC_INIT_STR "cancun post-misc-init"
#endif /* BCM_TRIDENT3_SUPPORT */


static char *warmboot_init_commands[] =
{
  "echo \"rc: unit $unit device $devname\"",
  "warmboot on",
  "init noreset",
#ifdef BCM_TRIDENT3_SUPPORT
  CANCUN_PRE_MISC_INIT_STR,
#endif
  "init misc",
#ifdef BCM_TRIDENT3_SUPPORT
  CANCUN_POST_MISC_INIT_STR,
#endif
  "init mmu",
  "echo rc: MMU initialized",
  "set sesto slice",
  "init bcm",
  "l2mode interval=3000000",
  "echo rc: L2 Table shadowing enabled",
  "linkscan spbm=ge,xe,ce interval=250000",
  "echo rc: Port modes initialized",
};

static char *common_init_commands[] =
{
  "echo \"rc: unit $unit device $devname\"",
  "local strata 1",
  "local PBMP_ALL 0x0bffffff",
  "counter off",
  "linkscan off",
  "l2mode off",
  "init soc",
#ifdef BCM_TRIDENT3_SUPPORT
  CANCUN_PRE_MISC_INIT_STR,
#endif
  "init misc",
#ifdef BCM_TRIDENT3_SUPPORT
  CANCUN_POST_MISC_INIT_STR,
#endif
  "init mmu",
  "echo rc: MMU initialized",
  "set sesto slice",
  "init bcm",
  "l2mode interval=3000000",
  "echo rc: L2 Table shadowing enabled",
  "linkscan spbm=ge,xe,ce interval=250000",
  "echo rc: Port modes initialized",
  "local dma true",
  "counter sync",
  //  "*:led stop; *:led start",
};

/* This is a work around to improve the boot up time on fastboot by setting *
 * the Slice Register on Sesto PHY */
void _set_sesto_phy_slice()
{
  char cmd[64];
  int p;
  int addr[] =
  {
    0x1e,  0x1f,  0xe,   0xf,   0x3e,  0x3f,  0x2e,  0x2f,
    0x11e, 0x11f, 0x10e, 0x10f, 0x13e, 0x13f, 0x12e, 0x12f,
    0x5e,  0x5f,  0x4e,  0x4f,  0x7e,  0x7f,  0x6e,  0x6f,
    0x15e, 0x15f, 0x14e, 0x14f, 0x17e, 0x17f, 0x16e, 0x16f
  };

  for (p = 0; p < COUNTOF(addr); p++)
  {
    sprintf(cmd, "%s %d %s", "phy raw c45", addr[p], "1 0x8000 0");
    sh_process_command(0, cmd);
  }
}

static int num_init_commands;

#define PLATFORM_ID_FILE_1   "/etc/machine.conf"
#define PLATFORM_ID_FILE_2   "/host/machine.conf"

static int get_onie_platform_name(char *onie_platform)
{

  if ((0 == platform_string_find(PLATFORM_ID_FILE_1, onie_platform))
      || (0 == platform_string_find(PLATFORM_ID_FILE_2, onie_platform)))
  {
    return 1;
  }

  return 0;
}

int check_devid(struct platform_data_s *board);
int board_id_default(struct platform_data_s *board);
extern int driverSwitchIdGet(uint16_t *chipId, uint8_t *revision);

struct platform_data_s platform_data[] =
{
  {
    SVK_BCM956846K,
    BCM56846_DEVICE_ID,
    "x86_64-bcm",
    &board_id_default,
    "Trident+ SVK",
    NULL
  },
  {
    SVK_BCM956850K,
    BCM56850_DEVICE_ID,
    "x86_64-bcm",
    &board_id_default,
    "Trident2 SVK",
    NULL
  },
  {
    SVK_BCM956960K,
    BCM56960_DEVICE_ID,
    "x86_64-bcm",
    &board_id_default,
    "Tomahawk SVK",
    NULL
  },
  {
    SVK_BCM956970K,    
    BCM56970_DEVICE_ID,
    "x86_64-bcm",
    &board_id_default,
    "Tomahawk2 SVK",
    NULL
  },
  {
    SVK_BCM956870K,
    BCM56870_DEVICE_ID,
    "x86_64-bcm",
    &board_id_default,
    "Trident3 SVK",
    NULL,
  },
  {
    SVK_BCM956980K,
    BCM56980_DEVICE_ID,
    "x86_64-bcm",
    &board_id_default,
    "Tomahawk3 SVK",
    NULL,
  },
  {
    DELL_S6000,    
    BCM56850_DEVICE_ID,
    "s6000",
    &board_id_default,
    "Dell s6000 Trident2",
    NULL
  },
  {
    DELL_S6100,
    BCM56960_DEVICE_ID,
    "s6100",
    &board_id_default,
    "Dell s6100 Tomahawk",
    &s6100_post_init_commands_t1[0],
  },
  {
    ARISTA_7260CX3,
    BCM56970_DEVICE_ID,
    "x86_64-arista_7260cx3_64",
    &board_id_default,
    "Arista 7260cx3 Tomahawk2",
    NULL,
  },
  {
    CELESTICA_HALIBURTON,
    BCM56340_DEVICE_ID,
    "x86_64-cel_e1031-r0",
    &board_id_default,
    "Haliburton Helix4",
    NULL,
  },
};

struct platform_data_s *discovered_board_data = NULL;

int platform_initialized = 0;

int board_id_default(struct platform_data_s *board)
{
  uint16_t devid = 0;

  driverSwitchIdGet(&devid, NULL);
  if (0 == devid)
  {
    printf("Failed to obtain switch ID.\r\n");
    return 0;
  }

  if (((board->dev_id & 0xFFF0) == (devid & 0xFFF0)) &&
      (get_onie_platform_name(board->onie_platform) == 1))
  {
    return 1;
  }

  return 0;
}

#if 0
extern sal_mutex_t cpld_lock;

static int sfp_power_set(uint32_t port, uint32_t enable)
{
  uint8_t tx_pwr;
  sfp_port_data_t *sfp_port_data = NULL;

  sfp_port_data = sfp_port_data_get(port);

  if ((sfp_port_data != NULL) &&
      (sfp_port_data->sfp_type != HPC_MODULE_NONE))
  {
    if (read_cpld(sfp_port_data->tx_pwr_reg, &tx_pwr) >= 0)
    {
      if (enable)
      {
        tx_pwr &= ~(sfp_port_data->port_bit_mask);
      }
      else
      {
        tx_pwr |= sfp_port_data->port_bit_mask;
      }
      if (write_cpld(sfp_port_data->tx_pwr_reg, tx_pwr) >= 0)
      {
        printf("%s Tx power for SFP port %d by writing 0x%0x to register 0x%0x \n",
               (enable) ? "Enabled":"Disabled", port, tx_pwr, sfp_port_data->tx_pwr_reg);
        return 0;
      }
    }

    printf("Failed to %s Tx power for SFP port %d\n", (enable) ? "enable":"disable", port);
    return -1;
  }

  return -1;
}
#endif

static int board_configure(platform_brd_id_t board_id)
{
#if 0
  int idx;

  /* Following is done in NOS platform driver */
  if(board_id == CELESTICA_HALIBURTON)
  {
#define CPLD_SMC_SEPRST 0x222 /* Seperate Reset Control Register */
    cpld_lock = sal_mutex_create("cpld_lock");
    if(NULL == cpld_lock)
    {
      printf("Failed to create cpld mutex\n");
      return -1;
    }

    /* 1. Take PHY's out of reset, if applicable             *
     * 2. Enable Tx Power for the fiber ports                */

    /* To enable the detection of external PHY's on the platform */
    if (write_cpld(CPLD_SMC_SEPRST, 0x1f) < 0) 
    {
      printf("Failed to reset the PHY's\n");
      return -1;
    }

    for (idx = 50; idx <= 53; idx++) /* Last four ports */
    {
      sfp_power_set(idx, 1);
    }
  }
#endif

  return 0;
}

static int board_init(void)
{
  int i;
  int rc = -1;

  for (i = 0; i < sizeof(platform_data) / sizeof(platform_data[0]); i++)
  {
    if (1 == platform_data[i].board_id_f(&platform_data[i]))
    {
      discovered_board_data = &platform_data[i];
      printf("\r\nFound board: %s\r\n", discovered_board_data->description);
      rc = board_configure(discovered_board_data->board_id);
      platform_initialized = 1;
      break;
    }
  }

  if (platform_initialized == 0)
  {
    /* 
    * If platform was not detected just return success. 
    * SAI will already check if the silicon is support
    */
    rc = 0;
  }

  return rc;
}

char **platform_script_get(void)
{
  char** rs = NULL;
  char* mmu_config;

  if (1 != platform_initialized)
  {
    return NULL;
  }

  if (NULL == discovered_board_data)
  {
    return NULL;
  }

  rs = discovered_board_data->platform_script;

  /* S6100 post init scripts */
  if (discovered_board_data->board_id == DELL_S6100)
  {
    mmu_config = sal_config_get("mmu_init_config");
    if (mmu_config != NULL)
    {
      if (strcmp("\"MSFT-TH-Tier0\"",mmu_config) == 0)
      {
        rs = &s6100_post_init_commands_t0[0];
      }
      if (strcmp("\"MSFT-TH-Tier1\"",mmu_config) == 0)
      {
        rs = &s6100_post_init_commands_t1[0];
      }
    }
  }

  return rs;
}

int platform_board_id_get(void)
{
  if (1 != platform_initialized)
  {
    return -1;
  }

  if (NULL == discovered_board_data)
  {
    return -1;
  }

  return discovered_board_data->board_id;
}


#ifdef INCLUDE_KNET

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <soc/knet.h>
#include <uk-proxy-kcom.h>
#include <bcm-knet-kcom.h>

/* Function defined in linux-user-bde.c */
extern int
bde_irq_mask_set(int unit, uint32 addr, uint32 mask);
extern int
bde_hw_unit_get(int unit, int inverse);

static soc_knet_vectors_t knet_vect_uk_proxy = {
    {
        uk_proxy_kcom_open,
        uk_proxy_kcom_close,
        uk_proxy_kcom_msg_send,
        uk_proxy_kcom_msg_recv
    },
    bde_irq_mask_set,
    bde_hw_unit_get
};

static soc_knet_vectors_t knet_vect_bcm_knet = {
    {
        bcm_knet_kcom_open,
        bcm_knet_kcom_close,
        bcm_knet_kcom_msg_send,
        bcm_knet_kcom_msg_recv
    },
    bde_irq_mask_set,
    bde_hw_unit_get
};

static void
knet_kcom_config(void)
{
    soc_knet_vectors_t *knet_vect;
    char *kcom_name;
    int procfd;
    char procbuf[128];

    /* Direct IOCTL by default */
    knet_vect = &knet_vect_bcm_knet;
    kcom_name = "bcm-knet";

    if ((procfd = open("/proc/linux-uk-proxy", O_RDONLY)) >= 0) {
        if ((read(procfd, procbuf, sizeof(procbuf))) > 0 &&
            strstr(procbuf, "KCOM_KNET : ACTIVE") != NULL) {
            /* Proxy loaded and active */
            knet_vect = &knet_vect_uk_proxy;
            kcom_name = "uk-proxy";
        }
        close(procfd);
    }

    soc_knet_config(knet_vect);
    var_set("kcom", kcom_name, 0, 0);
}

#endif /* INCLUDE_KNET */

void sal_config_init_defaults(void)
{
  /* Place holder */
  return;
}

int bde_create(void)
{
  linux_bde_bus_t bus;
  bus.be_pio = SYS_BE_PIO;
  bus.be_packet = SYS_BE_PACKET;
  bus.be_other = SYS_BE_OTHER;
  return linux_bde_create(&bus, &bde);
}

/*
 * Diagnostics shell routine.
 */

#ifdef BCM_WARM_BOOT_SUPPORT
#if defined(BCM_ESW_SUPPORT) || defined(BCM_SBX_SUPPORT) || defined(BCM_PETRA_SUPPORT) || defined(BCM_DFE_SUPPORT)
STATIC int
platform_scache_read_dummy_func(int unit, uint8 *buf, int offset, int nbytes)
{
  return SOC_E_RESOURCE;
}

STATIC int
platform_scache_write_dummy_func(int unit, uint8 *buf, int offset, int nbytes)
{
  return SOC_E_RESOURCE;
}
#endif /* BCM_ESW_SUPPORT || defined(BCM_SBX_SUPPORT) || defined(BCM_PETRA_SUPPORT) || defined(BCM_DFE_SUPPORT)*/
#endif /* BCM_WARM_BOOT_SUPPORT */

#ifdef BCM_TRIDENT3_SUPPORT
/*****************************************************************//**
 * \brief Get CanCun file prefix for TD3 platform
 *
 * \param unit       [IN]     Unit number
 * \param prefix_p   [IN/OUT] Location where prefix string is written
 * \param prefix_max [IN]     Prefix buffer maximum length
 *
 * \return           pointer to prefix string
 *
 * \notes  None
 *
 ********************************************************************/
static char* get_cancun_td3_prefix(int unit, char* prefix_p, int prefix_max)
{
  if (prefix_p != NULL)
  {
    snprintf(prefix_p, prefix_max, "%s_", SOC_UNIT_NAME(unit));
    convertStrToLowerCase(prefix_p);
  }

  return prefix_p;
}

/*****************************************************************//**
 * \brief Get CanCun files location
 *
 * \param path   [IN/OUT] Location where CanCun files are located
 * \param size   [IN]     Maximum length of the buffer
 *
 * \return  0               Success
 * \return  -1              Failure
 *
 ********************************************************************/
int get_cancun_files_path(char *path, int size)
{
  if(getcwd(path, size) == NULL)
  {
    return -1;
  }
  return 0;
}

/*****************************************************************//**
 * \brief To load TD3 CanCun files
 *
 * \param unit        [IN]     Unit number
 * \param mode        [IN]     CanCun mode (CANCUN_PRE_MISC_INIT, CANCUN_POST_MISC_INIT)
 * \param file_prefix [IN/OUT] CanCun file prefix
 *
 * \return  BCM_E_xxx
 *
 ********************************************************************/
static int cancun_file_load(int unit, cancun_load_mode_t mode, char *file_prefix)
{
  int      rv = BCM_E_NONE;
  char     load_command[CANCUN_LOAD_COMMAND_LEN];
  static char fpath[512] = "";

  if (file_prefix == NULL)
  {
    return BCM_E_PARAM;
  }

  /* Get the location of CanCun files */
  if (fpath == NULL)
  {
    if(get_cancun_files_path(fpath, sizeof(fpath)) != 0)
    {
      printf("Failed to get cancun files directory path\n");
    }
    else
    {
      printf("Cancun files are picked up from \"%s\" \n", fpath);
    }
  }

  do
  {
    if (mode == CANCUN_PRE_MISC_INIT)
    {
      /* Create load command string. */
      snprintf(load_command, sizeof(load_command),
          "CanCun load %s%s%s.pkg", fpath, file_prefix, "cmh");
      if (sh_process_command(unit, load_command) != CMD_OK)
      {
        printf("Failed CanCun load CMH\n");
        rv = BCM_E_INTERNAL;
        break;
      }

      snprintf(load_command, sizeof(load_command),
          "CanCun load %s%s%s.pkg", fpath, file_prefix, "cch");
      if (sh_process_command(unit, load_command) != CMD_OK)
      {
        printf("Failed CanCun load CCH\n");
        rv = BCM_E_INTERNAL;
        break;
      }

      snprintf(load_command, sizeof(load_command),
          "CanCun load %s%s%s.pkg", fpath, file_prefix, "ceh");
      if (sh_process_command(unit, load_command) != CMD_OK)
      {
        printf("Failed CanCun load CEH\n");
        rv = BCM_E_INTERNAL;
        break;
      }

    }
    else if (mode == CANCUN_POST_MISC_INIT)
    {
      /* Create load command string. */
      snprintf(load_command, sizeof(load_command),
          "CanCun load %s%s%s.pkg", fpath, file_prefix, "cih");
      if (sh_process_command(unit, load_command) != CMD_OK)
      {
        printf("Failed CanCun load CIH\n");
        rv = BCM_E_INTERNAL;
        break;
      }

      if (soc_feature(unit, soc_feature_flex_flow))
      {
        /* Create load command string. */
        snprintf(load_command, sizeof(load_command),
            "CanCun load %s%s%s.pkg", fpath, file_prefix, "cfh");
        if (sh_process_command(unit, load_command) != CMD_OK)
        {
          printf("Failed CanCun load CFH\n");
          rv = BCM_E_INTERNAL;
          break;
        }
      }

      if (sh_process_command(unit, "CanCun stat") != CMD_OK)
      {
        printf("Failed CanCun stat command\n");
        rv = BCM_E_INTERNAL;
        break;
      }
    }
  } while (0);

  return rv;
}

/*****************************************************************//**
 * \brief To process CanCun diag shell command
 *
 * \param unit     [IN]     Unit number
 * \param cmd      [IN]     CanCun command
 *
 * \return  None
 *
 ********************************************************************/
void cancun_process_cmd(int unit, char *cmd)
{
  static char prefix[CANCUN_FILENAME_SIZE] = "";

  if(prefix[0] == '\0')
  {
    (void)get_cancun_td3_prefix(unit, prefix, (sizeof(prefix) - 1));
  }

  if (strstr(cmd, "cancun pre-misc-init"))
  {
    (void)cancun_file_load(unit, CANCUN_PRE_MISC_INIT, prefix);
  }
  else if (strstr(cmd, "cancun post-misc-init"))
  {
    (void)cancun_file_load(unit, CANCUN_POST_MISC_INIT, prefix);
  }
  else
  {
    printf("Invalid CanCun command\n");
  }

  return;
}
#endif

/* Platform specific SDK initialization */
int platformInit(sai_init_t *init)
{
  uint32  flags;
#ifndef NO_SAL_APPL
  char    *script;
  int     j;
  int     rc;
  char    **platform_script;
  char    path[256];
#endif
  int i;
#ifdef BCM_WARM_BOOT_SUPPORT
#if defined(BCM_ESW_SUPPORT)
  int     warm_boot = FALSE;
  int     stable_location = BCM_SWITCH_STABLE_NONE;
  uint32  stable_flags = 0;
  uint32  stable_size = 0;
  char    *stable_filename = NULL;
#endif /* BCM_ESW_SUPPORT */
#endif /* BCM_WARM_BOOT_SUPPORT */
#ifdef BCM_EASY_RELOAD_SUPPORT
#if defined(BCM_DFE_SUPPORT)
  int easy_reload = FALSE;
#endif
#endif
  int mode;

  static int init_done = FALSE;

  if(init != NULL && (SAI_F_FAST_BOOT & init->sai_flags))
  {
    printf("Boot flags: Fast boot\n");
  }

  /* Identify the method used to pick up the configuration file */
  if(platform_config_init(init) != 0)
  {
    printf("Platform config initilization failed.\r\n");
    return -1;
  }

  if(platform_config_method_get(&mode) != 0)
  {
    printf("Failed to get platform bootup method.\r\n");
    return -1;
  }

  if (CELESTICA_HALIBURTON == platform_board_id_get()
      && (mode == PLATFORM_CONFIG_DEFAULT))
  {
    printf("Platform does not support default boot configuration. "
        "Please specify platform configuration file as input.\r\n");
    return -1;
  }

  rc = sal_core_init();
  if (BCM_FAILURE(rc))
  {
    printf("sal_core_init failed, rc = %d (%s).\r\n", rc, bcm_errmsg(rc));
    return -1;
  }

  rc = sal_appl_init();
  if (BCM_FAILURE(rc))
  {
    printf("sal_appl_init failed, rc = %d (%s).\r\n", rc, bcm_errmsg(rc));
    return -1;
  }

  if (mode == PLATFORM_CONFIG_DEFAULT)
  {
    for (i = 0; i < COUNTOF(common_variables); i++)
    {
      rc = sal_config_set(common_variables[i].name, common_variables[i].value);
      if (BCM_FAILURE(rc))
      {
        printf("configuration of \"%s\" failed, rc = %d (%s).\r\n",
            common_variables[i].name, rc, bcm_errmsg(rc));
      }
    }
  }

#ifdef INCLUDE_KNET
  knet_kcom_config();
#endif

  sal_thread_main_set(sal_thread_self());

#if defined(INCLUDE_EDITLINE)
  sal_readline_config(diag_complete, diag_list_possib);
#endif /* INCLUDE_EDITLINE */

  parse_user_var_get = diag_user_var_get;

  bslmgmt_init();

  assert(sizeof(uint8) == 1);
  assert(sizeof(uint16) == 2);
  assert(sizeof(uint32) == 4);
  assert(sizeof(uint64) == 8);

  sal_assert_set(_diag_assert);

  init_symtab();

  sal_srand(1);   /* Seed random number generator: arbitrary value */

#ifndef NO_SAL_APPL
  /*
   * Path (default is current directory followed by home directory)
   */

  sal_strcpy(path, ". ");
  sal_homedir_get(path + 2, sizeof (path) - 2);
  var_set("path", path, TRUE, FALSE);
#endif

  if (SAL_BOOT_PLISIM) {
    var_set_integer("plisim", 1, 0, 0);
  }

#ifdef UNIX
  var_set_integer("unix", 1, 0, 0);
#endif

  sh_bg_init();

  sysconf_init();

  /*
   * At boot time, probe for devices and attach the first one.
   * In PLISIM, this is not done; the probe and attach commands
   * must be given explicitly.
   */
  flags = sal_boot_flags_get();

  if ((sysconf_probe()) < 0) {
      printf("ERROR: PCI SOC device probe failed\n");
  }

  printf("\r\nInitializing platform\r\n");
  rc = board_init();
  if (0 != rc)
  {
    printf("\r\nError initializing platform, rc = %d.\r\n", rc);
    return -1;
  }

  var_set_integer("units", soc_ndev, FALSE, FALSE);

  /* Apply configuration passed through the config file */
  if(platform_config_apply(init) != 0)
  {
    printf("Failed to apply the configuration during "
        "platform initialization.\r\n");
    return -1;
  }

  /** For CS5922295 */
  if (NULL == sal_config_get("bcm_tunnel_term_compatible_mode"))
  {
    sal_config_set("bcm_tunnel_term_compatible_mode", "1");
  }

  for (i = 0; i < soc_all_ndev; i++) {
      if (sysconf_attach(SOC_NDEV_IDX2DEV(i)) < 0) {
          printf("ERROR: SOC unit %d attach failed\n", SOC_NDEV_IDX2DEV(i));
      }
  } /* for */
  
  cmdlist_init();

#ifdef BCM_WARM_BOOT_SUPPORT
#if defined(BCM_ESW_SUPPORT)

  if (flags & BOOT_F_WARM_BOOT) {
    printf("Boot flags: Warm boot\n");
    warm_boot = TRUE;

    for (i = 0; i < soc_ndev; i++) {
      SOC_WARM_BOOT_START(SOC_NDEV_IDX2DEV(i));
#ifndef NO_SAL_APPL
      diag_rc_set(SOC_NDEV_IDX2DEV(i), "reload.soc");
#endif
    }
  } else {
    printf("Boot flags: Cold boot\n");
    for (i = 0; i < soc_ndev; i++) {
      SOC_WARM_BOOT_DONE(SOC_NDEV_IDX2DEV(i));
    }
  }

  for (i = 0; i < soc_ndev; i++) {
    stable_filename = soc_property_get_str(SOC_NDEV_IDX2DEV(i), spn_STABLE_FILENAME);

    if ((NULL == stable_filename) &&
        (stable_filename =
         soc_property_get_str(SOC_NDEV_IDX2DEV(i), "scache_filename"))) {
      stable_location = _SHR_SWITCH_STABLE_APPLICATION;
      stable_flags = 0;
      stable_size = SOC_DEFAULT_LVL2_STABLE_SIZE;
      if ((SOC_CONTROL(SOC_NDEV_IDX2DEV(i)) != NULL) &&
          soc_feature(SOC_NDEV_IDX2DEV(i), soc_feature_ser_parity) &&
          soc_property_get(SOC_NDEV_IDX2DEV(i), "memcache_in_scache", 0)) {
        if (SOC_IS_TD2_TT2(SOC_NDEV_IDX2DEV(i))) {
          uint16 dev_id;
          uint8  rev_id;
          soc_cm_get_id(SOC_NDEV_IDX2DEV(i), &dev_id, &rev_id);
          if (dev_id == BCM56830_DEVICE_ID) {
            stable_size *= 20;
          } else {
            stable_size *= 12;
          }
        } else if (SOC_IS_TRIUMPH3(SOC_NDEV_IDX2DEV(i))) {
          stable_size *= 30;
        } else if (SOC_IS_TD_TT(SOC_NDEV_IDX2DEV(i))) {
          stable_size *= 14;
        } else {
          stable_size *= 16;
        }
      }

      if ((SOC_CONTROL(SOC_NDEV_IDX2DEV(i)) != NULL) &&
          SOC_IS_TOMAHAWKX(SOC_NDEV_IDX2DEV(i))) {
        stable_size = 85  * 1024 * 1024;
      }
      /* Recently increased field scache size to push more data. VERSION_1_15 */
      if ((SOC_CONTROL(SOC_NDEV_IDX2DEV(i)) != NULL) &&
          SOC_IS_TD_TT(SOC_NDEV_IDX2DEV(i))) {

        if(SOC_IS_APACHE(SOC_NDEV_IDX2DEV(i))) {
          /* Default scache size 7MB is needed for apache family */
          stable_size += 4 * 1024 * 1024;
        } else if (SOC_IS_TRIDENT2X(SOC_NDEV_IDX2DEV(i))) {
          /* Default scache size 16MB is needed for trident2x */
          stable_size += 13 * 1024 * 1024;
        } else {
          stable_size += 135 * 1024;
        }

      }

      if ((SOC_CONTROL(SOC_NDEV_IDX2DEV(i)) != NULL) &&
          SOC_IS_TRIUMPH3(SOC_NDEV_IDX2DEV(i))) {
        stable_size += 2*1024 * 1024;
      }
    }

    stable_location = soc_property_get(SOC_NDEV_IDX2DEV(i), spn_STABLE_LOCATION,
        stable_location);

#ifndef CRASH_RECOVERY_SUPPORT
    if (stable_location == _SHR_SWITCH_STABLE_SHARED_MEM) {
      stable_location =  _SHR_SWITCH_STABLE_APPLICATION;
      printf(
          "Unit %d: soc property stable_location=4 is only supported with CRASH_RECOVERY compilation, falling back to stable_location=3\n",
          SOC_NDEV_IDX2DEV(i));
    }
#endif

    if (BCM_SWITCH_STABLE_NONE != stable_location) {
      /* Otherwise, nothing to do */
      stable_flags = soc_property_get(SOC_NDEV_IDX2DEV(i), spn_STABLE_FLAGS,
          stable_flags);
      stable_size = soc_property_get(SOC_NDEV_IDX2DEV(i), spn_STABLE_SIZE,
          stable_size);

      if ((BCM_SWITCH_STABLE_APPLICATION == stable_location) &&
          (NULL != stable_filename)) {
#if (defined(LINUX) && !defined(__KERNEL__)) || defined(UNIX)
        /* Try to open the scache file */
        if (appl_scache_file_open(SOC_NDEV_IDX2DEV(i), (flags & BOOT_F_WARM_BOOT) ?
              TRUE : FALSE,
              stable_filename) < 0) {
          printf("Unit %d: stable cache file not %s\n", SOC_NDEV_IDX2DEV(i),
              (flags & BOOT_F_WARM_BOOT) ?
              "recovered" : "created");

          /* Fall back to Level 1 Warm Boot */
          stable_location = BCM_SWITCH_STABLE_NONE;
          stable_size = 0;
        }

#else /* (defined(LINUX) && !defined(__KERNEL__)) || defined(UNIX) */
        printf("Build-in stable cache file not supported in this configuration\n");
        /* Use Level 1 Warm Boot instead*/
        stable_location = BCM_SWITCH_STABLE_NONE;
        stable_size = 0;
#endif /* (defined(LINUX) && !defined(__KERNEL__)) || defined(UNIX) */
      }

      if (soc_stable_set(SOC_NDEV_IDX2DEV(i), stable_location, stable_flags) < 0) {
        printf("Unit %d: soc_stable_set failure\n", SOC_NDEV_IDX2DEV(i));
      } else if (soc_stable_size_set(SOC_NDEV_IDX2DEV(i), stable_size) < 0) {
        printf("Unit %d: soc_stable_size_set failure\n", SOC_NDEV_IDX2DEV(i));
      }

      if (1 == soc_property_get(SOC_NDEV_IDX2DEV(i),
            spn_WARMBOOT_EVENT_HANDLER_ENABLE, 0)) {
        if (soc_event_register(SOC_NDEV_IDX2DEV(i),
              appl_warm_boot_event_handler_cb, NULL) < 0) {
          printf("Unit %d: soc_event_register failure\n", SOC_NDEV_IDX2DEV(i));
        }
      }
    } else {

      /* EMPTY SCACHE INITIALIZATION ->
       * in case stable_* parameters are not defined in configuration file,
       * initiating scache with size 0(zero). in order that scache commits
       * wont fail and cause application exit upon startup */
      if (soc_switch_stable_register(SOC_NDEV_IDX2DEV(i),
            &platform_scache_read_dummy_func,
            &platform_scache_write_dummy_func,
            NULL, NULL) < 0) {
        printf("Unit %d: soc_switch_stable_register failure\n", SOC_NDEV_IDX2DEV(i));
      }

      stable_location = BCM_SWITCH_STABLE_NONE;
      stable_size     = 0;
      stable_flags    = 0;

      if (soc_stable_set(SOC_NDEV_IDX2DEV(i), stable_location, stable_flags) < 0) {
        printf("Unit %d: soc_stable_set failure\n", SOC_NDEV_IDX2DEV(i));
      } else if (soc_stable_size_set(SOC_NDEV_IDX2DEV(i), stable_size) < 0) {
        printf("Unit %d: soc_stable_size_set failure\n", SOC_NDEV_IDX2DEV(i));
      }
      /* <- EMPTY SCACHE INITIALIZATION */

      /* Treat all virtual ports as MIM for Level 1 */
      SOC_WARM_BOOT_MIM(SOC_NDEV_IDX2DEV(i));
    }
  }
#endif /* BCM_ESW_SUPPORT */
#endif /* BCM_WARM_BOOT_SUPPORT */

#ifndef NO_SAL_APPL
  /* Add backdoor for mem tuner to update system configuration */
  soc_mem_config_set = sal_config_set;

  /*
   * If a startup script is given in the boot parameters, attempt to
   * load it.  This script is for general system configurations such
   * as host table additions and NFS mounts.
   */

  if ((script = sal_boot_script()) != NULL) {
    if (sh_rcload_file(-1, NULL, script, FALSE) != CMD_OK) {
      printf("ERROR loading boot init script: %s\n", script);
    }
  }

  platform_script = platform_script_get();

  for (i = 0; i < soc_ndev; i++) {
    if (soc_attached(SOC_NDEV_IDX2DEV(i))) {
      sh_swap_unit_vars(SOC_NDEV_IDX2DEV(i));
      if (SOC_IS_RCPU_ONLY(SOC_NDEV_IDX2DEV(i))) {
        /* Wait for master unit to establish link */
        sal_sleep(3);
      }

      if (warm_boot == FALSE)
      {
          num_init_commands = sizeof(common_init_commands) / sizeof(common_init_commands[0]);
      }
      else
      {
          num_init_commands = sizeof(warmboot_init_commands) / sizeof(warmboot_init_commands[0]);
      }
      for (j = 0; j < num_init_commands; j++)
      {
          char* current_cmd;
          if (warm_boot == FALSE)
          {
              current_cmd = common_init_commands[j];
          }
          else
          {
              current_cmd = warmboot_init_commands[j];
          }
          
          if (strstr(current_cmd, "set sesto slice"))
          {
              if (DELL_S6100 == platform_board_id_get())
              {
                  _set_sesto_phy_slice();
              }
              continue;
          }
          
          if (strstr(current_cmd, "cancun"))
          {
#ifdef BCM_TRIDENT3_SUPPORT
              if (soc_feature(SOC_NDEV_IDX2DEV(i), soc_feature_cancun))
              {
                  (void) cancun_process_cmd(SOC_NDEV_IDX2DEV(i), current_cmd);
              }
#endif
              continue;
          }
          if (SOC_IS_TRIDENT3X(SOC_NDEV_IDX2DEV(i))) {
              if (strstr(current_cmd, "led")) {
                  continue;
              }
          }
          rc = sh_process_command(SOC_NDEV_IDX2DEV(i), current_cmd);
          if (BCM_FAILURE(rc))
          {
              printf("initialization command \"%s\" failed, rc = %d (%s).\r\n",
                     current_cmd, rc, bcm_errmsg(rc));
          }
      }
      printf("Common SDK init completed\r\n");
      
      /* Skip commands from platform script during fastboot/warmboot */
      if ((NULL != platform_script) 
#ifdef BCM_WARM_BOOT_SUPPORT
#if defined(BCM_ESW_SUPPORT)
          && (warm_boot == FALSE)
#endif
#endif
         )
      {
        j = 0;
        do
        {
          rc = sh_process_command(0, platform_script[j]);
          if (BCM_FAILURE(rc))
          {
            printf("initialization command \"%s\" failed, rc = %d (%s).\r\n",
                               platform_script[j], rc, bcm_errmsg(rc));
          }
          j++;
        } while (NULL != platform_script[j]);
        sh_process_command(0, "echo rc: platform SDK init complete");
      }
    }
  }

  if (soc_ndev <= 0) {
    printf("No attached units.\n");
  }

#endif /* NO_SAL_APPL */

#ifdef BCM_WARM_BOOT_SUPPORT
#if defined(BCM_ESW_SUPPORT)
  if (warm_boot) {
    /* Warm boot is done, clear reloading state */
    for (i = 0; i < soc_ndev; i++) {
      SOC_WARM_BOOT_DONE(SOC_NDEV_IDX2DEV(i));
    }
  }
#endif /* BCM_ESW_SUPPORT */
#endif /* BCM_WARM_BOOT_SUPPORT */

  for (i = 0; i < soc_ndev; i++)
  {
    rc = bcm_rx_init(SOC_NDEV_IDX2DEV(i));
    if (BCM_FAILURE(rc))
    {
      printf("RX init failed, unit %d rc %d \n", SOC_NDEV_IDX2DEV(i), rc);
      break;
    }
  }

  /* Load post-SDK initialization SOC file. This will allow the user to *
   * run diag commands after system initialization. */
  if(platform_config_post_init(init) != 0)
  {
    printf("Failed to apply the post init configuration during "
        "platform initialization.\r\n");
    /* Not returning error code intentionally here */
  }

#ifdef BCM_DIAG_SHELL_CUSTOM_INIT_F
  {
    /* Call custom init function prior to entering input loop */
    extern int BCM_DIAG_SHELL_CUSTOM_INIT_F (void);
    BCM_DIAG_SHELL_CUSTOM_INIT_F ();
  }
#endif /* BCM_DIAG_SHELL_CUSTOM_INIT_F */

  init_done = TRUE;
  
  return 0;
}

void
platform_phy_cleanup()
{
  if (DELL_S6100 == platform_board_id_get())
  {
    _set_sesto_phy_slice();
  }
}
