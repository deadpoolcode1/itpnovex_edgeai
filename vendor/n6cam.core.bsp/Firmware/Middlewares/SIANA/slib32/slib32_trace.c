/**
 *******************************************************************************
 * @file    slib32_trace.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements the tracer service
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
#include <string.h>

#include "slib32_bitmask.h"
#include "slib32_trace.h"
#include "stm32_hal.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Trace
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

/** Trace End-Of-Line */
#ifndef SLIB32_TRACE_EOL
  #define SLIB32_TRACE_EOL        "\n"
#endif /* SLIB32_TRACE_EOL */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Helper macro for trace printf */
#define TRACE_PRINTF(fmt, ...) \
  stream_printf(_trace.stream, _trace.line, _trace.size, fmt, ##__VA_ARGS__)

/** Helper macro for trace vprintf */
#define TRACE_VPRINTF(fmt, args) \
  stream_vprintf(_trace.stream, _trace.line, _trace.size, fmt, args)

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

/** Tracer instance */
typedef struct
{
  bool            init;       /*!< Initialization flag */
  bool            enable;     /*!< Trace enable */
  uint8_t         level;      /*!< Trace level */
  uint32_t        topic;      /*!< Topic mask */
  /* Streaming */
  size_t          size;       /*!< Line size */
  uint8_t         *line;      /*!< Line buffer */
  const t_stream  *stream;    /*!< Output stream */
  /* RTOS support */
  t_rtos_lock_fn  lock_fn;    /*!< Lock handler function */
  void            *lock;      /*!< Lock object */
} t_trace;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_trace    _trace = { 0 };
static const char *_trace_level_str[TRACE_LEVEL_ALL] = {
  "FATAL",
  "ERROR",
  "WARNING",
  "INFO",
  "DEBUG",
};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _trace_acquire(bool acquire);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t trace_init(const t_stream *stream, uint8_t* line, size_t size, uint8_t level, uint32_t topic)
{
  /* Validate */
  if (!stream || !line)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Initialize trace */
  _trace.init   = true;
  _trace.enable = (size != 0);
  _trace.stream = stream;
  _trace.line   = line;
  _trace.size   = size;
  _trace.lock_fn= NULL;
  _trace.lock   = NULL;
  trace_set_level(level);
  trace_set_topic(topic);
  return SLIB32_OK;
}

int32_t trace_init_rtos(t_rtos_lock_fn lock_fn, void *lock)
{
  /* Validate */
  if (!_trace.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!lock_fn || !lock)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Initialize RTOS support */
  _trace.lock_fn = lock_fn;
  _trace.lock    = lock;
  return SLIB32_OK;
}

void trace_enable(bool enable)
{
  _trace.enable = enable;
}

void trace_set_level(uint8_t level)
{
  _trace.level = level;
}

void trace_set_topic(uint32_t mask)
{
  _trace.topic = mask;
}

int32_t trace_topic_enable(uint8_t topic, bool enable)
{
  /* Validate */
  if (topic >= TRACE_TOPIC_MAX)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  /* Update */
  bitmask_update(&_trace.topic, topic, enable);
  return SLIB32_OK;
}

void trace(uint8_t level, uint8_t topic, const char *name, const char *file, uint32_t line, const char *fmt, ...)
{
  /* Validate */
  if (!_trace.init || !_trace.enable || (level > _trace.level) || !bitmask_get(&_trace.topic, topic))
  {
    return;
  }

  /* Acquire tracer */
  _trace_acquire(true);

  /* Print trace */
  TRACE_PRINTF("%10" PRIu32 " : ", HAL_GetTick());
  TRACE_PRINTF("%-7s : ", _trace_level_str[level]);
  if (name)
  {
    TRACE_PRINTF("%s : ", name);
  }
  if (file && (level <= TRACE_LEVEL_WARNING))
  {
    char *pf = strrchr(file, '/');
    while (pf && (pf != file))
    {
      if ((*pf == '/') || (*pf == '\\'))
      {
        pf++;
        break;
      }
      pf--;
    }
    TRACE_PRINTF("%s:%" PRIu32 " : ", pf, line);
  }
  va_list args;
  va_start(args, fmt);
  TRACE_VPRINTF(fmt, args);
  va_end(args);
  TRACE_PRINTF(SLIB32_TRACE_EOL);

  /* Release tracer */
  _trace_acquire(false);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Acquire/release the trace
 * @param acquire True to acquire, false to release
 */
static void _trace_acquire(bool acquire)
{
  if (_trace.lock_fn)
  {
    _trace.lock_fn(_trace.lock, acquire);
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Trace -->
*//*--------------------------------------------------------------------------*/
