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
#include "shell_task.h"
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

/* Debounced count, and what was last reported.
 *
 * Two faults were measured on the bench, and they are the same fault seen
 * from two sides:
 *
 *   - The raw edge re-arms the instant the count touches zero, so a single
 *     frame in which the detector drops every box — glare, motion blur, a
 *     confidence sitting on the threshold — reads as the room emptying and
 *     the next frame reads as a fresh arrival. A static scene produced an
 *     event every few seconds, faster than the modem could drain them.
 *   - Firing only on 0->N means a person joining people already in view
 *     raises nothing at all: 3 -> 4 is not an edge. A scene that never
 *     empties therefore never reports again, and looks broken from the
 *     outside while working exactly as written.
 *
 * So the count itself is debounced — a new value must hold **continuously
 * for _nn_debounce_ms** before it is believed — and every change of the
 * debounced value is reported, not just the departure from zero. The
 * default is 1000 ms: a person walking in is reported a second later, and
 * nothing that does not survive a whole second is reported at all. 0
 * disables the wait and reports every frame's change, which is the old
 * behaviour and the reason for the storm.
 *
 * The window is measured in time rather than frames on purpose. Frame rate
 * varies with load, so "N frames" is a different amount of steadiness at
 * 30 Hz than at 12, and it is steadiness in seconds that the customer's
 * server cares about.
 *
 * Every change is reported in both directions — 3 -> 4 and 4 -> 3 alike —
 * including the change to zero, which is how a server learns the area is
 * clear. rsn stays 0x10 and rsd carries the new count, so an rsd of 0 reads
 * as "no people now". The SD snapshot is skipped on that one: there is
 * nothing to photograph. */
#define NN_DEBOUNCE_DEFAULT_MS   1000U

static volatile uint32_t _nn_debounce_ms   = NN_DEBOUNCE_DEFAULT_MS;
static uint32_t          _nn_cand_boxes    = 0U;  /* value being confirmed */
static uint32_t          _nn_cand_since    = 0U;  /* tick it first showed  */
static uint32_t          _nn_stable_boxes  = 0U;  /* last believed count   */

/* ThreadX ticks for the configured window, rounded up so a sub-tick
 * setting still waits at least one tick. */
static uint32_t _nn_debounce_ticks(void)
{
  uint32_t ms = _nn_debounce_ms;
  if (ms == 0U) { return 0U; }
  uint32_t ticks = (ms * TX_TIMER_TICKS_PER_SECOND) / 1000U;
  return (ticks == 0U) ? 1U : ticks;
}

void nn_task_debounce_set(uint32_t ms)  { _nn_debounce_ms = ms; }
uint32_t nn_task_debounce_get(void)     { return _nn_debounce_ms; }

void nn_task_detect_set(bool enable)
{
  /* Start from a known-empty scene. Without this a `detect stop` taken
   * while people were in view leaves the count believed, and the first
   * arrival after `detect start` reports nothing new. */
  if (enable)
  {
    _nn_cand_boxes   = 0U;
    _nn_cand_since   = tx_time_get();
    _nn_stable_boxes = 0U;
    _nn_prev_boxes   = 0U;
  }
  _nn_detect_enabled = enable;
}
bool nn_task_detect_get(void)          { return _nn_detect_enabled; }
void nn_task_action_set(uint8_t mask)  { _nn_action_mask = mask; }
void nn_task_det_set(uint8_t mask)     { _nn_det_mask = mask; }

/* Test-frame override: when non-NULL, the NN loop reads inference input
 * from this buffer instead of the camera's ancillary buffer. Useful for
 * bench-testing the algorithm against a known scene without depending on
 * camera focus. Caller is responsible for cache coherence.
 *
 * It expires. `frame upload` arms this buffer immediately (so that a
 * following `detect start` cannot feed the NN an ATON-hostile live frame
 * mid-test), and nothing in the shell obliges anyone to clear it again —
 * which meant an injection test could, and did, leave the product running
 * inference on a still photograph for three days. The live view kept
 * drawing that picture's boxes over the real video, and because detection
 * notifications fire on the 0->N box edge and the picture's count never
 * fell back to zero, no real person walking into view could raise an event
 * again. Neither symptom looks like a fault; both look like a camera that
 * has stopped seeing people.
 *
 * So the override carries a deadline that every set() restarts. A test
 * holds it for as long as the test runs; an abandoned test gets the lens
 * back on its own. */
#define NN_TEST_FRAME_TTL_S       120U
#define NN_TEST_FRAME_TTL_TICKS   (NN_TEST_FRAME_TTL_S * TX_TIMER_TICKS_PER_SECOND)

static uint8_t * volatile _nn_test_frame_override = NULL;
static volatile uint32_t  _nn_test_frame_deadline = 0U;

/* Signed compare, so the comparison stays correct across tick wrap. */
static bool _nn_test_frame_expired(void)
{
  return (int32_t)(tx_time_get() - _nn_test_frame_deadline) >= 0;
}

void nn_task_set_test_frame(uint8_t *frame)
{
  _nn_test_frame_deadline = tx_time_get() + NN_TEST_FRAME_TTL_TICKS;
  _nn_test_frame_override = frame;
}

bool nn_task_test_frame_active(void)
{
  return (_nn_test_frame_override != NULL) && !_nn_test_frame_expired();
}

uint32_t nn_task_test_frame_remaining_s(void)
{
  if (!nn_task_test_frame_active())
  {
    return 0U;
  }
  int32_t left = (int32_t)(_nn_test_frame_deadline - tx_time_get());
  return (left <= 0) ? 0U
                     : ((uint32_t)left / TX_TIMER_TICKS_PER_SECOND) + 1U;
}

uint32_t nn_task_get_box_count(void)        { return (uint32_t)_pp_box_count; }

/* Debug: snapshot of the model's most recent output tensor. The NN loop
 * memcpy's a small head/tail of _nn_out[0] into here after each inference;
 * the shell can read it for sanity-checking model behaviour without an
 * SWD debugger. Just the first 16 + last 16 floats; enough to spot
 * total-garbage cases vs sensible distributions. */
typedef struct { float head[16]; float tail[16]; uint32_t bytes; } t_nn_dump;
static volatile t_nn_dump _nn_dump = {0};
void nn_task_dump_output(float *head_out, float *tail_out, uint32_t *bytes_out)
{
  if (head_out)  memcpy(head_out, (const void*)_nn_dump.head, sizeof(_nn_dump.head));
  if (tail_out)  memcpy(tail_out, (const void*)_nn_dump.tail, sizeof(_nn_dump.tail));
  if (bytes_out) *bytes_out = _nn_dump.bytes;
}

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
  if ((boxes != _nn_stable_boxes) && (_nn_action_mask != 0U))
  {
    if ((_nn_action_mask & 0x01U) && (boxes > 0U))
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
      (void)snapshot_request(fname);
    }
    LINFO(TRACE_NN, "[simulate] %lu object(s)", (unsigned long)boxes);
  }
  /* Adopt the simulated count as the believed one, with the window
   * restarted: a simulate is an assertion about the scene, and the live
   * loop should report the real count as a change away from it rather
   * than immediately re-reporting what was just simulated. */
  _nn_stable_boxes = boxes;
  _nn_cand_boxes   = boxes;
  _nn_cand_since   = tx_time_get();
  _nn_prev_boxes   = boxes;
}

/* COCO-class -> SoW class mapping (proposal W5/W6).
 *   bit0 = person (COCO 0); bit1 = vehicles.
 * Vehicle COCO ids: bicycle 1, car 2, motorcycle 3, bus 5, truck 7, plus
 * the airplane 4 / train 6 / boat 8 bucket (the model occasionally labels
 * a car/truck as one of these; "something on wheels/wings/water" still
 * counts as a vehicle for the W6 signal). */
static bool _class_passes_mask(int32_t class_index, uint8_t det_msk)
{
  /* People class (COCO person = 0) */
  if (class_index == 0)      return (det_msk & 0x01U) != 0U;
  /* Vehicle classes */
  if (class_index == 2 || class_index == 1 ||   /* car, bicycle        */
      class_index == 3 || class_index == 5 ||   /* motorcycle, bus     */
      class_index == 7 || class_index == 4 ||   /* truck, airplane     */
      class_index == 6 || class_index == 8)     /* train, boat         */
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
      if ((override != NULL) && _nn_test_frame_expired())
      {
        /* Hand the lens back, once, and say so loudly enough that anyone
         * reading the console understands why the picture stopped. */
        _nn_test_frame_override = NULL;
        override = NULL;
        LWARNING(TRACE_NN, "test frame expired after %u s — NN back on the "
                           "live camera", (unsigned)NN_TEST_FRAME_TTL_S);
      }
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

      /* Debounce the count, then report every change of the debounced
       * value. See the _nn_debounce block above for why both halves of
       * this are needed. */
      if (cur_boxes != _nn_cand_boxes)
      {
        _nn_cand_boxes = cur_boxes;
        _nn_cand_since = tx_time_get();     /* restart the window */
      }

      /* Signed compare, so the wait stays correct across tick wrap. */
      bool count_changed = false;
      if ((_nn_cand_boxes != _nn_stable_boxes) &&
          ((int32_t)(tx_time_get() - _nn_cand_since) >=
           (int32_t)_nn_debounce_ticks()))
      {
        _nn_stable_boxes = _nn_cand_boxes;
        count_changed    = true;
      }

      /* Never let an injected picture masquerade as a real detection. The
       * side effects here are the product's outward claims — a JPEG filed
       * as evidence and a notification telling the customer's server that
       * people are present. Both must describe the lens. `frame run` still
       * reports its count on the console, which is what an injection test
       * actually reads, and `detect simulate` is still the explicit way to
       * exercise this path on purpose. */
      if (count_changed && (_nn_action_mask != 0U)
          && (_nn_test_frame_override != NULL))
      {
        LWARNING(TRACE_NN, "%lu detection(s) from the injected test frame — "
                           "not reported (use 'detect simulate' to test the "
                           "notification path)", (unsigned long)cur_boxes);
      }
      else if (count_changed && (_nn_action_mask != 0U))
      {
        /* bit0 = save to SD: build SoW §7 filename + trigger snapshot.
         * Not on the change to zero — there is nobody to photograph. */
        if ((_nn_action_mask & 0x01U) && (_nn_stable_boxes > 0U))
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
          /* Atomic claim so we don't clobber a competing producer
           * (shell `photo savesd`) racing on the shared filename
           * buffer. If the pipeline is already busy, drop this one
           * — we can't block inference waiting for SD. */
          (void)snapshot_request(fname);
        }
        /* bit1 = report: emit the SoW §6 notification for this detection.
         * shell_notify_emit writes it to the CDC shell and queues it to the
         * modem; the modem leg is asynchronous, so this returns in
         * microseconds and inference is not held behind the UART link.
         *
         * rsn=0x10 is the §4.2 "people detected" bit and rsd carries the
         * count — the same pair `detect simulate` reports, so a simulated
         * detection and a real one are indistinguishable downstream. */
        if (_nn_action_mask & 0x02U)
        {
          shell_notify_emit(0x10U, _nn_stable_boxes, false);
        }
        LINFO(TRACE_NN, "count now %lu object(s)",
              (unsigned long)_nn_stable_boxes);
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

  /* Run ATON.
   *
   * Watchdog: cap the total time we'll spin in the inner WFE loop. A
   * normal inference is ~25 ms; legitimate epochs all return within a
   * few ms of WFE. If we see >1 second of looping (which has only
   * been observed on specific model + input combinations where the
   * ATON deadlocks an epoch's completion IRQ) we abort the inference,
   * mark this frame as a no-detection, and return so the kit keeps
   * processing live camera frames instead of bricking on a single
   * bad frame. */
  stat_time_start(STAT_TIME_NN_MODEL);
  stai_return_code ret;
  uint32_t wdog_ticks = tx_time_get();
  uint32_t wdog_iters = 0U;
  bool     wdog_fired = false;
  const uint32_t WDOG_TICKS_MAX  = 100U;     /* ~1 s @ 100Hz */
  const uint32_t WDOG_ITERS_MAX  = 4000U;    /* belt-and-braces */

  do {
    ret = stai_network_run(network_context, STAI_MODE_ASYNC);
    if (ret == STAI_RUNNING_WFE)
      LL_ATON_OSAL_WFE();
    wdog_iters++;
    if (wdog_iters > WDOG_ITERS_MAX ||
        (tx_time_get() - wdog_ticks) > WDOG_TICKS_MAX)
    {
      wdog_fired = true;
      break;
    }
  } while (ret == STAI_RUNNING_WFE || ret == STAI_RUNNING_NO_WFE);

  if (wdog_fired)
  {
    LERROR(TRACE_NN, "NN inference watchdog fired after %lu ticks / %lu iters — aborting frame",
           (unsigned long)(tx_time_get() - wdog_ticks), (unsigned long)wdog_iters);
    /* Reset inference state so the next frame starts clean. */
    (void)stai_ext_network_new_inference(network_context);
    stat_time_stop(STAT_TIME_NN_MODEL);
    flash_acquire(false);
    /* Report zero detections for this frame and bail. */
    _pp_box_count = 0;
    return;
  }

  ret = stai_ext_network_new_inference(network_context);
  if (ret != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network new inference failed");
    Error_Handler();
  }
  stat_time_stop(STAT_TIME_NN_MODEL);

  /* Release FLASH */
  flash_acquire(false);

  /* The active model (yolov8n, conv-only INT8 with the head kept in
   * float) outputs FULLY-DECODED detections: class channels are already
   * sigmoid probabilities and box channels are pixel cx/cy/w/h in
   * [0..input_size]. So we need NEITHER the sigmoid nor the stedgeai
   * dequant-bias correction the relu30 model required (float32 output
   * here carries the dequant). The only fix-up is scaling the 4 box
   * channels to the normalized [0,1] range the PP/display/SoW box report
   * expect. Layout is CHW = [4+NB_CLASSES, NUM_BOXES], stride NUM_BOXES. */
#if (POSTPROCESS_TYPE == POSTPROCESS_OD_YOLO_V8_UF)
  if (_nn_out[0] != NULL && _nn_out_len[0] >= (4U + AI_OD_YOLOV8_PP_NB_CLASSES) * AI_OD_YOLOV8_PP_TOTAL_BOXES * sizeof(float32_t))
  {
    SCB_InvalidateDCache_by_Addr(_nn_out[0], _nn_out_len[0]);
    float32_t      *out = (float32_t*)_nn_out[0];
    const float32_t inv = 1.0f / (float32_t)CAMERA_ANCILLARY_WIDTH;   /* 256 */
    const uint32_t  NB  = AI_OD_YOLOV8_PP_TOTAL_BOXES;
    for (uint32_t ch = 0U; ch < 4U; ch++)        /* x_center, y_center, w, h */
    {
      for (uint32_t b = 0U; b < NB; b++)
      {
        out[ch * NB + b] *= inv;
      }
    }
    SCB_CleanDCache_by_Addr(_nn_out[0], _nn_out_len[0]);
  }
#endif

  /* Debug: capture head/tail of output[0] for shell inspection. The model
   * output is float32 (NB_CLASSES + 4 bbox per box, * num_boxes). */
  if (_nn_out[0] != NULL && _nn_out_len[0] > 0U)
  {
    /* Output buffer is in PSRAM written by NPU — flush cache for read */
    SCB_InvalidateDCache_by_Addr(_nn_out[0], _nn_out_len[0]);
    uint32_t n = _nn_out_len[0];
    _nn_dump.bytes = n;
    memcpy((void*)_nn_dump.head, _nn_out[0], sizeof(_nn_dump.head));
    if (n >= sizeof(_nn_dump.tail))
    {
      memcpy((void*)_nn_dump.tail, _nn_out[0] + n - sizeof(_nn_dump.tail), sizeof(_nn_dump.tail));
    }
    /* Also log the output buffer address for diagnostic */
    LINFO(TRACE_NN, "NN out[0]@%p len=%lu", _nn_out[0], (unsigned long)_nn_out_len[0]);
  }
  else
  {
    LINFO(TRACE_NN, "NN out[0]=%p len=%lu nb=%lu", _nn_out[0],
          (unsigned long)_nn_out_len[0], (unsigned long)_nn_out_nb);
  }

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
  /* Convert boxes. The SSD PP can return more detections than our buffer
   * holds (up to max_boxes_limit * nb_classes); clamp to NN_BOXES_MAX_NUM
   * so we never overflow _pp_box_buff (and the shell's stack copy of it). */
  memset(_pp_box_buff, 0x00U, sizeof(_pp_box_buff));
  int32_t n = out->nb_detect;
  if (n > NN_BOXES_MAX_NUM) { n = NN_BOXES_MAX_NUM; }
  for (int32_t idx = 0; idx < n; idx++)
  {
    _pp_box_buff[idx] = (t_nn_box)(out->pOutBuff[idx]);
  }
  _pp_box_count = (size_t)n;

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
