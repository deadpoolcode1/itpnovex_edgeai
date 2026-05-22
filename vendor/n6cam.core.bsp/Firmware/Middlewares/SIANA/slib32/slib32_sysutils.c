/**
 *******************************************************************************
 * @file    slib32_sysutils.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements various system utilities
 * @note    Depends on STM-HAL
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 SIANA Systems. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *******************************************************************************
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "slib32_sysutils.h"
#include "stm32_hal.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup SysUtils
* @{
* @defgroup PUBLIC_Definitions          PUBLIC constants
* @defgroup PUBLIC_Macros               PUBLIC macros
* @defgroup PUBLIC_Types                PUBLIC data-types
* @defgroup PUBLIC_Data                 PUBLIC data / variables
* @defgroup PUBLIC_API                  PUBLIC API
* @defgroup PRIVATE_TUNABLES            PRIVATE compile-time tunables
* @defgroup PRIVATE_Definitions         PRIVATE constants
* @defgroup PRIVATE_Macros              PRIVATE macros
* @defgroup PRIVATE_Types               PRIVATE data-types
* @defgroup PRIVATE_Data                PRIVATE data / variables
* @defgroup PRIVATE_Functions           PRIVATE functions
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_TUNABLES
* @{
*//*--------------------------------------------------------------------------*/

/** SYSUTILS not available */
#ifndef SLIB32_SYSUTILS_NOT_AVAILABLE
  #define SLIB32_SYSUTILS_NOT_AVAILABLE   "N/A"
#endif /* SLIB32_SYSUTILS_NOT_AVAILABLE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Critical section context */
typedef struct
{
  size_t    nesting;
  uint32_t  primask;
} t_critical;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_critical _critical = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

WEAK int32_t sysutils_get_firmware_version_string(uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Generate version */
  snprintf((char*)buff, size, SLIB32_SYSUTILS_NOT_AVAILABLE);
  return (int32_t)strlen((char*)buff);
}

WEAK int32_t sysutils_get_hardware_version_string(uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Generate version */
  snprintf((char*)buff, size, SLIB32_SYSUTILS_NOT_AVAILABLE);
  return (int32_t)strlen((char*)buff);
}

WEAK int32_t sysutils_get_mcu_uptime_string(uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Generate version */
  snprintf((char*)buff, size, SLIB32_SYSUTILS_NOT_AVAILABLE);
  return (int32_t)strlen((char*)buff);
}

WEAK int32_t sysutils_get_mcu_uid_string(uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Generate UID */
  uint32_t uid[3U] = {0U};
  uid[0] = HAL_GetUIDw0();
  uid[1] = HAL_GetUIDw1();
  uid[2] = HAL_GetUIDw2();
  snprintf((char*)buff, size, "%08" PRIX32 "%08" PRIX32 "%08" PRIX32, uid[2], uid[1], uid[0]);
  return (int32_t)strlen((char*)buff);
}

WEAK void sysutils_mcu_reboot(const t_stream *stream, int32_t reason)
{
  UNUSED(stream);
  UNUSED(reason);
}

WEAK void sysutils_enter_critical_section(void)
{
  /* Disable interrupts */
  if (_critical.nesting == 0U)
  {
    _critical.primask = __get_PRIMASK();
    __disable_irq();
  }
  _critical.nesting++;
}

WEAK void sysutils_exit_critical_section(void)
{
  /* Restore interrupts */
  _critical.nesting--;
  if (_critical.nesting == 0U)
  {
    __set_PRIMASK(_critical.primask);
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: SysUtils -->
*//*--------------------------------------------------------------------------*/
