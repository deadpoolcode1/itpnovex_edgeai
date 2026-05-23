/**
 ******************************************************************************
 * @file    registry_task.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the registry module.
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
#include "flash.h"
#include "n6cam_crc.h"
#include "registry_task.h"
#include "registry.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Registry
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

/* Registry task tunables
 * TODO: Optimize stack size
 */
#define REGISTRY_TASK_STACK_SIZE  (1U * 1024U)
#define REGISTRY_TASK_PRIO        APP_PRIO_CRITICAL
#define REGISTRY_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* Registry events */
#define REGISTRY_EVT_SAVE       BIT(0U)

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

/** Registry task handler */
typedef struct
{
  TX_EVENT_FLAGS_GROUP  evt;
  TX_MUTEX              mtx;
  TX_THREAD             thread;
  uint8_t               stack[REGISTRY_TASK_STACK_SIZE];
} t_registry_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_registry_task  _registry_task = { 0 };   /*!< Task instance */
extern uint32_t         _sregistry;               /*!< Registry address (from linker) */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void     _registry_task_run(uint32_t args);
static void     _registry_task_init(void);

static int32_t  _registry_read(uint8_t *buff, size_t size);
static int32_t  _registry_write(const uint8_t *buff, size_t size);
static uint32_t _registry_crc32(const uint8_t *buff, size_t size);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t registry_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_registry_task.thread, "tx.task.registry",
    _registry_task_run, 0,
    _registry_task.stack, REGISTRY_TASK_STACK_SIZE,
    REGISTRY_TASK_PRIO, REGISTRY_TASK_PRIO,
    REGISTRY_TASK_TIME_SLICE, TX_AUTO_START
  );
}

void registry_request_save(void)
{
  rtos_raise_event(&_registry_task.evt, REGISTRY_EVT_SAVE);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Registry task entry point.
 * @param args Task arguments
 */
static void _registry_task_run(uint32_t args)
{
  UNUSED(args);

  /* Initialize */
  _registry_task_init();

  /* Registry task */
  while (1)
  {
    /* Wait until save request */
    rtos_wait_any_event(&_registry_task.evt, REGISTRY_EVT_SAVE, true);

    /* Save registry
     * TODO: Add custom logic to define if it is necessary to save...
     */
    if (registry_save() != SLIB32_OK)
    {
      LERROR(TRACE_REGISTRY, "Registry save failed");
      Error_Handler();
    }
  }
}

/**
 * @brief Registry task initialization.
 */
static void _registry_task_init(void)
{
  int32_t status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_REGISTRY_REQUIRE);

  /*-->> INITIALIZE <<--*/
  status = tx_event_flags_create(&_registry_task.evt, "tx.evt.registry");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
  status = tx_mutex_create(&_registry_task.mtx, "tx.mtx.registry", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize registry */
  status = registry_init(_registry_read, _registry_write, _registry_crc32);
  if (status != SLIB32_OK)
  {
    LERROR(TRACE_REGISTRY, "Registry init failed");
    Error_Handler();
  }
  status = registry_init_rtos((t_rtos_lock_fn)rtos_mutex_acquire, (void*)&_registry_task.mtx);
  if (status != SLIB32_OK)
  {
    LERROR(TRACE_REGISTRY, "Registry RTOS init failed");
    Error_Handler();
  }

  /* Start registry */
  status = registry_start(false);
  if (status == SLIB32_RESET)
  {
    LINFO(TRACE_REGISTRY, "Registry has reset to defaults");
  }
  else if (status == SLIB32_UPDATED)
  {
    LINFO(TRACE_REGISTRY, "Registry updated");
  }
  else if (status != SLIB32_OK)
  {
    LERROR(TRACE_REGISTRY, "Registry failed (%ld)", status);
    Error_Handler();
  }

  /* Safe-boot: bump boot_count + sync-save BEFORE any other task (NN,
   * camera, display) gets a chance to crash. shell_task reads this
   * counter later to decide if safe mode should engage.
   *
   * Bumping here in registry_task_init means we catch crashes that
   * happen anywhere downstream of this point — including NN model
   * load + ATON HW init, which run before shell_task_init completes
   * and would otherwise miss the v1.5.1 shell-side bump. */
  {
    t_registry_data *reg = registry_acquire();
    if (reg != NULL)
    {
      uint8_t bn = (uint8_t)(reg->boot_count + 1U);
      reg->boot_count = bn;
      registry_release();
      /* Synchronous save — registry_request_save() only queues a save
       * event for our own task loop (which hasn't started yet), so the
       * counter would never land in flash if NN crashes next. */
      if (registry_save() == SLIB32_OK)
      {
        LINFO(TRACE_REGISTRY, "Boot count = %u", (unsigned)bn);
      }
      else
      {
        LERROR(TRACE_REGISTRY, "Boot count save failed (count=%u)", (unsigned)bn);
      }
    }
  }

  /*-->> READY <<--*/
  LINFO(TRACE_REGISTRY, "Task started");
  task_raise_event(TX_EVT_REGISTRY_READY);
}

/**
 * @brief Registry read function
 * @param buff Destination buffer
 * @param size Number of bytes to read
 * @return Number of bytes read or error code (negative)
 */
static int32_t _registry_read(uint8_t *buff, size_t size)
{
  return flash_read((uint32_t)&_sregistry, buff, size);
}

/**
 * @brief Registry write function
 * @param buff Source buffer
 * @param size Number of bytes to write
 * @return Number of bytes written or error code (negative)
 */
static int32_t _registry_write(const uint8_t *buff, size_t size)
{
  return flash_write((uint32_t)&_sregistry, buff, size);
}

/**
 * @brief Registry CRC function
 * @param buff Buffer
 * @param size Buffer size
 * @return CRC32 value
 */
static uint32_t _registry_crc32(const uint8_t *buff, size_t size)
{
  return bsp_crc_calculate((uint32_t*)buff, size);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Registry -->
*//*--------------------------------------------------------------------------*/
