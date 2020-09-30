/*********************************************************************
 *
 * Copyright: (c) 2018 Broadcom.
 * Broadcom Proprietary and Confidential. All rights reserved.
 *
 *********************************************************************/

#ifndef PLATFORM_FILE__H
#define PLATFORM_FILE__H

#include <stdint.h>
#include "driver_util.h"


/*****************************************************************//**
* \brief convert all letters inside a buffer to lower case
*
* \param buf       [IN/OUT]  name of the buffer
*
* \return void
* \note   This f(x) returns the same letter in the same buffer but all
*         lower case, checking the buffer for empty string
*
********************************************************************/
void convertStrToLowerCase(char *buf);

#endif /* PLATFORM_FILE_H */
