/*********************************************************************
 *
 * Copyright: (c) 2017 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#define BRCM_SAI_VER "3.3.6.2"
#define OCP_SAI_VER  "1.3.7"
#define SDK_VER      "6.5.13"

typedef struct _brcm_sai_version_s {
    char *brcm_sai_ver;
    char *ocp_sai_ver;
    char *sdk_ver;
} _brcm_sai_version_t;

_brcm_sai_version_t brcm_sai_version_get();

