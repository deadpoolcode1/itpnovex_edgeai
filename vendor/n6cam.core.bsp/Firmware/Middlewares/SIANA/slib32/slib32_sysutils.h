/**
 *******************************************************************************
 * @file    slib32_sysutils.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for various system utilities
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
#ifndef _SLIB32_SYSUTILS_H_
#define _SLIB32_SYSUTILS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "slib32_stream.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup SysUtils
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#ifndef VERSION_STR_SIZE
  /** Version size: MM.mm.0123456789 */
  #define VERSION_STR_SIZE  (16U + 1U)
#endif /* VERSION_STR_SIZE */

#ifndef MCU_UID_STR_SIZE
  /** MCU UID size: 12bytes = 24hex digits */
  #define MCU_UID_STR_SIZE  (24U + 1U)
#endif /* MCU_UID_STR_SIZE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Macro to enter critical section */
#ifndef ENTER_CRITICAL_SECTION
  #define ENTER_CRITICAL_SECTION()  sysutils_enter_critical_section()
#endif /* ENTER_CRITICAL_SECTION */

/** Macro to exit critical section */
#ifndef EXIT_CRITICAL_SECTION
  #define EXIT_CRITICAL_SECTION()   sysutils_exit_critical_section()
#endif /* EXIT_CRITICAL_SECTION */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Gets the firmware version string
 *
 * @note  WEAK. Can be reimplemented if required
 *
 * @param buff String buffer
 * @param size String buffer size
 * @return Size of the string or error code (negative)
 */
int32_t sysutils_get_firmware_version_string(uint8_t *buff, size_t size);

/**
 * @brief Gets the hardware version string
 *
 * @note  WEAK. Can be reimplemented if required
 *
 * @param buff String buffer
 * @param size String buffer size
 * @return Size of the string or error code (negative)
 */
int32_t sysutils_get_hardware_version_string(uint8_t *buff, size_t size);

/**
 * @brief Gets the MCU uptime string
 *
 * @note  WEAK. Can be reimplemented if required
 *
 * @param buff String buffer
 * @param size String buffer size
 * @return Size of the string or error code (negative)
 */
int32_t sysutils_get_mcu_uptime_string(uint8_t *buff, size_t size);

/**
 * @brief Gets the MCU 96bit UID string
 *
 * @note  WEAK. Can be reimplemented if required
 *
 * @param buff String buffer
 * @param size String buffer size
 * @return Size of the string or error code (negative)
 */
int32_t sysutils_get_mcu_uid_string(uint8_t *buff, size_t size);

/**
 * @brief Reboot the MCU
 *
 * @note  WEAK. Can be reimplemented if required
 *
 * @param stream Stream instance
 * @param reason Reboot reason
 */
void sysutils_mcu_reboot(const t_stream *stream, int32_t reason);

/**
 * @brief Enter critical section
 *
 * @note  WEAK. Can be reimplemented if required
 */
void sysutils_enter_critical_section(void);

/**
 * @brief Exit critical section
 *
 * @note  WEAK. Can be reimplemented if required
 */
void sysutils_exit_critical_section(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: SysUtils -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_SYSUTILS_H_ */
