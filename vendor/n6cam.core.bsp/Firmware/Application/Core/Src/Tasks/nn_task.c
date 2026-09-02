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
#include "tile_detect.h"

#if ENABLE_NN == 1U
#include "camera_task.h"
#include "flash.h"
#include "n6cam_npu.h"
#include "stai.h"
#include "stai_network.h"
#include "snapshot_task.h"
#include "shell_task.h"
#include "registry.h"   /* SoW §4.2 notification reason bits */
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
 *
 * 4 KB, not the vendor's 2 KB. This loop no longer only runs inference: on a
 * confirmed change of the count it composes a SoW §6 notification, claims a
 * snapshot slot and reaches down into the USB write path, and the deepest of
 * those calls does not fit in 2 KB alongside its own frame. The buffers that
 * made it overflow now live in static storage (see _notify_emit), which is
 * the actual fix; this is the margin that keeps the next thing added to this
 * loop from being another silent memory corruption. modem_task and jpeg_task
 * are 4 KB for the same reason.
 */
#define NN_TASK_STACK_SIZE      (4U * 1024U)
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

uint32_t nn_get_detections(t_nn_box* box_buff, uint32_t box_cap)
{
  /* Validate */
  if (!box_buff || (box_cap == 0U))
  {
    return 0;
  }

  /* Capture detections.
   *
   * The count is re-read and bounded inside the lock against BOTH the
   * destination's capacity and our own array. Trusting a global for the
   * length of a memcpy into a caller's buffer is what made a bad
   * `_pp_box_count` a memory-corruption bug rather than a wrong number. */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);

  uint32_t n = (uint32_t)_pp_box_count;
  if (n > (uint32_t)NN_BOXES_MAX_NUM) { n = (uint32_t)NN_BOXES_MAX_NUM; }
  if (n > box_cap)                    { n = box_cap; }
  if (n > 0U)
  {
    memcpy(box_buff, &_pp_box_buff[0], (size_t)n * sizeof(t_nn_box));
  }

  /* Release and return */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);
  return n;
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
/* ── Main-path tiled detection (ScopusQA #22) ──────────────────────────────
 *
 * Off, the NN eats one 256x256 ancillary frame per camera event: the whole
 * field of view squeezed into 256 px, which is why a car at a third of the
 * frame vanishes (ScopusQA #17 — measured cliff between 50 % and 35 %).
 *
 * On, each camera event carries ONE TILE of a sweep: snapshot the main pipe at
 * the top of the sweep, crop tile i, infer, accumulate. After the last tile,
 * cross-tile NMS produces the merged detection set and that — not any single
 * tile — is what enters the debounce and the §4.2 action path. A sweep is
 * therefore ~12 inferences, about 1.1 s, which is the rate this was specified
 * at ("run the camera slowly, ~1 FPS").
 *
 * Two invariants matter and both cost a little code below:
 *
 *   - Nothing partial is ever published. `_pp_box_buff` keeps showing the last
 *     COMPLETED sweep for the whole of the next one, so the live overlay and
 *     `nn_get_detections` never see one tile's boxes in full-frame coordinates.
 *   - The count that drives notifications is the untruncated survivor count,
 *     while `_pp_box_count` stays clamped to NN_BOXES_MAX_NUM. A 12-tile sweep
 *     can find more objects than the publish buffer holds, and a consumer
 *     reading `_pp_box_count` entries out of a 20-entry array is the overflow
 *     the `detect simulate` clamp exists to prevent.
 *
 * An injected test frame still wins: `frame run` and the injection suites need
 * the single-frame path, so tiling stands down while an override is armed.
 */
static volatile bool    _nn_tile_mode      = false;
static uint32_t         _nn_tile_idx       = 0U;   /* next tile in the sweep */
static uint32_t         _nn_tile_n         = 0U;   /* tiles in this sweep    */
static uint32_t         _nn_tile_sweeps    = 0U;   /* completed sweeps       */
static uint32_t         _nn_tile_ms        = 0U;   /* last sweep, ms         */
static uint32_t         _nn_tile_t0        = 0U;
static bool             _nn_tile_starved   = false;/* camera gave no buffer  */

/* Last COMPLETED sweep, kept so the publish buffer never shows a part sweep. */
static t_nn_box         _nn_tile_pub[NN_BOXES_MAX_NUM];
static uint32_t         _nn_tile_pub_n     = 0U;

/* Per-tile NN input. Its own buffer: the shell's `_frame_test_buf` belongs to
 * the injection path, which must stay usable while this is running. */
static __ALIGN_BEGIN uint8_t
  _nn_tile_in[CAMERA_ANCILLARY_BUFFER_SIZE] __ALIGN_END IN_PSRAM;

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
 * The first attempt at this required a new count to hold **continuously**
 * for a whole window before it was believed, and it was wrong in a way that
 * only a busy scene shows. Restarting the window on every change means a
 * count that oscillates faster than the window never survives one: with
 * four people in frame and one of them flickering in and out — which is
 * exactly what a 192x192 quantised detector does to a partly-occluded or
 * distant object — the count alternates 3,4,3,4 at frame rate and the
 * window restarts every ~90 ms, forever. The believed count never moves,
 * so **no notification and no photo are ever sent at all** while people are
 * plainly in view. Measured on the bench: a static scene of four people
 * took 144 s to report once, and reported nothing in between. Silence is a
 * worse failure than the storm it replaced, because it looks like a dead
 * device.
 *
 * What the flicker actually is decides the fix. A detector of this size
 * misses objects that are there far more often than it invents ones that
 * are not, so an oscillation between 3 and 4 almost always means four, seen
 * imperfectly. The count is therefore hysteretic rather than debounced, and
 * deliberately asymmetric:
 *
 *   - **Rising** is judged over one whole window, on how many of the looks
 *     taken in it agreed: the higher count must appear in at least
 *     NN_RISE_CONFIRM_PCT of the frames seen since the evidence opened,
 *     and at least NN_RISE_CONFIRM_FRAMES times so that a very short
 *     window still works. Not consecutively — the frames in between are
 *     the dropouts.
 *   - **Falling** must be earned: the count is adopted downward only after
 *     it has stayed below the believed value for the whole window without
 *     once touching it, and it then falls to the *highest* value seen
 *     during that window rather than the last one. A person who blinks out
 *     for three frames does not empty the room.
 *
 * So 3 -> 4 reports one window later, 4 -> 3 -> 4 -> 3 reports nothing, and
 * everybody leaving reports 0 one window later. The scheme always
 * converges: the rise path cannot be starved by oscillation, and the fall
 * path is reset only by the count actually returning to what is believed.
 *
 * **Why the rise is a fraction and not a count (ScopusQA #8).** Two frames
 * was the first rule, on the reasoning that one spurious box is a blip and
 * two agreeing looks are a scene. Measured against an empty bench, that is
 * simply untrue: the detector invents a person for one or two frames about
 * five times a minute, and any pair of those inside one window confirmed a
 * rise. A camera pointed at an empty room therefore reported somebody
 * arriving and leaving every few seconds — and with action bit2 set it
 * uploaded a photograph of the empty room each time.
 *
 * No count can separate the two cases, because the flicker this scheme
 * exists to tolerate is also non-consecutive. Only the *rate* differs, and
 * it differs by an order of magnitude: four people with one flickering puts
 * the higher count in about half the frames of a window, while an invented
 * box reaches 5-8%. NN_RISE_CONFIRM_PCT sits between the two.
 *
 * The window is measured in time rather than frames on purpose. Frame rate
 * varies with load, so "N frames" is a different amount of steadiness at
 * 30 Hz than at 12, and it is steadiness in seconds that the customer's
 * server cares about. The rise confirmation is a fraction of the frames in
 * that window because it is about how many looks at the scene agree, not
 * how long they took — and a fraction, unlike a count, means the same
 * thing at 30 Hz as it does at 12.
 *
 * Every change is reported in both directions — 3 -> 4 and 4 -> 3 alike —
 * including the change to zero, which is how a server learns the area is
 * clear. rsn stays 0x10 and rsd carries the new count, so an rsd of 0 reads
 * as "no people now". The SD snapshot is skipped on that one: there is
 * nothing to photograph.
 *
 * `detect debounce 0` disables both halves and reports every frame's
 * change, which is the original behaviour and the reason for the storm; it
 * is kept because it is the only way to see the raw detector from outside. */
#define NN_DEBOUNCE_DEFAULT_MS    1000U
#define NN_RISE_CONFIRM_FRAMES    2U    /* floor, for very short windows */
#define NN_RISE_CONFIRM_PCT       40U   /* share of the window that must agree */

static volatile uint32_t _nn_debounce_ms   = NN_DEBOUNCE_DEFAULT_MS;
static uint32_t          _nn_stable_boxes  = 0U;  /* last believed count   */
/* Latest per-class split of the filtered box buffer, and the last split
 * actually reported, so an unchanged class is not re-announced. */
static uint32_t          _nn_people_now     = 0U;
static uint32_t          _nn_vehicles_now   = 0U;
static uint32_t          _nn_people_rep     = 0U;
static uint32_t          _nn_vehicles_rep   = 0U;

/* Rising: the highest count seen while collecting evidence, how many frames
 * have exceeded the believed value, how many frames have been looked at
 * since, and when that evidence started. A window after it opens the
 * evidence is judged and then cleared either way. */
static uint32_t          _nn_rise_cand     = 0U;
static uint32_t          _nn_rise_hits     = 0U;
static uint32_t          _nn_rise_frames   = 0U;
static uint32_t          _nn_rise_since    = 0U;

/* Falling: when the count last agreed with the believed value, and the
 * highest it has reached since. */
static uint32_t          _nn_fall_since    = 0U;
static uint32_t          _nn_fall_peak     = 0U;

/* ThreadX ticks for the configured window, rounded up so a sub-tick
 * setting still waits at least one tick. */
/* How many sweeps a change must survive before tiling believes it.
 *
 * Two is the smallest number that means anything: one sweep is a single
 * sample, and a window shorter than the sampling period debounces nothing. */
#define NN_TILE_DEBOUNCE_SWEEPS   2U

/* The debounce window actually in force, in ms.
 *
 * A tiled sweep samples the scene about once every 1.4 s, and the debounce
 * default is 1000 ms — a window SHORTER than the gap between samples, which
 * cannot debounce anything. Every sweep's count was therefore believed on
 * sight, and a live scene drifting between 3 and 4 people raised an event per
 * sweep. That is not cosmetic: the notification stream shares the console, so
 * it swallowed `photo upload`'s reply and F1/F2 of the integration suite
 * failed against a camera that was working perfectly.
 *
 * So in tile mode the floor is a couple of sweeps. An operator who asks for a
 * longer window still gets it; what they cannot ask for is one shorter than
 * the thing being measured. Public so `detect debounce query` can report it —
 * answering "1000 ms" while the firmware used 2800 would be its own trap.
 */
uint32_t nn_task_debounce_effective(void)
{
  uint32_t ms = _nn_debounce_ms;
  if (ms == 0U) { return 0U; }

  if (_nn_tile_mode && (_nn_tile_ms > 0U))
  {
    uint32_t floor_ms = _nn_tile_ms * NN_TILE_DEBOUNCE_SWEEPS;
    if (ms < floor_ms) { ms = floor_ms; }
  }
  return ms;
}

static uint32_t _nn_debounce_ticks(void)
{
  uint32_t ms = nn_task_debounce_effective();
  if (ms == 0U) { return 0U; }
  uint32_t ticks = (ms * TX_TIMER_TICKS_PER_SECOND) / 1000U;
  return (ticks == 0U) ? 1U : ticks;
}

void nn_task_debounce_set(uint32_t ms)  { _nn_debounce_ms = ms; }
uint32_t nn_task_debounce_get(void)     { return _nn_debounce_ms; }

/* Reset the hysteresis to agree with whatever count is now believed. Used
 * when the believed value is set from outside the live loop (start/stop,
 * `detect simulate`), so the next real frame is judged against it rather
 * than against a window left half-open by the previous scene. */
static void _nn_count_reset(uint32_t to)
{
  _nn_stable_boxes = to;
  _nn_rise_cand    = 0U;
  _nn_rise_hits    = 0U;
  _nn_rise_frames  = 0U;
  _nn_rise_since   = tx_time_get();
  _nn_fall_peak    = 0U;
  _nn_fall_since   = tx_time_get();
}

/* Feed one frame's counts in; returns true when the believed count changed,
 * in which case _nn_stable_boxes is the new value.
 *
 * `raw` is what the frame COUNTED. `sustained` is that plus the detections
 * that held the sustain floor without reaching the counting one — see
 * tile_cfg_conf_sustain(). A rise is judged on `raw`, because nothing below
 * the floor may ever raise a count; a fall is judged on `sustained`, because a
 * detection hovering at the floor has not gone away, and treating it as gone
 * is what made the count alternate on a scene that never moved (ScopusQA #24).
 * The single-frame path has no sub-threshold set — its post-processor
 * thresholds inside — and passes the same number twice, which is the old
 * behaviour exactly.
 *
 * Called once per inference from the NN loop and nowhere else, so the state
 * above needs no lock. */
static bool _nn_count_update(uint32_t raw, uint32_t sustained)
{
  uint32_t window = _nn_debounce_ticks();
  if (sustained < raw) { sustained = raw; }

  /* 0 = no filtering: believe every frame, which is what the raw detector
   * looks like and is occasionally what you want to see. */
  if (window == 0U)
  {
    if (raw == _nn_stable_boxes) { return false; }
    _nn_count_reset(raw);
    return true;
  }

  /* Every look taken while rise evidence is open counts towards the share
   * that has to agree, whether this one agreed or not. */
  if (_nn_rise_hits > 0U)
  {
    _nn_rise_frames++;
  }

  if (raw > _nn_stable_boxes)
  {
    /* Evidence for a rise, carrying the highest count seen while it is
     * collected. Taking the maximum is the whole point: with four people
     * and one flickering, the frames that see 4 are the truthful ones and
     * the frames that see 3 are the misses. */
    if (_nn_rise_hits == 0U)
    {
      _nn_rise_cand   = raw;
      _nn_rise_since  = tx_time_get();
      _nn_rise_frames = 1U;
    }
    else if (raw > _nn_rise_cand)
    {
      _nn_rise_cand = raw;
    }
    _nn_rise_hits++;
  }
  else if (sustained >= _nn_stable_boxes)
  {
    /* The scene agrees with what we believe: abandon any fall in progress.
     *
     * Rise evidence is *not* thrown away here — that was the mistake in the
     * first version. A scene alternating 3,4 against a believed 3 shows one
     * frame of evidence, then a frame that agrees, then more evidence;
     * clearing on the agreeing frame means the second hit never arrives and
     * the rise is starved forever. It is judged on the clock instead. */
    _nn_fall_peak  = 0U;
    _nn_fall_since = tx_time_get();
  }

  /* A window of looks has been taken since the evidence opened, so judge it.
   * Enough of them agreed: adopt the highest count seen while it lasted.
   * Too few: it was an invented box or two, and it is dropped rather than
   * being allowed to accumulate towards a later window. */
  if ((_nn_rise_hits > 0U) &&
      ((int32_t)(tx_time_get() - _nn_rise_since) >= (int32_t)window))
  {
    uint32_t cand   = _nn_rise_cand;
    bool     enough = (_nn_rise_hits >= NN_RISE_CONFIRM_FRAMES) &&
                      ((_nn_rise_hits * 100U) >=
                       (_nn_rise_frames * NN_RISE_CONFIRM_PCT));

    _nn_rise_hits   = 0U;
    _nn_rise_cand   = 0U;
    _nn_rise_frames = 0U;

    if (enough && (cand > _nn_stable_boxes))
    {
      _nn_count_reset(cand);
      return true;
    }
  }

  if (sustained >= _nn_stable_boxes)
  {
    return false;
  }

  /* Even what is still standing is short of the believed count: a fall,
   * believed only if it lasts. Remember the highest count seen while it lasts,
   * so we fall to 3 rather than to the single worst frame's 1 — and remember
   * it from the COUNTED number, because that is what will be reported. */
  if (raw > _nn_fall_peak) { _nn_fall_peak = raw; }

  /* Signed compare, so the wait stays correct across tick wrap. */
  if ((int32_t)(tx_time_get() - _nn_fall_since) >= (int32_t)window)
  {
    uint32_t settled = _nn_fall_peak;
    _nn_count_reset(settled);
    return true;
  }
  return false;
}

/* Floor between automatic photo uploads (action_msk bit2). The JPEG crosses
 * the internal 115200 link to the modem before it goes anywhere, which is
 * 10-15 s for a typical ~95 KB frame, and a room with people moving through
 * it changes far faster. Fixed rather than configurable on purpose: it is a
 * property of the link, not a preference, and another registry field would
 * mean another struct version and another settings reset on upgrade. */
#define NN_AUTO_UPLOAD_MIN_MS     20000U
#define NN_AUTO_UPLOAD_MIN_TICKS  ((NN_AUTO_UPLOAD_MIN_MS / 1000U) * \
                                   TX_TIMER_TICKS_PER_SECOND)

static uint32_t _nn_upload_next     = 0U;  /* tick the next one is allowed */
static uint32_t _nn_uploads_skipped = 0U;  /* dropped by the rate floor    */
static uint32_t _nn_uploads_busy    = 0U;  /* pipeline already busy        */

void nn_task_upload_stats(uint32_t *skipped, uint32_t *busy)
{
  if (skipped != NULL) *skipped = _nn_uploads_skipped;
  if (busy    != NULL) *busy    = _nn_uploads_busy;
}

void nn_task_detect_set(bool enable)
{
  /* Start from a known-empty scene. Without this a `detect stop` taken
   * while people were in view leaves the count believed, and the first
   * arrival after `detect start` reports nothing new. */
  if (enable)
  {
    _nn_count_reset(0U);
    _nn_prev_boxes = 0U;
    _nn_tile_idx   = 0U;      /* a stop/start begins a fresh sweep */
    _nn_tile_pub_n = 0U;
    _nn_people_now = _nn_vehicles_now = 0U;
    _nn_people_rep = _nn_vehicles_rep = 0U;
  }
  _nn_detect_enabled = enable;
}
bool nn_task_detect_get(void)          { return _nn_detect_enabled; }

/* Switching modes abandons whatever sweep was in flight and clears the last
 * published set: a half sweep's boxes are in the coordinates of a frame that
 * is about to stop being how we look at the world, and carrying the old count
 * across would raise a spurious edge on the first frame of the new mode. */
void nn_task_tile_set(bool enable)
{
  if (enable == _nn_tile_mode)
  {
    /* Already in the mode being asked for — but `detect mode tile` is also the
     * documented way to put the merge rules back after an experiment with
     * `tile edgedrop off`, and that has to work from inside tile mode, which
     * is where anyone experimenting already is (ScopusQA #24). Re-arm and
     * leave the sweep in progress alone: nothing about the mode changed. */
    if (enable)
    {
      tile_cfg_for_live();
    }
    return;
  }
  _nn_tile_mode    = enable;
  _nn_tile_idx     = 0U;
  _nn_tile_n       = 0U;
  _nn_tile_pub_n   = 0U;
  _nn_tile_starved = false;
  if (enable)
  {
    /* The factory tile geometry describes a sensor-sized uploaded frame and
     * degenerates on the 800x600 live pipe. Arm the one that belongs to it. */
    tile_cfg_for_live();
  }
  LINFO(TRACE_NN, "detection mode: %s", enable ? "tile" : "default");
}

bool nn_task_tile_get(void)            { return _nn_tile_mode; }

void nn_task_tile_stats(uint32_t *sweeps, uint32_t *last_ms, uint32_t *tiles)
{
  if (sweeps)  *sweeps  = _nn_tile_sweeps;
  if (last_ms) *last_ms = _nn_tile_ms;
  if (tiles)   *tiles   = _nn_tile_n;
}
void nn_task_action_set(uint8_t mask)  { _nn_action_mask = mask; }

/* Changing which classes count changes what the filtered box count *means*,
 * so everything derived from the old mask has to go with it (ScopusQA #8).
 *
 * The per-class reported counts are the ones that bite. `_nn_people_rep` is
 * only updated on a leg the mask has enabled, so people counted under
 * det_msk=0x01 stayed "last reported: 2" all the way through a spell of
 * vehicle-only, and the first frame after switching back to people either
 * announced a change that had happened while nobody was listening or — worse
 * — sat silent through a real arrival because the stale value happened to
 * match. Switching modes is the one thing the tester does by hand between
 * runs, which is why it was found by switching modes.
 *
 * Treated as a fresh start: whatever the new mask sees next is news. */
void nn_task_det_set(uint8_t mask)
{
  if (mask == _nn_det_mask)
  {
    return;
  }
  _nn_det_mask = mask;
  _nn_count_reset(0U);
  _nn_prev_boxes = 0U;
  _nn_people_now = _nn_vehicles_now = 0U;
  _nn_people_rep = _nn_vehicles_rep = 0U;
}

uint8_t nn_task_det_get(void)
{
  return _nn_det_mask;
}

void nn_task_counts_get(uint32_t *people, uint32_t *vehicles)
{
  if (people != NULL)
  {
    *people = _nn_people_now;
  }
  if (vehicles != NULL)
  {
    *vehicles = _nn_vehicles_now;
  }
}

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

/* Opt-in: let an injected frame drive the action path. See the note on
 * nn_task_test_frame_report_set() in the header. */
static volatile bool _nn_test_frame_report = false;

void nn_task_set_test_frame(uint8_t *frame)
{
  _nn_test_frame_deadline = tx_time_get() + NN_TEST_FRAME_TTL_TICKS;
  _nn_test_frame_override = frame;
}

void nn_task_test_frame_report_set(bool enable)
{
  _nn_test_frame_report = enable;
}

bool nn_task_test_frame_report_get(void)
{
  return _nn_test_frame_report;
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

/* Inferences completed on the injected frame. See nn_task_test_frame_seq(). */
static volatile uint32_t _nn_test_frame_seq = 0U;
uint32_t nn_task_test_frame_seq(void)       { return _nn_test_frame_seq; }

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

/* SoW §4.2 bits 4/5 — report people and vehicles under their own reason
 * codes.
 *
 * The detector is person+vehicle (`pv` model, 80 COCO classes, mapped to the
 * two SoW classes by `_class_passes_mask`), but every detection used to be
 * announced as `rsn=0x10` "people" whatever the model saw, so a car raised a
 * people event and `0x20` was never emitted by anything — ScopusQA #5.
 *
 * Each class is sent only when its own count changed, so a person walking
 * through a car park does not re-announce the parked cars on every edge.
 * `rsd` carries that class's count, which is why these are two notifications
 * and not one with `rsn=0x30`: a single event has only one `rsd`.
 *
 * Caller must have checked the report action bit.
 */
static void _nn_report_classes(uint32_t people, uint32_t vehicles)
{
  if ((_nn_det_mask & 0x01U) && (people != _nn_people_rep))
  {
    _nn_people_rep = people;
    shell_notify_emit(NOTIFY_RSN_PEOPLE, people);
  }
  if ((_nn_det_mask & 0x02U) && (vehicles != _nn_vehicles_rep))
  {
    _nn_vehicles_rep = vehicles;
    shell_notify_emit(NOTIFY_RSN_VEHICLE, vehicles);
  }
}

/* SoW test-injection (firmware-side simulate). Sets a synthetic box count
 * + a synthetic person-class box, runs the same on-edge side-effects the
 * inference loop would: snapshot trigger + notification + trace log.
 * Skips the actual NN inference — useful when the camera is out of focus
 * or the lens is dirty, so we want to verify the post-detect chain in
 * isolation. Holds the box-buffer mutex like the real path. */
void nn_task_simulate_detection(uint32_t boxes)
{
  nn_task_simulate_detection_class(boxes, 0);   /* COCO person */
}

void nn_task_simulate_detection_class(uint32_t boxes, int32_t class_index)
{
  /* Clamp here and not only at the shell. `_pp_box_count` is the count every
   * consumer copies out of a `_pp_box_buff[NN_BOXES_MAX_NUM]`, and the live
   * inference path guarantees the invariant in `_pp_publish_objects`; this
   * path used to publish the caller's number verbatim, so `detect simulate
   * 1000` made `nn_get_detections` read ~24 KB out of a 480-byte array and
   * write it into a 480-byte destination — on the shell task's 2 KB stack,
   * and on every camera frame into display_task's `.bss`. Clamping in the
   * setter fixes every caller at once. */
  if (boxes > (uint32_t)NN_BOXES_MAX_NUM) { boxes = (uint32_t)NN_BOXES_MAX_NUM; }

  rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
  _pp_box_count = (size_t)boxes;
  if (boxes > 0U)
  {
    /* Populate one fake person-class box so consumers (nn_get_detections)
     * see something coherent. Only fields the downstream cares about. */
    memset(&_pp_box_buff[0], 0, sizeof(_pp_box_buff[0]));
    _pp_box_buff[0].class_index = class_index;
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
  /* Adopt the simulated count as the believed one, with the hysteresis
   * restarted: a simulate is an assertion about the scene, and the live
   * loop should report the real count as a change away from it rather
   * than immediately re-reporting what was just simulated. */
  _nn_count_reset(boxes);
  _nn_prev_boxes = boxes;

  /* Split the asserted scene the same way a real one is split, so the live
   * loop's next report is a change away from what was simulated rather than a
   * contradiction of it. */
  if (class_index == 0)
  {
    _nn_people_now = boxes; _nn_vehicles_now = 0U;
  }
  else
  {
    _nn_people_now = 0U;    _nn_vehicles_now = boxes;
  }
  _nn_people_rep   = _nn_people_now;
  _nn_vehicles_rep = _nn_vehicles_now;
}

/* COCO-class -> SoW class mapping (proposal W5/W6).
 *   bit0 = person (COCO 0); bit1 = vehicles.
 * Vehicle COCO ids: bicycle 1, car 2, motorcycle 3, bus 5, truck 7, plus
 * the airplane 4 / train 6 / boat 8 bucket (the model occasionally labels
 * a car/truck as one of these; "something on wheels/wings/water" still
 * counts as a vehicle for the W6 signal). */
static bool _class_is_vehicle(int32_t class_index)
{
  return (class_index == 2 || class_index == 1 ||   /* car, bicycle    */
          class_index == 3 || class_index == 5 ||   /* motorcycle, bus */
          class_index == 7 || class_index == 4 ||   /* truck, airplane */
          class_index == 6 || class_index == 8);    /* train, boat     */
}

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
      if (override != NULL)
      {
        _nn_frame = override;                 /* injection wins over tiling  */
        /* Abandon any sweep in flight rather than resume it when the override
         * clears. Its frame snapshot is however many seconds old the
         * injection lasted, and half a sweep of a stale scene merged with
         * half of a live one is a detection of neither. */
        _nn_tile_idx = 0U;
      }
      else if (_nn_tile_mode)
      {
        /* One tile per camera event. The frame is snapshotted once, at the
         * top of the sweep, so every tile describes the same instant rather
         * than 12 successive views of a moving scene. */
        if (_nn_tile_idx == 0U)
        {
          uint16_t fw = 0U, fh = 0U;
          uint8_t *full = tile_capture_live(&fw, &fh);
          if (full == NULL)
          {
            /* Camera not streaming. Say it once per outage, not per frame. */
            if (!_nn_tile_starved)
            {
              _nn_tile_starved = true;
              LWARNING(TRACE_NN, "tile mode: no camera buffer — is the camera "
                                 "streaming? (detection paused)");
            }
            continue;
          }
          _nn_tile_starved = false;
          _nn_tile_n  = tile_sweep_begin(full, fw, fh);
          _nn_tile_t0 = HAL_GetTick();
        }
        tile_sweep_crop(_nn_tile_idx, _nn_tile_in);
        SCB_CleanInvalidateDCache_by_Addr((uint32_t*)_nn_tile_in,
                                          CAMERA_ANCILLARY_BUFFER_SIZE);
        _nn_frame = _nn_tile_in;
      }
      else
      {
        _nn_frame = camera_get_buffer(camera.ancillary.id);
      }
    }
    if (_nn_frame != NULL)
    {
      bool on_test_frame = (_nn_frame == _nn_test_frame_override) &&
                           (_nn_test_frame_override != NULL);
      bool on_tile       = (_nn_frame == _nn_tile_in);
      stat_time_start(STAT_TIME_NN_TOTAL);
      _nn_frame_process();
      stat_time_stop(STAT_TIME_NN_TOTAL);
      if (on_test_frame)
      {
        _nn_test_frame_seq++;
      }

      /* ── Tiled sweep: fold this tile in, and only act on a whole one ──── */
      uint32_t tile_total     = 0U;
      uint32_t tile_sustained = 0U;
      if (on_tile)
      {
        /* Take this tile's raw boxes, then immediately put the publish buffer
         * back to the last completed sweep. Consumers run on their own clocks
         * and must never catch one tile's boxes being read as full-frame
         * coordinates — that is a box in the wrong place on the overlay and,
         * worse, a count that never happened. */
        t_nn_box raw[NN_BOXES_MAX_NUM];
        uint32_t n_raw;

        rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
        n_raw = (uint32_t)_pp_box_count;
        if (n_raw > (uint32_t)NN_BOXES_MAX_NUM) n_raw = (uint32_t)NN_BOXES_MAX_NUM;
        memcpy(raw, _pp_box_buff, (size_t)n_raw * sizeof(t_nn_box));
        memcpy(_pp_box_buff, _nn_tile_pub,
               (size_t)_nn_tile_pub_n * sizeof(t_nn_box));
        _pp_box_count = (size_t)_nn_tile_pub_n;
        rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);

        tile_sweep_collect(_nn_tile_idx, raw, n_raw);

        _nn_tile_idx++;
        if (_nn_tile_idx < _nn_tile_n)
        {
          continue;               /* sweep still in progress — nothing to act on */
        }
        _nn_tile_idx = 0U;

        /* Sweep closed. Cross-tile NMS, then the same class split the
         * single-frame path does — but over the merged survivors. */
        (void)tile_sweep_finish();
        uint32_t n_dets = 0U;
        const t_tile_det *dets = tile_sweep_dets(&n_dets);

        uint32_t people = 0U, vehicles = 0U, pub = 0U;
        uint32_t sustained = 0U;
        for (uint32_t i = 0U; i < n_dets; i++)
        {
          if (!dets[i].sustain) continue;
          if (!_class_passes_mask(dets[i].cls, _nn_det_mask)) continue;

          /* Everything that survived NMS in a class we care about holds the
           * count up; only what cleared the counting floor is a detection. */
          sustained++;
          if (!dets[i].keep) continue;

          if (dets[i].cls == 0)                    { people++; }
          else if (_class_is_vehicle(dets[i].cls)) { vehicles++; }

          /* Publish what fits, in the centre+size form every consumer of
           * _pp_box_buff already speaks. The COUNT below is the untruncated
           * one; this buffer is the overlay's view, and it stays inside its
           * own bounds. */
          if (pub < (uint32_t)NN_BOXES_MAX_NUM)
          {
            t_nn_box *b = &_nn_tile_pub[pub++];
            b->x_center    = (dets[i].x1 + dets[i].x2) * 0.5f;
            b->y_center    = (dets[i].y1 + dets[i].y2) * 0.5f;
            b->width       =  dets[i].x2 - dets[i].x1;
            b->height      =  dets[i].y2 - dets[i].y1;
            b->conf        =  dets[i].conf;
            b->class_index =  dets[i].cls;
          }
        }
        _nn_tile_pub_n   = pub;
        tile_total       = people + vehicles;
        tile_sustained   = sustained;

        rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
        memcpy(_pp_box_buff, _nn_tile_pub, (size_t)pub * sizeof(t_nn_box));
        _pp_box_count    = (size_t)pub;
        _nn_people_now   = people;
        _nn_vehicles_now = vehicles;
        rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);

        _nn_tile_ms = HAL_GetTick() - _nn_tile_t0;
        _nn_tile_sweeps++;

        if (tile_sweep_saturated())
        {
          LWARNING(TRACE_NN, "tile sweep hit the %u-detection cap — counts are "
                             "a floor, not a total", (unsigned)TILE_MAX_DETS);
        }
      }

      /* SoW §4.2 W5/W6: filter detections by class against det_msk.
       * Holds the box-buffer mutex briefly so consumers see a consistent
       * (filtered) view. */
      if (!on_tile)
      {
      rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
      size_t kept = 0U;
      uint32_t people = 0U, vehicles = 0U;
      for (size_t i = 0U; i < _pp_box_count; i++)
      {
        int32_t cls = (int32_t)_pp_box_buff[i].class_index;
        if (_class_passes_mask(cls, _nn_det_mask))
        {
          /* Split the survivors by SoW class as they are kept. §4.2 has a
           * reason code for each (0x10 people, 0x20 vehicle) and the report
           * used to call every detection "people" regardless of what the
           * model actually saw — the detector has been person+vehicle since
           * the pv model went on (ScopusQA #5). */
          if (cls == 0)                 { people++; }
          else if (_class_is_vehicle(cls)) { vehicles++; }
          if (kept != i)
          {
            _pp_box_buff[kept] = _pp_box_buff[i];
          }
          kept++;
        }
      }
      _pp_box_count = kept;
      _nn_people_now   = people;
      _nn_vehicles_now = vehicles;
      rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);
      }

      /* SoW W12 / W11: on a 0->N box-count edge, fire side effects
       * per the action_msk profile. Edge-only so we don't spam SD
       * with one JPEG per frame at 22 Hz. */
      /* In tile mode this is the untruncated survivor count, not the size of
       * the publish buffer — a sweep can find more than NN_BOXES_MAX_NUM. */
      uint32_t cur_boxes = on_tile ? tile_total : (uint32_t)_pp_box_count;
      /* The single-frame path thresholds inside its post-processor and has no
       * sub-threshold set to offer, so it sustains on exactly what it counts. */
      uint32_t cur_sustained = on_tile ? tile_sustained : cur_boxes;

      /* Hysteresis: quick to believe a rise, slow to believe a fall. See
       * the _nn_debounce block above for why the symmetric version of this
       * could sit silent for minutes on a busy scene. */
      bool count_changed = _nn_count_update(cur_boxes, cur_sustained);

      /* Never let an injected picture masquerade as a real detection. The
       * side effects here are the product's outward claims — a JPEG filed
       * as evidence and a notification telling the customer's server that
       * people are present. Both must describe the lens. `frame run` still
       * reports its count on the console, which is what an injection test
       * actually reads, and `detect simulate` is still the explicit way to
       * exercise this path on purpose. */
      if (count_changed && (_nn_action_mask != 0U)
          && (_nn_test_frame_override != NULL)
          && !_nn_test_frame_report)
      {
        LWARNING(TRACE_NN, "%lu detection(s) from the injected test frame — "
                           "not reported (use 'detect simulate' to test the "
                           "notification path)", (unsigned long)cur_boxes);
      }
      else if (count_changed && (_nn_action_mask != 0U))
      {
        /* bits 0 and 2 both want a picture of what was just detected, so
         * build the SoW §7 filename once. Neither fires on the change to
         * zero — there is nobody to photograph. */
        char fname[48];
        if (((_nn_action_mask & 0x05U) != 0U) && (_nn_stable_boxes > 0U))
        {
          t_datetime dt = { 0 };
          (void)bsp_rtc_get_time(&dt);
          uint32_t ser = HAL_GetUIDw0();
          snprintf(fname, sizeof(fname),
                   "%lu_%02u%02u20%02u_%02u%02u%02u.rdy",
                   (unsigned long)ser,
                   (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year,
                   (unsigned)dt.hours, (unsigned)dt.minutes, (unsigned)dt.seconds);
        }

        /* bit0 = save to SD.
         * Atomic claim so we don't clobber a competing producer (shell
         * `photo savesd`) racing on the shared filename buffer. If the
         * pipeline is already busy, drop this one — we can't block
         * inference waiting for SD. */
        if ((_nn_action_mask & 0x01U) && (_nn_stable_boxes > 0U))
        {
          (void)snapshot_request(fname);
        }

        /* bit2 = upload the photo to the remote server, SoW §3.1: "Enable/
         * Disable taking photo and sending to remote server on detection of
         * new objects". The §4.2 mask table only ever defined bits 0 and 1,
         * so this half of §3.1 had no way to be switched on: the event said
         * how many people there were and no picture of them ever left the
         * device unless somebody typed `photo upload` by hand.
         *
         * Same trigger the shell command uses — a slot claim that returns
         * immediately; snapshot_task does the capture, the encode and the
         * SDVR+SENDBIN transfer on its own thread, so inference is never
         * held behind the UART.
         *
         * Rate-limited, because the transfer is the slow part of this
         * product and the scene is not. ~95 KB over the internal 115200
         * link takes 10-15 s, and a room where people come and go changes
         * faster than that; without a floor between uploads the queue would
         * never drain and every later picture would describe a moment long
         * past. One photo per NN_AUTO_UPLOAD_MIN_MS, the rest counted and
         * dropped — a dropped photo is recoverable, a permanently backed-up
         * link is not. */
        if ((_nn_action_mask & 0x04U) && (_nn_stable_boxes > 0U))
        {
          if ((int32_t)(tx_time_get() - _nn_upload_next) >= 0)
          {
            static uint32_t _auto_upload_ref = 0U;
            _auto_upload_ref++;
            /* Unique name, same reason as the shell path — see ScopusQA #4. */
            if (snapshot_request_upload(fname, _auto_upload_ref, fname))
            {
              _nn_upload_next = tx_time_get() + NN_AUTO_UPLOAD_MIN_TICKS;
            }
            else
            {
              _nn_uploads_busy++;   /* pipeline busy — not a rate drop */
            }
          }
          else
          {
            _nn_uploads_skipped++;
          }
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
          /* People (0x10) and vehicles (0x20) only. The §4.2 motion bits are
           * NOT raised here: motion in this product means the *unit* being
           * moved, measured by the inertial sensor (see motion_sensor.c), and
           * wiring them to the scene's box count reported "the box is being
           * carried" every time somebody walked past a stationary camera. */
          _nn_report_classes(_nn_people_now, _nn_vehicles_now);
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
  int32_t kept = 0;
  for (int32_t idx = 0; idx < n; idx++)
  {
    const t_nn_box b = (t_nn_box)(out->pOutBuff[idx]);

    /* A confidence is a probability. The post-processor occasionally hands
     * back a box whose confidence is not one — 1.0066e+38 was caught on the
     * bench, on a 46x59 px box against the bottom of the frame — and such a
     * box is above every threshold there is, by construction: it cannot be
     * filtered by raising one. It is a phantom person, an SD photograph of
     * nothing, and a notification (ScopusQA #24).
     *
     * Written as a rejection of what is NOT in range so that a NaN, which
     * fails every comparison it is given, is rejected too. Degenerate
     * geometry goes with it: a box with no area is not a detection either. */
    if (!(b.conf > 0.0f) || !(b.conf <= 1.0f)) { continue; }
    if (!(b.width > 0.0f) || !(b.height > 0.0f)) { continue; }

    _pp_box_buff[kept++] = b;
  }
  _pp_box_count = (size_t)kept;
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
