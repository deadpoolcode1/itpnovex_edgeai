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
#include "slib32_stat.h"
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
#define ENABLE_NN           1U
#define ENABLE_SCOM         0U
#define ENABLE_SENSOR_TOF   0U
#define ENABLE_SHELL        1U
#define ENABLE_TEST         1U
#define ENABLE_TRACER       1U
#ifdef DEBUG
  #define ENABLE_WATCHDOG   0U
#else
  #define ENABLE_WATCHDOG   1U
#endif /* DEBUG */

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
#elif defined(N6CAM_WIFI_MURATA)
  #include "version_murata.h"
#else
  #include "version_bsp.h"
#endif /* DEBUG */

/** Firmware version string */
extern char FW_VERSION[VERSION_STR_SIZE];

/*-->> STATISTICS <<-------------------------------------*/

/** Statistic entry IDs */
typedef enum
{
  STAT_FREQ_CAMERA = 0x00U,
  STAT_TIME_NN_TOTAL,
  STAT_TIME_NN_COPY,
  STAT_TIME_NN_MODEL,
  STAT_TIME_NN_PP,
  STAT_TIME_DISPLAY_TOTAL,
  STAT_TIME_DISPLAY_DRAW,
  STAT_TIME_ENC_H264,
  STAT_TIME_ENC_JPEG,
  /*-------*/
  STAT_ENTRY_NUM
} t_stat_id;

/*-->> TRACER <<-----------------------------------------*/

/** Tracer topics (Max = 32) */
typedef enum
{
  TRACE_SYSTEM = 0x00U,
  TRACE_CAMERA,
  TRACE_DISPLAY,
  TRACE_FX,
  TRACE_JPEG,
  TRACE_MODEM,
  TRACE_NN,
  TRACE_NX,
  TRACE_PP,
  TRACE_REGISTRY,
  TRACE_SCOM,
  TRACE_SENSOR_TOF,
  TRACE_SHELL,
  TRACE_SNAPSHOT,
  TRACE_TEST,
  TRACE_UX,
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
#define TRACE_DEFAULT_TOPIC     TRACE_TOPICS_ALL & ~BIT(TRACE_PP)

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
 * 3. WATCHDOG    < Optional >
 * 4. REGISTRY
 * 5. SENSORS
 *    CAMERA
 *    TOF         < Optional >
 * 6. PLATFORM
 *    FX                              Waits for SYSTEM_READY
 *    NX          < Optional >        Waits for SYSTEM_READY
 *    UX
 *----< SYSTEM_READY >--------
 */
#define TX_EVT_BSP_READY            BIT(0U)

#define TX_EVT_TEST_REQUIRE         (TX_EVT_BSP_READY)
#define TX_EVT_TEST_READY           (ENABLE_TEST? BIT(1U) : 0U)

#define TX_EVT_WATCHDOG_REQUIRE     (TX_EVT_BSP_READY | TX_EVT_TEST_READY)
#define TX_EVT_WATCHDOG_READY       (ENABLE_WATCHDOG? BIT(2U) : 0U)

#define TX_EVT_REGISTRY_REQUIRE     (TX_EVT_BSP_READY | TX_EVT_TEST_READY | TX_EVT_WATCHDOG_READY)
#define TX_EVT_REGISTRY_READY       BIT(3U)

#define TX_EVT_SENSOR_PREPARE       (TX_EVT_REGISTRY_READY)
#define TX_EVT_SENSOR_REQUIRE       BIT(4U)
#define TX_EVT_CAMERA_READY         BIT(5U)
#define TX_EVT_SENSOR_TOF_READY     (ENABLE_SENSOR_TOF? BIT(6U) : 0U)
#define TX_EVT_SENSOR_READY         (TX_EVT_CAMERA_READY | TX_EVT_SENSOR_TOF_READY)

#define TX_EVT_SYSTEM_READY         BIT(10U)

#define TX_EVT_FX_REQUIRE           (TX_EVT_SYSTEM_READY)
#define TX_EVT_FX_READY             BIT(11U)
#define TX_EVT_NX_REQUIRE           (TX_EVT_SYSTEM_READY)
#define TX_EVT_NX_READY             BIT(12U)
#define TX_EVT_UX_CDC_READY         BIT(13U)
#define TX_EVT_UX_UVC_READY         BIT(14U)

/* Application */
#define TX_EVT_NN_REQUIRE           (TX_EVT_SYSTEM_READY)
#define TX_EVT_NN_READY             (ENABLE_NN? BIT(15U) : 0U)

#define TX_EVT_JPEG_REQUIRE         (TX_EVT_SYSTEM_READY)
#define TX_EVT_JPEG_READY           BIT(16U)

#define TX_EVT_SNAPSHOT_REQUIRE     (TX_EVT_JPEG_READY)
#define TX_EVT_SNAPSHOT_READY       BIT(17U)

#define TX_EVT_DISPLAY_REQUIRE      (TX_EVT_UX_UVC_READY| TX_EVT_NN_READY | TX_EVT_SNAPSHOT_READY)
#define TX_EVT_DISPLAY_READY        BIT(18U)

#define TX_EVT_SCOM_REQUIRE         (TX_EVT_SYSTEM_READY)
#define TX_EVT_SCOM_READY           (ENABLE_SCOM? BIT(19U) : 0U)

#define TX_EVT_SHELL_REQUIRE        (TX_EVT_SYSTEM_READY | TX_EVT_UX_CDC_READY)
#define TX_EVT_SHELL_READY          (ENABLE_SHELL? BIT(20U) : 0U)

/*----------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif   /* _COMMON_H_ */
