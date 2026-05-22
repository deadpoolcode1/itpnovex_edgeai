/**
 *******************************************************************************
 * @file    n6cam_rtos.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam compatibility layer for RTOS
 * @note    Configures malloc lock/unlock
 *******************************************************************************
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
 *******************************************************************************
 */
#include "cmsis_compiler.h"
#include "n6cam_rtos.h"
#include "tx_thread.h"
#include "tx_initialize.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup RTOS
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

static int32_t  _tx_libc_init = 0;
static TX_MUTEX _tx_mtx_libc;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void tx_libc_init(void);

extern uint32_t HAL_GetTick(void);
extern void HAL_Delay(uint32_t Delay);
extern void __malloc_lock(struct _reent *reent);
extern void __malloc_unlock(struct _reent *reent);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

bool rtos_is_initialized(void)
{
  return TX_THREAD_GET_SYSTEM_STATE() == TX_INITIALIZE_IS_FINISHED;
}

void rtos_raise_event(TX_EVENT_FLAGS_GROUP *event, uint32_t mask)
{
  int32_t status;

  /* Validate */
  if (!event)
  {
    Error_Handler();
  }
  if (mask == 0x00)
  {
    return;
  }

  /* Raise event */
  status = tx_event_flags_set(event, mask, TX_OR);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

void rtos_clear_event(TX_EVENT_FLAGS_GROUP *event, uint32_t mask)
{
  int32_t status;

  /* Validate */
  if (!event)
  {
    Error_Handler();
  }
  if (mask == 0x00)
  {
    return;
  }

  /* Raise event */
  mask   = ~mask;
  status = tx_event_flags_set(event, mask, TX_AND);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

uint32_t rtos_wait_any_event(TX_EVENT_FLAGS_GROUP *event, uint32_t mask, bool clear)
{
  uint32_t flags;
  int32_t  status;

  /* Validate */
  if (!event)
  {
    Error_Handler();
  }
  if (mask == 0x00)
  {
    return 0x00U;
  }

  /* Wait event */
  status = tx_event_flags_get(event, mask, clear? TX_OR_CLEAR : TX_OR, &flags, TX_WAIT_FOREVER);
  if ((status != TX_SUCCESS) || !(mask & flags))
  {
    Error_Handler();
  }
  return flags;
}

uint32_t rtos_wait_all_events(TX_EVENT_FLAGS_GROUP *event, uint32_t mask, bool clear)
{
  uint32_t flags;
  int32_t  status;

  /* Validate */
  if (!event)
  {
    Error_Handler();
  }
  if (mask == 0x00)
  {
    return 0x00U;
  }

  /* Wait event */
  status = tx_event_flags_get(event, mask, clear? TX_AND_CLEAR : TX_AND, &flags, TX_WAIT_FOREVER);
  if ((status != TX_SUCCESS) || !(mask & flags))
  {
    Error_Handler();
  }
  return flags;
}

void rtos_mutex_acquire(TX_MUTEX *mutex, bool acquire)
{
  /* Validate */
  if (IS_IRQ_MODE())
  {
    /* Cannot acquire mutex in IRQ! Assume no collision */
    return;
  }
  if (!mutex)
  {
    Error_Handler();
  }

  /* Capture */
  if (acquire)
  {
    TX_THREAD* thread = tx_thread_identify();
    if (tx_mutex_get(mutex, !thread ? TX_NO_WAIT : TX_WAIT_FOREVER) != TX_SUCCESS)
    {
      Error_Handler();
    }
  }
  else
  {
    int32_t status = tx_mutex_put(mutex);
    if ((status != TX_SUCCESS) && (status != TX_NOT_OWNED))
    {
      Error_Handler();
    }
  }
}

void rtos_semaphore_acquire(TX_SEMAPHORE *semaphore, bool acquire)
{
  /* Validate */
  if (IS_IRQ_MODE())
  {
    /* Cannot acquire mutex in IRQ! Assume no collision */
    return;
  }
  if (!semaphore)
  {
    Error_Handler();
  }

  /* Capture */
  if (acquire)
  {
    TX_THREAD* thread = tx_thread_identify();
    if (tx_semaphore_get(semaphore, !thread ? TX_NO_WAIT : TX_WAIT_FOREVER) != TX_SUCCESS)
    {
      Error_Handler();
    }
  }
  else
  {
    int32_t status = tx_semaphore_put(semaphore);
    if ((status != TX_SUCCESS) && (status != TX_NOT_OWNED))
    {
      Error_Handler();
    }
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initialize the mutex to handle LIBC critical functions.
 */
static void tx_libc_init(void)
{
  int32_t status;
  status = tx_mutex_create(&_tx_mtx_libc, "tx.mtx.libc", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
  _tx_libc_init = 1;
}

/**
 * @brief Provides a tick value in millisecond
 *
 * @return tick value
 */
uint32_t HAL_GetTick(void)
{
  return tx_time_get();
}

/**
 * @brief This function provides minimum delay (in milliseconds) based on variable incremented.
 *
 * @param Delay   Specifies the delay time length, in milliseconds
 */
void HAL_Delay(uint32_t Delay)
{
  if (IS_IRQ_MODE())
  {
    Error_Handler();
  }
  tx_thread_sleep(Delay);
}

/**
 * @brief This function configures the source of the time base.
 *
 * @note  ThreadX will be handling the SysTicks
 *
 * @param TickPriority  Tick interrupt priority.
 * @return HAL status
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  return HAL_OK;
}

/**
 * @brief Thread-safe memory allocation utility. Lock memory while allocating.
 * @param reent
 */
void __malloc_lock(struct _reent *reent)
{
  int32_t status;

  /* TX not initialized? skip (collisions impossible) */
  if (!rtos_is_initialized())
  {
    return;
  }
  /* Initialize mutex if not ready */
  if (!_tx_libc_init)
  {
    tx_libc_init();
  }
  /* If unable to initialize mutex OR in IRQ: critical failure */
  if ((_tx_libc_init != 1) || IS_IRQ_MODE())
  {
    Error_Handler();
  }
  /* Capture mutex */
  status = tx_mutex_get(&_tx_mtx_libc, TX_WAIT_FOREVER);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

/**
 * @brief Thread-safe memory allocation utility. Unlocks memory after allocating.
 * @param reent
 */
void __malloc_unlock(struct _reent *reent)
{
  int32_t status;

  /* TX not initialized? skip (collisions impossible) */
  if (!rtos_is_initialized())
  {
    return;
  }
  /* Initialize mutex if not ready */
  if (!_tx_libc_init)
  {
    tx_libc_init();
  }
  /* If unable to initialize mutex OR in IRQ: critical failure */
  if ((_tx_libc_init != 1) || IS_IRQ_MODE())
  {
    Error_Handler();
  }
  /* Release mutex */
  status = tx_mutex_put(&_tx_mtx_libc);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: RTOS -->
*//*--------------------------------------------------------------------------*/
