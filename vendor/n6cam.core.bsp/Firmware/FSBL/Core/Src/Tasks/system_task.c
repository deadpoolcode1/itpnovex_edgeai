/**
 ******************************************************************************
 * @file    system_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the system module.
 *          This module is responsible for:
 *          - Initialize platform
 *          - Blinking status LED
 *          - Generate status message
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2024 SIANA Systems</center></h2>
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
 ******************************************************************************
 */
#include "n6cam_core.h"
#include "n6cam_uart.h"
#include "slib32_tick.h"
#include "system_task.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup System
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

/* System task tunables
 * TODO: Optimize stack size
 */
#define SYSTEM_TASK_STACK_SIZE  (2U * 1024U)
#define SYSTEM_TASK_PRIO        APP_PRIO_BACKGROUND
#define SYSTEM_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/* Internals */
#define SYSTEM_PERIOD           50U               /*!< System task period in [ms] */
#define SYSTEM_STATUS_PERIOD    1000U             /*!< Blink status LED@0.5Hz */
#define SYSTEM_RESET_PERIOD     5000U             /*!< Print reset FPS@0.2Hz */

#define TRACE_UART              UART_STLINK       /*!< Trace UART */
#define TRACE_OUT_SIZE          1024U             /*!< Trace output buffer size */

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

/** System task handler */
typedef struct
{
  TX_THREAD thread;
  uint8_t   stack[SYSTEM_TASK_STACK_SIZE];
} t_system_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_system_task      _system_task = { 0 };

static TX_MUTEX           _trace_mtx;
static uint8_t            _trace_out[TRACE_OUT_SIZE];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _system_task_run(uint32_t args);
static void _system_task_init(void);

static void _system_config_trace(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t system_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_system_task.thread, "tx.task.system",
    _system_task_run, 0,
    _system_task.stack, SYSTEM_TASK_STACK_SIZE,
    SYSTEM_TASK_PRIO, SYSTEM_TASK_PRIO,
    SYSTEM_TASK_TIME_SLICE, TX_AUTO_START
  );
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief System task entry point.
 * @param args Task arguments
 */
static void _system_task_run(uint32_t args)
{
  /* Periodic tasks */
  uint32_t led1_start = 0U;
  uint32_t info_start = 0U;

  UNUSED(args);

  /* Initialize */
  _system_task_init();

  /* System task */
  while (1)
  {
    /* Status LED */
    if (tick_is_expired(led1_start, SYSTEM_STATUS_PERIOD))
    {
      led1_start = tx_time_get();
      bsp_led_toggle(LED_USER1);
    }

    /* Status traces */
    if (tick_is_expired(info_start, SYSTEM_RESET_PERIOD))
    {
      info_start = tx_time_get();
      LINFO(TRACE_SYSTEM, "Please restart the board to run the application.");
    }

    /* Wait a while */
    tx_thread_sleep(SYSTEM_PERIOD);
  }
}

/**
 * @brief System task initialization.
 */
static void _system_task_init(void)
{
  /*-->> DEPENDENCIES <<--*/
  /* No dependencies */

  /*-->> INITIALIZE <<--*/
  /* Configure tracing */
  _system_config_trace();

  /* At this point, BSP is ready */
  task_raise_event(TX_EVT_BSP_READY);

  /* Wait for TEST to run */
#if ENABLE_TEST == 1U
  task_wait_event(TX_EVT_TEST_READY);
#endif /* ENABLE_TEST */

  /*-->> READY <<--*/
  LINFO(TRACE_SYSTEM, "** N6Cam.FSBL - v%s **", FW_VERSION);
  task_raise_event(TX_EVT_SYSTEM_READY);
}

/**
 * @brief Configure tracing utility.
 */
static void _system_config_trace(void)
{
  int32_t status;

  /* Initialize UART */
  status = bsp_uart_init(TRACE_UART, 115200, false);
  if (status != BSP_OK)
  {
    Error_Handler();
  }

  /* Create tracer mutex */
  status = tx_mutex_create(&_trace_mtx, "tx.mtx.trace", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize Tracer */
  status = trace_init(bsp_uart_get_stream(TRACE_UART), _trace_out, TRACE_OUT_SIZE, TRACE_DEFAULT_LEVEL, TRACE_DEFAULT_TOPIC);
  if (status != SLIB32_OK)
  {
    Error_Handler();
  }
  status = trace_init_rtos((t_rtos_lock_fn)rtos_mutex_acquire, (void*)&_trace_mtx);
  if (status != SLIB32_OK)
  {
    Error_Handler();
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: System -->
*//*--------------------------------------------------------------------------*/
