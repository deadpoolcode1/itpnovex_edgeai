/**
 *******************************************************************************
 * @file    slib32_stat.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for statistics utilities
 *
 * Statistics:
 * - Frequency: Number of counts over a period of time (normalized).
 * - Time: Time between start/stop (using Cumulative Average or Exponential Weighted Average).
 *
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
#ifndef _SLIB32_STAT_H_
#define _SLIB32_STAT_H_
#ifdef  __cplusplus
extern "C"
{
#endif

#include <stddef.h>

#include "slib32_core.h"
#include "slib32_error.h"
#include "slib32_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Stat
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

/** Statistics entry */
typedef struct
{
  uint32_t  accum;    /*!< Accumulator */
  uint32_t  count;    /*!< Number of samples */
  uint32_t  last;     /*!< Last value */
  uint32_t  start;    /*!< Start tick (used for freq/time only) */
  float     value;    /*!< Normalized value */
} t_stat_entry;

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
 * @brief Initializes the statistics module
 *
 * @note This function is thread unsafe. Use with care
 *
 * @param entry Array of statistics entries
 * @param size Number of entries
 * @return Error code
 */
int32_t stat_init(t_stat_entry *entry, size_t size);

/**
 * @brief Initializes the statistics RTOS support
 * @param lock_fn Lock handler function
 * @param lock Lock object, such as a mutex or semaphore
 * @return Error code
 */
int32_t stat_init_rtos(t_rtos_lock_fn lock_fn, void *lock);

/**
 * @brief Acquire/release the statistics
 * @param acquire True to acquire, false to release
 * @return Error code
 */
int32_t stat_acquire(bool acquire);

/**
 * @brief Enable/disable EWA
 *
 * @note  This affects all measurements but frequency
 *
 * @param enable True to enable, false to disable
 * @return Error code
 */
int32_t stat_ewa_enable(bool enable);

/**
 * @brief Set the EWA alpha
 *
 * @note  This affects all measurements but frequency
 *
 * @param ewa EWA alpha
 * @return Error code
 */
int32_t stat_ewa_alpha(float ewa);

/**
 * @brief Get a statistics entry
 * @param idx Entry index
 * @param entry Receiving entry
 * @return Error code
 */
int32_t stat_entry(size_t idx, t_stat_entry *entry);

/**
 * @brief Update a statistics entry
 * @param idx Entry index
 * @param value New value
 * @return Error code
 */
int32_t stat_update(size_t idx, uint32_t value);

/**
 * @brief Reset a statistics entry
 * @param idx Entry index
 * @return Error code
 */
int32_t stat_reset(size_t idx);

/**
 * @brief Get the statistics value
 * @param idx Entry index
 * @return Statistics value. NaN if error
 */
float stat_value(size_t idx);

/**
 * @brief Set the ticks per second
 *
 * @note  This affects all frequency measurements
 *
 * @param tps Ticks per second
 * @return Error code
 */
int32_t stat_freq_set_tps(uint32_t tps);

/**
 * @brief Count an event
 * @param idx Entry index
 * @param count Number of events
 * @return Error code
 */
int32_t stat_freq_count(size_t idx, uint32_t count);

/**
 * @brief Update the frequency statistics
 * @param idx Entry index
 * @return Error code
 */
int32_t stat_freq_update(size_t idx);

/**
 * @brief Start a time measurement
 *
 * @note  This function must be called before stat_time_stop()
 *
 * @param idx Entry index
 * @return Error code
 */
int32_t stat_time_start(size_t idx);

/**
 * @brief Stop a time measurement (and update the statistics)
 *
 * @note  This function must be called after stat_time_start()
 *
 * @param idx Entry index
 * @return Error code
 */
int32_t stat_time_stop(size_t idx);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Stat -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_STAT_H_ */
