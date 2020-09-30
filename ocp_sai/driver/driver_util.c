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

#include "driver_util.h"
#include "platform_config_api.h"
#include "bcm/error.h"
#include "error.h"


#include "soc/drv.h"
#if defined(BCM_TRIDENT2_SUPPORT) || defined(BCM_TOMAHAWK_SUPPORT)
#include "bcm_int/esw/trident2.h"
#include "soc/trident2.h"
#include "bcm_int/esw/tomahawk.h"
#include "soc/tomahawk.h"
#include "soc/tomahawk2.h"
#endif


#include "sal/appl/sal.h"
#include "sal/appl/config.h"
#include "sal/core/boot.h"
#include "appl/diag/shell.h"
#include "soc/devids.h"
#include "soc/cm.h"
#include "sal/appl/pci.h"
#include "sal/appl/editline/editline.h"
#include "linux-bde.h"

/* The bus properties are (currently) the only system specific
 * settings required.
 * These must be defined beforehand
 */

#ifndef PLATFORM_BCMSIM
#ifndef SYS_BE_PIO
#error "SYS_BE_PIO must be defined for the target platform"
#endif
#ifndef SYS_BE_PACKET
#error "SYS_BE_PACKET must be defined for the target platform"
#endif
#ifndef SYS_BE_OTHER
#error "SYS_BE_OTHER must be defined for the target platform"
#endif

#if !defined(SYS_BE_PIO) || !defined(SYS_BE_PACKET) || !defined(SYS_BE_OTHER)
#error "platform bus properties not defined."
#endif
#endif /* PLATFORM_BCMSIM */

static int verbose = 0;

#define SAI_BOOT_F_DEFAULT          0x000000
#define SAI_BOOT_F_WARM_BOOT        0x200000

/**************************************************************************//**
* \brief Get the SAI boot flags
*
* \return      unsigned int  sai platform boot flags
*****************************************************************************/
unsigned int sai_driver_boot_flags_get(void)
{
  return sal_boot_flags_get();
}

extern void _brcm_sai_ver_print();

#ifdef INCLUDE_DIAG_SHELL
/*****************************************************************//**
* \brief Bringup diagnostic shell prompt and process the input commands.
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
int sai_driver_shell()
{

  int rv = SAI_E_NONE;
  char *buffer;
  int c;

  /* Remove unwanted characters including new line from the input buffer */
  do {
    c = getchar();
  } while((c != EOF) && (c != '\n'));

  printf("Enter 'quit' to exit the application.\r\n");

  do {

    /* Read the input command */
    buffer = readline("drivshell>");

    /* Check for length of the command */
    if (strlen(buffer) > CMD_LEN_MAX) {
      printf("Unknown command.\r\n");
      continue;
    }

    /* Check if the user wants to exit the application */
    if(strcmp(buffer, "quit") == 0) {
      printf("Exiting the application.\r\n");
      break;
    }
    if(strcmp(buffer, "bsv") == 0) {
      _brcm_sai_ver_print();
      continue;
    }

    /* Pass the input command to the diagnostic shell */
    rv = sai_driver_process_command(buffer);

    if(rv == SAI_E_UNAVAIL) {
      printf("\r\nCommand \"%s\" is not supported. "
          "Error: %s.\r\n", buffer, bcm_errmsg(rv));
    } else if(rv != SAI_E_NONE) {
      printf("\r\nFailed to execute the diagnostic command. Error: %s.\r\n",
          bcm_errmsg(rv));
    }
  } while(1);

  return rv;
}
#endif /* INCLUDE_DIAG_SHELL */


 static int systemMappingInitialized = 0;
/*****************************************************************//**
* \brief Function to initialize the switch.
*
* \param init      [IN]    pointer to structure that contains path to
*                          platform customization config file, boot flags.
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
int sai_driver_init(sai_init_t *init)
{
  int rc;
 

  if(systemMappingInitialized != 0)
  {
    printf("\r\nDriver is already initialized.\r\n");
    return SAI_E_NONE;
  }

  /* Invoke platform specific initialization */
  rc = platformInit(init);

  if (0 != rc)
  {
    printf("\r\nError initializing driver, rc = %d.\r\n", rc);
    return SAI_E_FAIL;
  }

  systemMappingInitialized = 1;
 
  return SAI_E_NONE;
}

/*****************************************************************//**
* \brief Function to clear the flags necessary for warm boot support
*
* \return SAI_E_NONE     SAI API return code
********************************************************************/
int sai_warm_shut(void)
{
  systemMappingInitialized = 0;
  return SAI_E_NONE;
}

/*****************************************************************//**
* \brief Function to free up the resources and exit the driver
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
int sai_driver_exit()
{
#ifdef SAI_PRODUCT_DNX
  char *buffer = "exit";
#else
  char *buffer = "exit clean";
#endif
  (void) sai_driver_process_command(buffer);
  return SAI_E_NONE;
}

int
driverA2BGet(int val)
{
    return SOC_INFO(0).port_l2p_mapping[val];
}

/*****************************************************************
* \brief To get the chipId and revision number of the switch
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
int driverSwitchIdGet(uint16_t *chipId, uint8_t *revision)
{
  return soc_cm_get_id(0, chipId, revision);
}


#ifdef INCLUDE_DIAG_SHELL
/*****************************************************************//**
* \brief Process diagnostic shell command.
*
* \param commandBuf    [IN]    pointer to hold the diagnostic shell command
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
int sai_driver_process_command(char *commandBuf)
{
  int rc;

  if(verbose > 0) {
    printf("Received command = \"%s\".\r\n\r\n", commandBuf);
  }

  rc = sh_process_command(0, commandBuf);
  printf("\r\n");
  if ((rc < 0) && (rc != CMD_EXIT))
  {
    printf("command \"%s\" failed.  rc = %d.\r\n", commandBuf, rc);
  }

  return rc;
}
#endif /* INCLUDE_DIAG_SHELL */


int driver_config_set(char *name, char *value)
{
  int rc;

#if 0
  printf("Setting config variable \"%s\" to \"%s\".\r\n",
      name, value);
#endif
  rc = sal_config_set(name, value);
  if (BCM_FAILURE(rc))
  {
    printf("Configuration of \"%s\" failed, rc = %d (%s).\r\n",
        name, rc, bcm_errmsg(rc));
  }
  return rc;
}

int driver_process_command(int u, char *c)
{
  cmd_result_t rc;

  rc = sh_process_command(u, c);
  if (CMD_OK != rc)
  {
    printf("Platform command \"%s\" failed, rc = %d.\r\n", c, rc);
    return -1;
  }

  return 0;
}

#ifndef PLATFORM_BCMSIM
/*****************************************************************//**
* \brief This is required for SDK to link
*
* \return  None
*
********************************************************************/
void pci_print_all(void)
{
  int device;

  if (NULL == bde) {
    sal_printf("Devices not probed yet.\n");
    return;
  }

  sal_printf("Scanning function 0 of devices 0-%d\n", bde->num_devices(BDE_SWITCH_DEVICES) - 1);
  sal_printf("device fn venID devID class  rev MBAR0    MBAR1    IPIN ILINE\n");

  for (device = 0; device < bde->num_devices(BDE_SWITCH_DEVICES); device++) {
    uint32		vendorID, deviceID, class, revID;
    uint32		MBAR0, MBAR1, ipin, iline;

    vendorID = (bde->pci_conf_read(device, PCI_CONF_VENDOR_ID) & 0x0000ffff);

    if (vendorID == 0)
	    continue;


#define CONFIG(offset)	bde->pci_conf_read(device, (offset))

    deviceID = (CONFIG(PCI_CONF_VENDOR_ID) & 0xffff0000) >> 16;
    class    = (CONFIG(PCI_CONF_REVISION_ID) & 0xffffff00) >>  8;
    revID    = (CONFIG(PCI_CONF_REVISION_ID) & 0x000000ff) >>  0;
    MBAR0    = (CONFIG(PCI_CONF_BAR0) & 0xffffffff) >>  0;
    MBAR1    = (CONFIG(PCI_CONF_BAR1) & 0xffffffff) >>  0;
    iline    = (CONFIG(PCI_CONF_INTERRUPT_LINE) & 0x000000ff) >>  0;
    ipin     = (CONFIG(PCI_CONF_INTERRUPT_LINE) & 0x0000ff00) >>  8;

#undef CONFIG

    sal_printf("%02x  %02x %04x  %04x  "
               "%06x %02x  %08x %08x %02x   %02x\n",
               device, 0, vendorID, deviceID, class, revID,
               MBAR0, MBAR1, ipin, iline);
  }
}
#endif /* PLATFORM_BCMSIM */

