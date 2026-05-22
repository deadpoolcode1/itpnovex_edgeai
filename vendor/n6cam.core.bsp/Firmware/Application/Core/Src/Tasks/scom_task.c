/**
 ******************************************************************************
 * @file    scom_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the Serial COMmunications module.
 *          This module is responsible for:
 *          - Initializing peripheral and buffers
 *          - Handling communications pipeline
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
#include "scom_task.h"

#if ENABLE_SCOM == 1U
#include "n6cam_uart.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup SCOM
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

/* SCOM task tunables
 * TODO: Optimize stack size
 */
#define SCOM_TASK_STACK_SIZE  (2U * 1024U)
#define SCOM_TASK_PRIO        APP_PRIO_USER_INTERFACE
#define SCOM_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/* SCOM stream */
#define SCOM_UART             UART_APP
#define SCOM_UART_TIMEOUT     200U

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

/** SCOM task handler */
typedef struct
{
  TX_THREAD thread;
  uint8_t   stack[SCOM_TASK_STACK_SIZE];
} t_scom_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_scom_task  _scom_task = { 0 };
static t_scom_msg   _scom_msg;
static t_stream     *_scom_stream;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _scom_task_run(uint32_t args);
static void _scom_task_init(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t scom_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_scom_task.thread, "tx.task.scom",
    _scom_task_run, 0,
    _scom_task.stack, SCOM_TASK_STACK_SIZE,
    SCOM_TASK_PRIO, SCOM_TASK_PRIO,
    SCOM_TASK_TIME_SLICE, TX_AUTO_START
  );
}

WEAK bool scom_on_recv(t_scom_msg *msg)
{
  /* Default: Loopback */
  UNUSED(msg);
  return true;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief SCOM task entry point.
 * @param args Task arguments
 */
static void _scom_task_run(uint32_t args)
{
  int32_t status;

  UNUSED(args);

  /* Initialize task */
  _scom_task_init();

  /* SCOM command task */
  while (1)
  {
    /* RX message */
    status = stream_read(_scom_stream, _scom_msg.buff, SCOM_MSG_MAX_SIZE, SCOM_UART_TIMEOUT);
    if (status < 0)
    {
      LDEBUG(TRACE_SCOM, "RX Fail/Timeout. Restart...");
      continue;
    }
    _scom_msg.size = (size_t)status;
    if (_scom_msg.size == 0)
    {
      LDEBUG(TRACE_SCOM, "RX Empty. Restart...");
      continue;
    }

    /* Handle message */
    if (!scom_on_recv(&_scom_msg))
    {
      continue;
    }

    /* TX response */
    status = stream_write(_scom_stream, _scom_msg.buff, _scom_msg.size, SCOM_UART_TIMEOUT);
    if (status < SLIB32_OK)
    {
      LDEBUG(TRACE_SCOM, "TX Fail. Skip...");
    }
  }
}

/**
 * @brief SCOM task initialization.
 */
void _scom_task_init(void)
{
  int32_t status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_SCOM_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Initialize UART */
  status = bsp_uart_init(SCOM_UART, 115200, false);
  if (status != BSP_OK)
  {
    LERROR(TRACE_SCOM, "UART init failed");
    Error_Handler();
  }

  /* Set stream */
  _scom_stream = bsp_uart_get_stream(SCOM_UART);

  /*-->> READY <<--*/
  LINFO(TRACE_SCOM, "Task started");
  task_raise_event(TX_EVT_SCOM_READY);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: SCOM -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_SCOM */
