/**
 *******************************************************************************
 * @file    common.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Common definitions for the firmware project
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
#ifndef _COMMON_H_
#define _COMMON_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "slib32_bitmask.h"
#include "slib32_sysutils.h"
#include "slib32_trace.h"
#include "stm32_hal.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Common
* @{
*//*--------------------------------------------------------------------------*/

/*-->> TURN ON/OFF FEATURES <<---------------------------*/

#define ENABLE_TRACER           1U

/*-->> VERSION <<----------------------------------------*/
/* Version format: MM.mm.bb
 *   MM: Major
 *   mm: Minor
 *   bb: Build/commit
 * Example: 1.0.5439210
 *-----------------------
 * Major:
 *    0...80 : Production
 *   80...99 : Reserved
 *   99      : Engineering
 */
#ifdef DEBUG
  #define VERSION_MAJOR         99U
  #define VERSION_MINOR         0U
  #define VERSION_BUILD         0U
#else
  #include "version.h"
#endif /* DEBUG */

/** Firmware version string */
extern char FW_VERSION[VERSION_STR_SIZE];

/*-->> TRACER <<-----------------------------------------*/

/** Tracer topics (Max = 32) */
typedef enum
{
  TOPIC_SYSTEM = 0x00U,
  /*-------*/
  TOPIC_NUM
} t_trace_topic;

/** Tracer default level */
#ifdef DEBUG
  #define TRACE_DEFAULT_LEVEL   TRACE_LEVEL_DEBUG
#else
  #define TRACE_DEFAULT_LEVEL   TRACE_LEVEL_INFO
#endif

/** Tracer default topic */
#define TRACE_DEFAULT_TOPIC     TRACE_TOPICS_ALL

/** Tracer topic names */
extern const char *TRACE_TOPIC_NAME[TOPIC_NUM];

/* Tracer macros */
#if ENABLE_TRACER == 1U
  #define LFATAL(topic, ...)    trace(TRACE_LEVEL_FATAL  , topic, TRACE_TOPIC_NAME[topic], __FILE__, __LINE__, __VA_ARGS__)
  #define LERROR(topic, ...)    trace(TRACE_LEVEL_ERROR  , topic, TRACE_TOPIC_NAME[topic], __FILE__, __LINE__, __VA_ARGS__)
  #define LWARNING(topic, ...)  trace(TRACE_LEVEL_WARNING, topic, TRACE_TOPIC_NAME[topic], __FILE__, __LINE__, __VA_ARGS__)
  #define LINFO(topic, ...)     trace(TRACE_LEVEL_INFO   , topic, TRACE_TOPIC_NAME[topic], __FILE__, __LINE__, __VA_ARGS__)
  #define LDEBUG(topic, ...)    trace(TRACE_LEVEL_DEBUG  , topic, TRACE_TOPIC_NAME[topic], __FILE__, __LINE__, __VA_ARGS__)
#else
  #define LFATAL(...)           ( (void)0 )
  #define LERROR(...)           ( (void)0 )
  #define LWARNING(...)         ( (void)0 )
  #define LINFO(...)            ( (void)0 )
  #define LDEBUG(...)           ( (void)0 )
#endif /* ENABLE_TRACER */

/*-->> SYSTEM Handlers <<--------------------------------*/

/**
 * @brief This function is called when an unexpected error occurs
 *
 * @note  Implement this in main.c
 */
void Error_Handler(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Common -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _COMMON_H_ */
