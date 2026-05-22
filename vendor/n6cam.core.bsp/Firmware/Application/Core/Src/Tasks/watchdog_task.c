/**
 ******************************************************************************
 * @file    watchdog_task.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the watchdog module.
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
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
#include "watchdog_task.h"

#if ENABLE_WATCHDOG == 1U
#include "n6cam_watchdog.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Watchdog
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

/* Watchdog task tunables
 * TODO: Optimize stack size
 */
#define WATCHDOG_TASK_STACK_SIZE  512U
#define WATCHDOG_TASK_PRIO        APP_PRIO_CRITICAL
#define WATCHDOG_TASK_TIME_SLICE  TX_NO_TIME_SLICE

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

/** Watchdog task handler */
typedef struct
{
  TX_THREAD thread;
  uint8_t   stack[WATCHDOG_TASK_STACK_SIZE];
} t_watchdog_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/* Task handler */
static t_watchdog_task _watchdog_task = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _watchdog_task_run(uint32_t args);
static void _watchdog_task_init(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t watchdog_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_watchdog_task.thread, "tx.task.watchdog",
    _watchdog_task_run, 0,
    _watchdog_task.stack, WATCHDOG_TASK_STACK_SIZE,
    WATCHDOG_TASK_PRIO, WATCHDOG_TASK_PRIO,
    WATCHDOG_TASK_TIME_SLICE, TX_AUTO_START
  );
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Watchdog task entry point.
 * @param args Task arguments
 */
static void _watchdog_task_run(uint32_t args)
{
  UNUSED(args);

  /* Initialize */
  _watchdog_task_init();

  /* Watchdog task */
  while (1)
  {
    tx_thread_sleep(bsp_watchdog_window());
    bsp_watchdog_refresh();
  }
}

/**
 * @brief Watchdog task initialization.
 */
static void _watchdog_task_init(void)
{
  int32_t status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_WATCHDOG_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Start watchdog */
  status = bsp_watchdog_init();
  if (status != BSP_OK)
  {
    Error_Handler();
  }

  /*-->> READY <<--*/
  task_raise_event(TX_EVT_WATCHDOG_READY);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Watchdog -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_WATCHDOG */
