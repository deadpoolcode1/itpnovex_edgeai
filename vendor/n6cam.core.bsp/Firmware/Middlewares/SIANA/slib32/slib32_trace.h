/**
 *******************************************************************************
 * @file    slib32_trace.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the tracer service
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
#ifndef _SLIB32_TRACE_H_
#define _SLIB32_TRACE_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "slib32_stream.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Trace
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define TRACE_TOPIC_MAX     32U           /** Maximum number of topics */
#define TRACE_TOPICS_NONE   0x00U         /** Mask that disable all topics */
#define TRACE_TOPICS_ALL    0xFFFFFFFFU   /** Mask that enable all topics */

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

/** Tracer levels */
typedef enum
{
  TRACE_LEVEL_FATAL = 0x00U,
  TRACE_LEVEL_ERROR,
  TRACE_LEVEL_WARNING,
  TRACE_LEVEL_INFO,
  TRACE_LEVEL_DEBUG,
  TRACE_LEVEL_ALL,
} t_trace_level;

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
 * @brief Initializes the tracer
 *
 * @note This function is thread unsafe. Use with care
 *
 * @param stream Stream instance
 * @param line Output buffer
 * @param size Output buffer size
 * @param level Trace level
 * @param topic Trace topics mask
 * @return Error code
 */
int32_t trace_init(const t_stream *stream, uint8_t* line, size_t size, uint8_t level, uint32_t topic);

/**
 * @brief Initializes the trace rtos support
 * @param lock_fn Lock handler function
 * @param lock Lock object, such as a mutex or semaphore
 * @return Error code
 */
int32_t trace_init_rtos(t_rtos_lock_fn lock_fn, void *lock);

/**
 * @brief Enables the tracer
 * @param enable True to enable the tracer, false to disable it
 */
void trace_enable(bool enable);

/**
 * @brief Set the trace level
 * @param level Trace level
 */
void trace_set_level(uint8_t level);

/**
 * @brief Set the trace topic mask
 * @param mask Trace topics mask
 */
void trace_set_topic(uint32_t mask);

/**
 * @brief Set the trace topic
 * @param topic Trace topic
 * @param enable true to enable, false to disable
 * @return Error code
 */
int32_t trace_topic_enable(uint8_t topic, bool enable);

/**
 * @brief Trace a message
 * @param level Trace level
 * @param topic Trace topic
 * @param name Topic name
 * @param file File name
 * @param line Line number
 * @param fmt Format string
 * @param ... Variable arguments
 */
void trace(uint8_t level, uint8_t topic, const char *name, const char *file, uint32_t line, const char *fmt, ...);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Trace -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_TRACE_H_ */
