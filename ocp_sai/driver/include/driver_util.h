/*********************************************************************
 *
 * Copyright: (c) 2018 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#ifndef DRIVER_UTIL_H
#define DRIVER_UTIL_H

#include <stdint.h>

#define LIB_DLL_EXPORTED __attribute__((__visibility__("default")))
#define CMD_LEN_MAX 256

#define SAI_F_FAST_BOOT   0x00000001  /* Fast boot mode */

typedef struct sai_config_s
{
  char         *cfg_fname; /* Configuration file name along with the path */
  unsigned int flags;      /* SAI boot up flags */
  char         *wb_fname;  /* File to store warmboot configuration *
                            * along with the path */
  char         *rmcfg_fname; /* RM config file name along with the path */
  char         *cfg_post_fname;  /* Post init configuration file name *
                                  * along with the path */
  unsigned int sai_flags;  /* SAI flags */
} sai_init_t;

/*****************************************************************//**
* \brief Function to initialize the switch.
*
* \param init       [IN]   pointer to structure that contains path to
*                          platform customization config file, boot flags.
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
extern int sai_driver_init(sai_init_t *init) LIB_DLL_EXPORTED ;

/*****************************************************************//**
* \brief Function to free up the resources and exit the driver
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
extern int sai_driver_exit() LIB_DLL_EXPORTED;

/*****************************************************************//**
* \brief Function to clear the flags necessary for warm boot support
*
* \return SAI_E_NONE     SAI API return code
********************************************************************/
int sai_warm_shut(void);


extern unsigned int sai_driver_boot_flags_get(void) LIB_DLL_EXPORTED;

int driverSwitchIdGet(uint16_t *chipId, uint8_t *revision);

#ifdef INCLUDE_DIAG_SHELL
/*****************************************************************//**
* \brief Bringup diagnostic shell prompt and process the input commands.
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
int sai_driver_shell() LIB_DLL_EXPORTED;

/*****************************************************************//**
* \brief Process diagnostic shell command.
*
* \param commandBuf    [IN]    pointer to hold the diagnostic shell command
*
* \return SAI_E_XXX     SAI API return code
********************************************************************/
int sai_driver_process_command(char *commandBuf) LIB_DLL_EXPORTED;
#endif

int driver_config_set(char *name, char *value);
int driver_process_command(int u, char *c);

extern int driverA2BGet(int val) LIB_DLL_EXPORTED;

extern void platform_phy_cleanup() LIB_DLL_EXPORTED;

#endif  /* DRIVER_UTIL_H */
