/**
 *******************************************************************************
 * @file    slib32_stream.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements stream manipulation utilities
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
#include <stdio.h>
#include <string.h>

#include "slib32_stream.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Stream
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

/** Stream print timeout in [ms] */
#ifndef SLIB32_STREAM_PRINT_TIMEOUT
  #define SLIB32_STREAM_PRINT_TIMEOUT  1000U
#endif /* SLIB32_STREAM_PRINT_TIMEOUT */

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _stream_acquire(const t_stream *stream, void* lock, bool acquire);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t stream_init(t_stream *stream, t_stream_read_fn read, t_stream_write_fn write)
{
  /* Validate */
  if (!stream || !read || !write)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Initialize stream */
  memset(stream, 0U, sizeof(t_stream));
  stream->init        = true;
  stream->read        = read;
  stream->write       = write;
  stream->lock_fn     = NULL;
  stream->lock_read   = NULL;
  stream->lock_write  = NULL;
  return SLIB32_OK;
}

int32_t stream_init_rtos(t_stream *stream, t_rtos_lock_fn lock_fn, void *lock_read, void *lock_write)
{
  /* Validate */
  if (!stream || !lock_fn || !lock_read || !lock_write)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!stream->init)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Initialize RTOS support */
  stream->lock_fn    = lock_fn;
  stream->lock_read  = lock_read;
  stream->lock_write = lock_write;
  return SLIB32_OK;
}

int32_t stream_read(const t_stream *stream, uint8_t *buff, size_t size, uint32_t timeout)
{
  /* Validate */
  if (!stream || !buff)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!stream->init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!stream->read || (size == 0))
  {
    return 0;
  }

  /* Read data */
  _stream_acquire(stream, stream->lock_read, true);
  int32_t status = stream->read(buff, size, timeout);
  _stream_acquire(stream, stream->lock_read, false);
  return status;
}

int32_t stream_write(const t_stream *stream, const uint8_t *buff, size_t size, uint32_t timeout)
{
  /* Validate */
  if (!stream || !buff)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!stream->init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!stream->write || (size == 0))
  {
    return 0;
  }

  /* Write data */
  _stream_acquire(stream, stream->lock_write, true);
  int32_t status = stream->write(buff, size, timeout);
  _stream_acquire(stream, stream->lock_write, false);
  return status;
}

int32_t stream_printf(const t_stream *stream, uint8_t *buff, size_t size, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int32_t status = stream_vprintf(stream, buff, size, fmt, args);
  va_end(args);
  return status;
}

int32_t stream_vprintf(const t_stream *stream, uint8_t *buff, size_t size, const char *fmt, va_list args)
{
  /* Validate */
  if (!stream || !buff)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!stream->init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!stream->write || (size == 0))
  {
    return 0;
  }

  /* Print data */
  _stream_acquire(stream, stream->lock_write, true);
  vsnprintf((char*)buff, size, fmt, args);
  int32_t printed = (int32_t)strlen((char*)buff);
  int32_t status  = stream->write(buff, printed, SLIB32_STREAM_PRINT_TIMEOUT);
  _stream_acquire(stream, stream->lock_write, false);
  return status;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Acquire/release a stream
 * @param stream  Stream instance
 * @param lock    Lock object
 * @param acquire True to acquire the lock, false to release it
 */
static void _stream_acquire(const t_stream *stream, void* lock, bool acquire)
{
  if (stream->lock_fn)
  {
    stream->lock_fn(lock, acquire);
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Stream -->
*//*--------------------------------------------------------------------------*/
