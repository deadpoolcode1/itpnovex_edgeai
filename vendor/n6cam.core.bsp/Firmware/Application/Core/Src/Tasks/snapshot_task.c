/**
 ******************************************************************************
 * @file    snapshot_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the snapshot capture module.
 *          This module is responsible for:
 *          - Receive user commands to capture a snapshot
 *          - Requesting a JPEG encoded frame
 *          - Storing the snapshot
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
#include "fx_app.h"
#include "jpeg_task.h"
#include "n6cam_core.h"
#include "n6cam_sdio.h"
#include "snapshot_task.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Snapshot
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

/* Snapshot task tunables
 * TODO: Optimize stack size
 */
#define SNAPSHOT_TASK_STACK_SIZE  (2U * 1024U)
#define SNAPSHOT_TASK_PRIO        APP_PRIO_USER_INTERFACE
#define SNAPSHOT_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* Snapshot task events */
#define SNAPSHOT_EVT_ALL          0xFFFFFFFFU
#define SNAPSHOT_EVT_REQUEST      BIT(0U)
#define SNAPSHOT_EVT_START        BIT(1U)

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

typedef enum
{
  AVAILABLE = 0,
  WAITING,
  BUSY,
} t_snap_state;

/** Snapshot task handler */
typedef struct
{
  __IO t_snap_state     state;
  TX_EVENT_FLAGS_GROUP  evt;
  TX_THREAD             thread;
  uint8_t               stack[SNAPSHOT_TASK_STACK_SIZE];
} t_snapshot_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_snapshot_task _snapshot_task = {
  .state = AVAILABLE,
};

/* SoW §7 filename override. Empty = use vendor default 'Snapshot{idx}.jpeg'. */
#define SNAP_FILENAME_MAX  63U
static char _snap_filename[SNAP_FILENAME_MAX + 1U] = "";

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _snapshot_task_run(uint32_t args);
static void _snapshot_task_init(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t snapshot_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_snapshot_task.thread, "tx.task.snapshot",
    _snapshot_task_run, 0,
    _snapshot_task.stack, SNAPSHOT_TASK_STACK_SIZE,
    SNAPSHOT_TASK_PRIO, SNAPSHOT_TASK_PRIO,
    SNAPSHOT_TASK_TIME_SLICE, TX_AUTO_START
  );
}

void snapshot_create(t_draw *draw)
{
  /* Validate */
  if (_snapshot_task.state != WAITING)
  {
    return;
  }

  /* Prepare JPEG */
  _snapshot_task.state = BUSY;
  jpeg_configure(draw);
  rtos_raise_event(&_snapshot_task.evt, SNAPSHOT_EVT_START);
}

bool snapshot_trigger(void)
{
  /* Only start if not already occupied */
  if (bsp_sdio_is_detected() && (_snapshot_task.state == AVAILABLE))
  {
    rtos_raise_event(&_snapshot_task.evt, SNAPSHOT_EVT_REQUEST);
    return true;
  }
  return false;
}

void snapshot_set_filename(const char *filename)
{
  if (filename == NULL)
  {
    _snap_filename[0] = '\0';
    return;
  }
  /* Copy at most SNAP_FILENAME_MAX bytes, always NUL-terminate */
  size_t i;
  for (i = 0; (i < SNAP_FILENAME_MAX) && (filename[i] != '\0'); i++)
  {
    _snap_filename[i] = filename[i];
  }
  _snap_filename[i] = '\0';
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Snapshot task entry point.
 * @param args Task arguments
 */
static void _snapshot_task_run(uint32_t args)
{
  uint8_t *jpeg_buff;
  size_t  jpeg_size;
  int32_t status;

  UNUSED(args);

  /* Initialize task */
  _snapshot_task_init();

  /* Snapshot task */
  while (1)
  {
    /* Reset state, if not done already */
    _snapshot_task.state = AVAILABLE;

    /* Wait for user event to start */
    rtos_wait_any_event(&_snapshot_task.evt, SNAPSHOT_EVT_REQUEST, true);
    _snapshot_task.state = WAITING;

    /* Wait for image capture, then encode/store */
    rtos_wait_any_event(&_snapshot_task.evt, SNAPSHOT_EVT_START, true);

    jpeg_buff = jpeg_encode(&jpeg_size);
    if (_snap_filename[0] != '\0')
    {
      status = fx_app_write_file_exact(_snap_filename, jpeg_buff, jpeg_size);
      _snap_filename[0] = '\0';  /* one-shot override */
    }
    else
    {
      status = fx_app_write_file("Snapshot", "jpeg", jpeg_buff, jpeg_size);
    }
    if (status != FX_SUCCESS)
    {
      LERROR(TRACE_SNAPSHOT, "Store snapshot failed (%d)", status);
    }
  }
}

/**
 * @brief Snapshot task initialization.
 */
static void _snapshot_task_init(void)
{
  UINT status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_SNAPSHOT_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Create an event flags group. */
  status = tx_event_flags_create(&_snapshot_task.evt, "tx.evt.snapshot");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /*-->> READY <<--*/
  LINFO(TRACE_SNAPSHOT, "Task started");
  task_raise_event(TX_EVT_SNAPSHOT_READY);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Snapshot -->
*//*--------------------------------------------------------------------------*/
