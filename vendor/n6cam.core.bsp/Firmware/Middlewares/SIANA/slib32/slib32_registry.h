/**
 *******************************************************************************
 * @file    slib32_registry.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the system registry module
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
#ifndef _SLIB32_REGISTRY_H_
#define _SLIB32_REGISTRY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "registry.h"         /* User-defined registry data */
#include "slib32_core.h"
#include "slib32_error.h"
#include "slib32_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Registry
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Registry read function
 * @param buff Destination buffer
 * @param size Number of bytes to read
 * @return Number of bytes read or error code (negative)
 */
typedef int32_t (*t_registry_read_fn)(uint8_t *buff, size_t size);

/**
 * @brief Registry write function
 * @param buff Source buffer
 * @param size Number of bytes to write
 * @return Number of bytes written or error code (negative)
 */
typedef int32_t (*t_registry_write_fn)(const uint8_t *buff, size_t size);

/**
 * @brief Registry CRC function
 * @param buff Buffer
 * @param size Buffer size
 * @return CRC32 value
 */
typedef uint32_t (*t_registry_crc32_fn)(const uint8_t *buff, size_t size);

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
 * @brief Initializes the registry
 *
 * @note This function is thread unsafe. Use with care
 *
 * @param read Read function
 * @param write Write function
 * @param crc32 CRC32 function
 * @return Error code
 */
int32_t registry_init(t_registry_read_fn read, t_registry_write_fn write, t_registry_crc32_fn crc32);

/**
 * @brief Initializes the registry RTOS support
 * @param lock_fn Lock handler function
 * @param lock Lock object, such as a mutex or semaphore
 * @return Error code
 */
int32_t registry_init_rtos(t_rtos_lock_fn lock_fn, void *lock);

/**
 * @brief Starts the registry and validates the data
 *
 * @note Call this function before using the registry
 *
 * @param force Force the registry start (even if already started)
 * @return Error code
 */
int32_t registry_start(bool force);

/**
 * @brief Acquire registry data
 *
 * @note  Call this function to access/modify registry data
 *        Use registry_release() when done
 *
 * @return Pointer to the registry data or NULL if not started
 */
t_registry_data *registry_acquire(void);

/**
 * @brief Release registry data
 *
 * @note  Call this function after registry_acquire()
 *
 * @return Error code
 */
void registry_release(void);

/**
 * @brief Save registry to storage
 * @return Error code
 */
int32_t registry_save(void);

/**
 * @brief Get default registry data
 * @return Registry defaults
 */
const t_registry_data *registry_get_defaults(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Registry -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_REGISTRY_H_ */
