/**
 *******************************************************************************
 * @file    slib32_stream.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for stream manipulation utilities
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
#ifndef _SLIB32_STREAM_H_
#define _SLIB32_STREAM_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>

#include "slib32_core.h"
#include "slib32_error.h"
#include "slib32_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Stream
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
 * @brief Stream read function
 * @param buff Destination buffer
 * @param size Number of bytes to read
 * @param timeout Timeout in milliseconds
 * @return Number of bytes read or error code (negative)
 */
typedef int32_t (*t_stream_read_fn)(uint8_t *buff, size_t size, uint32_t timeout);

/**
 * @brief Stream write function
 * @param buff Source buffer
 * @param size Number of bytes to write
 * @param timeout Timeout in milliseconds
 * @return Number of bytes written or error code (negative)
 */
typedef int32_t (*t_stream_write_fn)(const uint8_t *buff, size_t size, uint32_t timeout);

/** Stream instance */
typedef struct
{
  bool              init;           /*!< Initialization flag */
  t_stream_read_fn  read;           /*!< Read function */
  t_stream_write_fn write;          /*!< Write function */
  /* RTOS support */
  t_rtos_lock_fn    lock_fn;        /*!< Lock handler function */
  void              *lock_read;     /*!< Read lock object */
  void              *lock_write;    /*!< Write lock object */
} t_stream;

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
 * @brief Initializes a stream instance
 *
 * @note This function is thread unsafe. Use with care
 *
 * @param stream Stream instance
 * @param read Read function
 * @param write Write function
 * @return Error code
 */
int32_t stream_init(t_stream *stream, t_stream_read_fn read, t_stream_write_fn write);

/**
 * @brief Initializes the stream RTOS support
 * @param stream Stream instance
 * @param lock_fn Lock handler function
 * @param lock_read Read lock object, such as a mutex or semaphore
 * @param lock_write Write lock object, such as a mutex or semaphore
 * @return Error code
 */
int32_t stream_init_rtos(t_stream *stream, t_rtos_lock_fn lock_fn, void *lock_read, void *lock_write);

/**
 * @brief Reads a stream
 * @param stream Stream instance
 * @param buff Read buffer
 * @param size Read buffer maximum capacity
 * @param timeout Timeout in milliseconds
 * @return Number of bytes read or error code (negative)
 */
int32_t stream_read(const t_stream *stream, uint8_t *buff, size_t size, uint32_t timeout);

/**
 * @brief Writes a stream
 * @param stream Stream instance
 * @param buff Write buffer
 * @param size Write buffer maximum capacity
 * @param timeout Timeout in milliseconds
 * @return Number of bytes written or error code (negative)
 */
int32_t stream_write(const t_stream *stream, const uint8_t *buff, size_t size, uint32_t timeout);

/**
 * @brief Prints a formatted string to a stream
 * @param stream Stream instance
 * @param buff Write buffer
 * @param size Write buffer maximum capacity
 * @param fmt String format
 * @param ... Variable arguments
 * @return Number of bytes written or error code (negative)
 */
int32_t stream_printf(const t_stream *stream, uint8_t *buff, size_t size, const char *fmt, ...);

/**
 * @brief Prints a formatted string to a stream
 * @param stream Stream instance
 * @param buff Write buffer
 * @param size Write buffer maximum capacity
 * @param fmt String format
 * @param args Variable arguments
 * @return Number of bytes written or error code (negative)
 */
int32_t stream_vprintf(const t_stream *stream, uint8_t *buff, size_t size, const char *fmt, va_list args);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Stream -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_STREAM_H_ */
