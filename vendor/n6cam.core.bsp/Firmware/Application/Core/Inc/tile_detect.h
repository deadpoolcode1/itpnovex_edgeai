/**
  ******************************************************************************
  * @file    tile_detect.h
  * @brief   Tiled multi-crop detection — geometry, crop/resize, and cross-tile
  *          NMS, shared by the `tile` shell commands and the live NN loop.
  *
  * Why this is a module and not two copies
  * ---------------------------------------
  * Tiling was first written inside shell_task.c, driven synchronously: arm the
  * NN at a crop, block until an inference lands, repeat. That is exactly the
  * wrong shape for the live path — nn_task is the thread that *runs* the
  * inferences, so it cannot block waiting for itself.
  *
  * The two callers therefore need the same arithmetic on different clocks:
  *
  *   shell_task  `tile run` / `tile live` — synchronous, one command, one sweep
  *   nn_task     main-path tiling         — one tile per camera frame event,
  *                                          a sweep spread across ~12 frames
  *
  * So the state lives here and the drive stays with the caller. `tile_sweep_*`
  * is a cursor: begin, then crop/collect per index in any pacing the caller
  * likes, then finish. Nothing in here blocks, allocates, or touches the NN.
  *
  * There is ONE sweep in flight at a time — a single static accumulator, which
  * is what keeps this free of allocation. Callers must not interleave; the
  * shell refuses `tile run` while the live loop owns the engine.
  ******************************************************************************
  */

#ifndef TILE_DETECT_H
#define TILE_DETECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "camera_config.h"
#include "nn_task.h"

/** Maximum tiles along either axis. 12x12 is far past anything useful at
 *  ~90 ms an inference; it exists to bound the origin arrays. */
#define TILE_MAX_AXIS       12U

/** Pre-NMS accumulation cap across the whole sweep. */
#define TILE_MAX_DETS       256U

/** NN input side. The network is fixed at 256x256 and stays that way — a
 *  different side would be a retrain and a model reflash, not a setting. */
#define TILE_NN_SIDE        CAMERA_ANCILLARY_WIDTH

/** A detection after remapping, in full-frame normalized [0,1] CORNER
 *  coordinates. Corners rather than centre+size because NMS is all we do with
 *  them; the conversion back to the t_nn_box centre form happens once, at the
 *  boundary where the result re-enters the normal detection path. */
typedef struct
{
  float   x1, y1, x2, y2;
  float   conf;
  int32_t cls;
  bool    keep;
} t_tile_det;

/* ── Configuration ─────────────────────────────────────────────────────── */

/** Restore the factory geometry (see tile_detect.c for what it is and why). */
void     tile_cfg_default(void);

void     tile_cfg_set_grid(uint16_t cols, uint16_t rows);
void     tile_cfg_set_crop(uint16_t crop_px);
void     tile_cfg_set_overlap(uint16_t ovl_h, uint16_t ovl_v);
void     tile_cfg_set_thresh(float conf, float iou);

/**
 * @brief Also run the WHOLE frame as one extra pass alongside the tiles
 *        (default on).
 *
 * Tiles alone are not strictly better. Measured over the ScopusQA image set,
 * they recover small and distant objects the single downscale loses — and they
 * lose objects large enough to overflow a 256 px tile, because a fragment of a
 * car is either missed or read as something else. The whole-frame pass is the
 * detector that used to run on the main path, so keeping it alongside means
 * tiling can only add. One inference in thirteen.
 */
void     tile_cfg_set_fullpass(bool on);
bool     tile_cfg_get_fullpass(void);

/**
 * @brief Drop a tile detection whose box runs into a tile edge that is not a
 *        frame edge (default on).
 *
 * An object bigger than a tile is cut by the grid and each piece is reported
 * as a whole object; the pieces overlap only along the seam, so their IoU is
 * low and NMS keeps them all. With tiles alone and this off, 5_people.jpeg
 * measured 19 people. A box that stops at a cut is a fragment and is dropped —
 * the object it belongs to is seen whole by a neighbour or by the whole-frame
 * pass. A box that stops at the frame edge is kept: nothing was cut there.
 */
void     tile_cfg_set_edgedrop(bool on);
bool     tile_cfg_get_edgedrop(void);

void     tile_cfg_get(uint16_t *cols, uint16_t *rows, uint16_t *crop,
                      uint16_t *ovl_h, uint16_t *ovl_v,
                      float *conf, float *iou);

/** Tiles per sweep — cols * rows. */
uint32_t tile_cfg_count(void);

/**
 * @brief Set the geometry that belongs to the LIVE main pipe: ScopusQA #22's
 *        4 columns x 3 rows at the NN's own 256 px side.
 *
 * The factory defaults describe a sensor-sized uploaded frame and degenerate
 * on an 800x600 one — a 576 px crop over a 600 px axis puts all three rows
 * within 24 px of each other. This is the geometry the main path arms itself
 * with, and `tile live` uses it too.
 */
void     tile_cfg_for_live(void);

/* ── Geometry ──────────────────────────────────────────────────────────── */

/**
 * @brief Tile origins along one axis.
 *
 * `n` origins for a square `crop` sliding over `span`, from 0 to span-crop.
 * `ovl` 0 means auto: distribute evenly, which covers the whole span and
 * produces overlap automatically whenever crop*n > span. The last origin is
 * forced to the far edge so no strip of the frame is ever unseen.
 */
void     tile_axis_origins(uint16_t n, uint16_t span, uint16_t crop,
                           uint16_t ovl, uint16_t *origins,
                           uint16_t *stride_out);

/* ── Sweep cursor ──────────────────────────────────────────────────────── */

/**
 * @brief Start a sweep over `frame` (RGB888, fw x fh). Resets the accumulator
 *        and computes the grid. Returns the number of tiles to step through.
 *
 * The frame is borrowed, not copied — it must stay valid until the sweep
 * finishes.
 */
uint32_t tile_sweep_begin(const uint8_t *frame, uint16_t fw, uint16_t fh);

/**
 * @brief Render tile `idx` into `dst` (TILE_NN_SIDE^2 * 3 bytes, RGB888).
 *        Bilinear, source coordinates clamped to the frame.
 */
void     tile_sweep_crop(uint32_t idx, uint8_t *dst);

/**
 * @brief Fold `n` boxes produced by the NN for tile `idx` into the sweep.
 *        Boxes below the configured confidence are dropped here; the rest are
 *        remapped from tile-normalized to full-frame-normalized coordinates.
 */
void     tile_sweep_collect(uint32_t idx, const t_nn_box *boxes, uint32_t n);

/**
 * @brief Close the sweep: class-aware IoU-NMS across every tile. Returns the
 *        number of survivors. A person straddling two tiles is one detection
 *        after this, which is the entire point of the overlap.
 */
uint32_t tile_sweep_finish(void);

/** Survivors of the last finished sweep. `*n` is the raw array length; walk it
 *  and skip entries with keep == false. */
const t_tile_det *tile_sweep_dets(uint32_t *n);

/** Diagnostics for the last sweep: every box the NN emitted, and how many
 *  survived the confidence floor. */
void     tile_sweep_stats(uint32_t *raw, uint32_t *over_thresh);

/** True when the accumulator filled up and tiles were truncated — the sweep's
 *  counts are then a floor, not a total, and callers say so. */
bool     tile_sweep_saturated(void);

/* ── Live-frame capture ────────────────────────────────────────────────── */

/**
 * @brief Snapshot the live DCMIPP main pipe into the module's own RGB888
 *        scratch, expanding RGB565, and hand back the buffer.
 *
 * The main pipe carries the full field of view at CAMERA_MAIN_WIDTH x HEIGHT,
 * which is more than the 256x256 ancillary the NN normally eats — that extra
 * resolution is the whole reason tiling finds anything the single downscale
 * misses. Returns NULL when the camera is not streaming.
 *
 * The scratch belongs to this module, so a live sweep no longer clobbers the
 * frame that `tile upload` parked in the firmware-update buffer.
 */
uint8_t *tile_capture_live(uint16_t *fw_out, uint16_t *fh_out);

#ifdef __cplusplus
}
#endif

#endif /* TILE_DETECT_H */
