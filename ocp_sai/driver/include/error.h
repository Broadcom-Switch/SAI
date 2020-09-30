/** \addtogroup error Error Handling
 *  @{
 */
/*****************************************************************************
 * 
 * (C) Copyright Broadcom Corporation 2013-2016
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * 
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 ***************************************************************************//**
 * \file			error.h
 ******************************************************************************/

#ifndef __SAI_ERROR_H__
#define __SAI_ERROR_H__

#include <shared/error.h>

/** 
 * SAI API error codes.
 * 
 * Note: An error code may be converted to a string by passing the code
 * to sai_errmsg().
 */
typedef enum sai_error_e {
    SAI_E_NONE         = _SHR_E_NONE, 
    SAI_E_INTERNAL     = _SHR_E_INTERNAL, 
    SAI_E_MEMORY       = _SHR_E_MEMORY, 
    SAI_E_UNIT         = _SHR_E_UNIT, 
    SAI_E_PARAM        = _SHR_E_PARAM, 
    SAI_E_EMPTY        = _SHR_E_EMPTY, 
    SAI_E_FULL         = _SHR_E_FULL, 
    SAI_E_NOT_FOUND    = _SHR_E_NOT_FOUND, 
    SAI_E_EXISTS       = _SHR_E_EXISTS, 
    SAI_E_TIMEOUT      = _SHR_E_TIMEOUT, 
    SAI_E_BUSY         = _SHR_E_BUSY, 
    SAI_E_FAIL         = _SHR_E_FAIL, 
    SAI_E_DISABLED     = _SHR_E_DISABLED, 
    SAI_E_BADID        = _SHR_E_BADID, 
    SAI_E_RESOURCE     = _SHR_E_RESOURCE, 
    SAI_E_CONFIG       = _SHR_E_CONFIG, 
    SAI_E_UNAVAIL      = _SHR_E_UNAVAIL, 
    SAI_E_INIT         = _SHR_E_INIT, 
    SAI_E_PORT         = _SHR_E_PORT 
} sai_error_t;

/** Switch event types */
typedef enum sai_switch_event_e {
    SAI_SWITCH_EVENT_PARITY_ERROR          =   _SHR_SWITCH_EVENT_PARITY_ERROR, 
    SAI_SWITCH_EVENT_STABLE_FULL           =   _SHR_SWITCH_EVENT_STABLE_FULL, 
    SAI_SWITCH_EVENT_STABLE_ERROR          =   _SHR_SWITCH_EVENT_STABLE_ERROR, 
    SAI_SWITCH_EVENT_UNCONTROLLED_SHUTDOWN =   _SHR_SWITCH_EVENT_UNCONTROLLED_SHUTDOWN, 
    SAI_SWITCH_EVENT_WARM_BOOT_DOWNGRADE   =   _SHR_SWITCH_EVENT_WARM_BOOT_DOWNGRADE, 
    SAI_SWITCH_EVENT_MMU_BST_TRIGGER       =   _SHR_SWITCH_EVENT_MMU_BST_TRIGGER, 
} sai_switch_event_t;

#define SAI_SUCCESS(rv)     \
    _SHR_E_SUCCESS(rv) 
#define SAI_FAILURE(rv)     \
    _SHR_E_FAILURE(rv) 
#define SAI_IF_ERROR_RETURN(op)  \
    _SHR_E_IF_ERROR_RETURN(op) 
#define SAI_IF_ERROR_NOT_UNAVAIL_RETURN(op)  \
    _SHR_E_IF_ERROR_NOT_UNAVAIL_RETURN(op) 
#define sai_errmsg(rv)      \
    _SHR_ERRMSG(rv) 
#endif /* __SAI_ERROR_H__ */
/*@}*/
