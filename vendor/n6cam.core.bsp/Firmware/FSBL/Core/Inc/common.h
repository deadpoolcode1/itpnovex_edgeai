/**
 *******************************************************************************
 * @file    common.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   Common definitions for the firmware project
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 SIANA Systems. All rights reserved.
 *
 *******************************************************************************
 */
#ifndef _COMMON_H_
#define _COMMON_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "slib32_bitmask.h"
#include "slib32_core.h"
#include "slib32_error.h"
#include "slib32_sysutils.h"
#include "slib32_trace.h"
#include "stm32_hal.h"
#include "tx_api.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup Common
* @{
*//*--------------------------------------------------------------------------*/

/*-->> TURN ON/OFF FEATURES <<---------------------------*/

#define ENABLE_DCACHE       1U
#define ENABLE_TEST         1U
#define ENABLE_TRACER       1U

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
  #define VERSION_MAJOR     99U
  #define VERSION_MINOR     0U
  #define VERSION_BUILD     0U
#else
  #include "version.h"
#endif /* DEBUG */

/** Firmware version string */
extern char FW_VERSION[VERSION_STR_SIZE];

/*-->> TRACER <<-----------------------------------------*/

/** Tracer topics (Max = 32) */
typedef enum
{
  TRACE_SYSTEM = 0x00U,
  TRACE_TEST,
  /*-------*/
  TRACE_NUM
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
extern const char *TRACE_TOPIC_NAME[TRACE_NUM];

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

/*-->> SYSTEM Handlers <<-----------------------------------------------------*/

/**
 * @brief This function is executed in case of error occurrence.
 *
 * @note  Defined in main.c
 */
void Error_Handler(void);

/*-->> TASK SYNC <<-----------------------------------------------------------*/
/* Sequential init:
 * 1. BSP
 * 2. TEST        < Optional >
 *----< SYSTEM_READY >--------
 */
#define TX_EVT_BSP_READY            BIT(0U)

#define TX_EVT_TEST_REQUIRE         (TX_EVT_BSP_READY)
#define TX_EVT_TEST_READY           (ENABLE_TEST? BIT(1U) : 0U)

#define TX_EVT_SYSTEM_READY         BIT(2U)

/*----------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif   /* _COMMON_H_ */
