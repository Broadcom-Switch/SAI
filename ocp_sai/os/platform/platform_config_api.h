/*********************************************************************
 *
 * Copyright: (c) 2018 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#ifndef PLATFORM_CONFIG_API_H
#define PLATFORM_CONFIG_API_H

#include <stdint.h>
#include "bcm/port.h"
#include "driver_util.h"

#define PLAT_SCACHE_FILENAME    "/tmp/scache"
#define PLAT_STABLE_SIZE        "0x2000000"     /* 32 MB */

typedef struct switch_config_variables_s
{
  char *name;
  char *value;
} switch_config_variables_t;

int platformInit(sai_init_t *init);

#endif /* PLATFORM_CONFIG_API_H */
