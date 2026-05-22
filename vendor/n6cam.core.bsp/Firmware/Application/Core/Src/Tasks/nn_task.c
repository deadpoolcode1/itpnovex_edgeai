/**
 ******************************************************************************
 * @file    nn_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the NN module.
 *          This module is responsible for:
 *          - Initialize NN model and PP
 *          - Run inference
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
#include "nn_task.h"

#if ENABLE_NN == 1U
#include "camera_task.h"
#include "flash.h"
#include "n6cam_npu.h"
#include "stai.h"
#include "stai_network.h"
#include "snapshot_task.h"
#include "n6cam_rtc.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup NN
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

/* NN task tunables
 * TODO: Optimize stack size
 */
#define NN_TASK_STACK_SIZE      (2U * 1024U)
#define NN_TASK_PRIO            APP_PRIO_IMPORTANT
#define NN_TASK_TIME_SLICE      APP_TIME_SLICE_DEFAULT

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

/** NN task handler */
typedef struct
{
  TX_MUTEX      pp_box_mtx;
  TX_SEMAPHORE  aton_sem;
  TX_THREAD     thread;
  uint8_t       stack[NN_TASK_STACK_SIZE];
} t_nn_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/* Task handler */
static t_nn_task                _nn_task = { 0 };

/* Internals */
static uint8_t*                 _nn_frame = NULL;
static uint8_t*                 _nn_in;
static uint32_t                 _nn_in_len;
static uint8_t*                 _nn_out[STAI_NETWORK_OUT_NUM] = { 0 };
static uint32_t                 _nn_out_len[STAI_NETWORK_OUT_NUM] = { 0 };
static uint32_t                 _nn_out_nb = 0;

static size_t                   _pp_box_count;
static t_nn_box                 _pp_box_buff[NN_BOXES_MAX_NUM];
static t_nn_params              _pp_params;

/* Model and Model Interface Instantiation */
STAI_NETWORK_CONTEXT_DECLARE(network_context, STAI_NETWORK_CONTEXT_SIZE)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _nn_task_run(uint32_t args);
static void _nn_task_init(void);
static void _nn_config_npu(void);

static void _nn_frame_process(void);

static void _pp_publish_objects(t_nn_boxes *out);

extern void ll_aton_osal_wfe(void);
extern void ll_aton_osal_signal_event(void);
extern void NPU_END_OF_EPOCH_IRQHandler(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t nn_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_nn_task.thread, "tx.task.nn",
    _nn_task_run, 0,
    _nn_task.stack, NN_TASK_STACK_SIZE,
    NN_TASK_PRIO, NN_TASK_PRIO,
    NN_TASK_TIME_SLICE, TX_AUTO_START
  );
}

uint32_t nn_get_detections(t_nn_box* box_buff)
{
  /* Validate */
  if (!box_buff || (_pp_box_count == 0))
  {
    return 0;
  }

  /* Capture detections */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);

  memcpy(box_buff, &_pp_box_buff, _pp_box_count * sizeof(t_nn_box));

  /* Release and return */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);
  return _pp_box_count;
}

uint32_t nn_task_suspend_thread(void)
{
  return tx_thread_suspend(&_nn_task.thread);
}

uint32_t nn_task_resume_thread(void)
{
  return tx_thread_resume(&_nn_task.thread);
}

/* Runtime detection gate (SoW §3.1 detect start/stop). Default is OFF —
 * the SoW says detection must be explicitly started. */
static volatile bool    _nn_detect_enabled = false;
static volatile uint8_t _nn_action_mask    = 0U;
static volatile uint8_t _nn_det_mask       = 0x01U; /* default people */
/* Edge detection: previous frame's box count. Lets us fire snapshot/notify
 * only on 0->N transitions, not every frame at 22 Hz. */
static uint32_t          _nn_prev_boxes    = 0U;

void nn_task_detect_set(bool enable)   { _nn_detect_enabled = enable; }
bool nn_task_detect_get(void)          { return _nn_detect_enabled; }
void nn_task_action_set(uint8_t mask)  { _nn_action_mask = mask; }
void nn_task_det_set(uint8_t mask)     { _nn_det_mask = mask; }

/* Test-frame override: when non-NULL, the NN loop reads inference input
 * from this buffer instead of the camera's ancillary buffer. Useful for
 * bench-testing the algorithm against a known scene without depending on
 * camera focus. Caller is responsible for cache coherence. */
static uint8_t * volatile _nn_test_frame_override = NULL;

void nn_task_set_test_frame(uint8_t *frame) { _nn_test_frame_override = frame; }
uint32_t nn_task_get_box_count(void)        { return (uint32_t)_pp_box_count; }

/* SoW test-injection (firmware-side simulate). Sets a synthetic box count
 * + a synthetic person-class box, runs the same on-edge side-effects the
 * inference loop would: snapshot trigger + notification + trace log.
 * Skips the actual NN inference — useful when the camera is out of focus
 * or the lens is dirty, so we want to verify the post-detect chain in
 * isolation. Holds the box-buffer mutex like the real path. */
void nn_task_simulate_detection(uint32_t boxes)
{
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
  _pp_box_count = (size_t)boxes;
  if (boxes > 0U)
  {
    /* Populate one fake person-class box so consumers (nn_get_detections)
     * see something coherent. Only fields the downstream cares about. */
    memset(&_pp_box_buff[0], 0, sizeof(_pp_box_buff[0]));
    _pp_box_buff[0].class_index = 0;   /* COCO person */
    _pp_box_buff[0].conf        = 1.0f;
  }
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);

  /* Drive the same edge logic the inference loop uses. We replay the
   * snapshot + notification side effects inline because we don't want
   * to wait for the next camera frame to wake the loop. */
  if ((boxes > 0U) && (_nn_prev_boxes == 0U) && (_nn_action_mask != 0U))
  {
    if (_nn_action_mask & 0x01U)
    {
      t_datetime dt = { 0 };
      (void)bsp_rtc_get_time(&dt);
      uint32_t ser = HAL_GetUIDw0();
      char fname[48];
      snprintf(fname, sizeof(fname),
               "%lu_%02u%02u20%02u_%02u%02u%02u.rdy",
               (unsigned long)ser,
               (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year,
               (unsigned)dt.hours, (unsigned)dt.minutes, (unsigned)dt.seconds);
      snapshot_set_filename(fname);
      if (!snapshot_trigger())
      {
        snapshot_set_filename(NULL);
      }
    }
    LINFO(TRACE_NN, "[simulate] %lu object(s)", (unsigned long)boxes);
  }
  _nn_prev_boxes = boxes;
}

/* COCO-class -> SoW class mapping (proposal W5/W6).
 *   bit0 = person; bit1 = car|motorcycle|bus|truck.
 *   Bicycle (class 1) is intentionally ignored — the SoW only lists
 *   people + vehicles. Other COCO classes also drop. */
static bool _class_passes_mask(int32_t class_index, uint8_t det_msk)
{
  /* People class */
  if (class_index == 0)      return (det_msk & 0x01U) != 0U;
  /* Vehicle classes (COCO): car=2, motorcycle=3, bus=5, truck=7 */
  if (class_index == 2 || class_index == 3 ||
      class_index == 5 || class_index == 7)
                              return (det_msk & 0x02U) != 0U;
  return false;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief NN task entry point.
 * @param args Task arguments
 */
static void _nn_task_run(uint32_t args)
{
  UNUSED(args);

  /* Initialize task */
  _nn_task_init();

  /* NN task */
  while (1)
  {
    /* Wait for a new frame */
    camera_wait_event(CAMERA_EVT_FRAME_ANCILLARY, true);

    /* SoW §3.1: skip inference when detection is stopped. We still drain the
     * event above to keep the camera pipeline flowing. */
    if (!_nn_detect_enabled)
    {
      continue;
    }

    /* Process frame. If a test-frame override is in place, use it instead of
     * the live camera buffer — exercises the NN pipeline against a known
     * scene (W6 algorithm validation when camera optics are subpar). */
    {
      uint8_t *override = _nn_test_frame_override;
      _nn_frame = (override != NULL) ? override
                                     : camera_get_buffer(camera.ancillary.id);
    }
    if (_nn_frame != NULL)
    {
      stat_time_start(STAT_TIME_NN_TOTAL);
      _nn_frame_process();
      stat_time_stop(STAT_TIME_NN_TOTAL);

      /* SoW §4.2 W5/W6: filter detections by class against det_msk.
       * Holds the box-buffer mutex briefly so consumers see a consistent
       * (filtered) view. */
      rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
      size_t kept = 0U;
      for (size_t i = 0U; i < _pp_box_count; i++)
      {
        if (_class_passes_mask((int32_t)_pp_box_buff[i].class_index, _nn_det_mask))
        {
          if (kept != i)
          {
            _pp_box_buff[kept] = _pp_box_buff[i];
          }
          kept++;
        }
      }
      _pp_box_count = kept;
      rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);

      /* SoW W12 / W11: on a 0->N box-count edge, fire side effects
       * per the action_msk profile. Edge-only so we don't spam SD
       * with one JPEG per frame at 22 Hz. */
      uint32_t cur_boxes = (uint32_t)_pp_box_count;
      if ((cur_boxes > 0U) && (_nn_prev_boxes == 0U) && (_nn_action_mask != 0U))
      {
        /* bit0 = save to SD: build SoW §7 filename + trigger snapshot */
        if (_nn_action_mask & 0x01U)
        {
          t_datetime dt = { 0 };
          (void)bsp_rtc_get_time(&dt);
          uint32_t ser = HAL_GetUIDw0();
          char fname[48];
          snprintf(fname, sizeof(fname),
                   "%lu_%02u%02u20%02u_%02u%02u%02u.rdy",
                   (unsigned long)ser,
                   (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year,
                   (unsigned)dt.hours, (unsigned)dt.minutes, (unsigned)dt.seconds);
          snapshot_set_filename(fname);
          if (!snapshot_trigger())
          {
            /* SD busy/missing — drop this one; don't block inference */
            snapshot_set_filename(NULL);
          }
        }
        /* bit1 = report cellular: handled when modem channel is wired
         * (W11/W13). For now we still log the detection via trace. */
        LINFO(TRACE_NN, "detected %lu object(s)", (unsigned long)cur_boxes);
      }
      _nn_prev_boxes = cur_boxes;
    }
  }
}

/**
 * @brief NN task initialization.
 */
static void _nn_task_init(void)
{
  const LL_Buffer_InfoTypeDef *in_info;
  const LL_Buffer_InfoTypeDef *out_info;
  int32_t                     status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_NN_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Initialize NPU */
  _nn_config_npu();

  /* Initialize ATON */
  status = tx_semaphore_create(&_nn_task.aton_sem, "tx.sem.nn", 1U);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize boxes */
  _pp_box_count = 0;
  memset(_pp_box_buff, 0x00U, sizeof(_pp_box_buff));
  status = tx_mutex_create(&_nn_task.pp_box_mtx, "tx.mtx.nn.pp", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize STAI */
  status = stai_runtime_init();
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI runtime init failed");
    Error_Handler();
  }
  /* Initialize network */
  status = stai_network_init(network_context);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network init failed");
    Error_Handler();
  }

  /* Get network buffers info */
  stai_network_info info;
  status = stai_network_get_info(network_context, &info);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network get info failed");
    Error_Handler();
  }

  _nn_out_nb = STAI_NETWORK_OUT_NUM;

  /* Get the input buffer size & address */
  _nn_in_len = info.inputs[0].size_bytes;
  status = stai_network_get_inputs(network_context, &_nn_in, (stai_size *)&info.n_inputs);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network get inputs failed");
    Error_Handler();
  }

  /* Get the output buffers size & address */
  status = stai_network_get_outputs(network_context, _nn_out, &_nn_out_nb);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network get outputs failed");
    Error_Handler();
  }
  for (int i = 0; i < _nn_out_nb; i++)
  {
    _nn_out_len[i] = info.outputs[i].size_bytes;
  }



  /* Initialize PP */
  status = app_postprocess_init(&_pp_params, &info);
  if (status != AI_OD_POSTPROCESS_ERROR_NO)
  {
    LERROR(TRACE_NN, "PP init failed");
    Error_Handler();
  }

  /*-->> READY <<--*/
  LINFO(TRACE_NN, "Task started");
  task_raise_event(TX_EVT_NN_READY);
}

/**
 * @brief NPU configuration.
 */
static void _nn_config_npu(void)
{
  bsp_npu_init();
  bsp_npu_ram_enable();
  bsp_npu_cache_enable();
}

/**
 * @brief Process a new ancillary frame in the NPU
 */
static void _nn_frame_process(void)
{
  t_nn_boxes  pp_output;
  int32_t     status;

  /* Invalidate data and prepare NN input */
  stat_time_start(STAT_TIME_NN_COPY);
  SCB_InvalidateDCache_by_Addr(_nn_frame, CAMERA_ANCILLARY_BUFFER_SIZE);
  memcpy(_nn_in, _nn_frame, CAMERA_ANCILLARY_BUFFER_SIZE);
  SCB_CleanDCache_by_Addr(_nn_in, CAMERA_ANCILLARY_BUFFER_SIZE);
  stat_time_stop(STAT_TIME_NN_COPY);

  /* Acquire FLASH (weights) */
  flash_acquire(true);

  /* Run ATON */
  stat_time_start(STAT_TIME_NN_MODEL);
  stai_return_code ret;

  do {
    ret = stai_network_run(network_context, STAI_MODE_ASYNC);
    if (ret == STAI_RUNNING_WFE)
      LL_ATON_OSAL_WFE();
  } while (ret == STAI_RUNNING_WFE || ret == STAI_RUNNING_NO_WFE);

  ret = stai_ext_network_new_inference(network_context);
  if (ret != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network new inference failed");
    Error_Handler();
  }
  stat_time_stop(STAT_TIME_NN_MODEL);

  /* Release FLASH */
  flash_acquire(false);

  /* Run PP */
  pp_output.pOutBuff = NULL;
  stat_time_start(STAT_TIME_NN_PP);
  status = app_postprocess_run((void**)_nn_out, _nn_out_nb, &pp_output, &_pp_params);
  if (status != AI_OD_POSTPROCESS_ERROR_NO)
  {
    LERROR(TRACE_NN, "PP process failed");
    Error_Handler();
  }
  stat_time_stop(STAT_TIME_NN_PP);

  /* Store detections */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
  _pp_publish_objects(&pp_output);
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);
}

/**
 * @brief Publish PP output from PP temporal memory to nn_task output buffer  
 *
 * @param out PP output
 */
static void _pp_publish_objects(t_nn_boxes *out)
{
  /* Convert boxes */
  memset(_pp_box_buff, 0x00U, sizeof(_pp_box_buff));
  for (int32_t idx = 0; idx < out->nb_detect; idx++)
  {
    _pp_box_buff[idx] = (t_nn_box)(out->pOutBuff[idx]);
  }
  _pp_box_count = out->nb_detect;

}

/**
 * @brief ATON started handler.
 */
void ll_aton_osal_wfe(void)
{
  rtos_semaphore_acquire(&_nn_task.aton_sem, true);
}

/**
 * @brief ATON completed handler.
 */
void ll_aton_osal_signal_event(void)
{
  rtos_semaphore_acquire(&_nn_task.aton_sem, false);
}

/**
 * @brief This function handles NPU interrupt request.
 */
void NPU_END_OF_EPOCH_IRQHandler(void)
{
  CDNN0_IRQHandler();
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: NN -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_NN */
