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
  bool    keep;      /* survived NMS and is above the confidence floor       */
  bool    sustain;   /* survived NMS, floor or sustain floor — see below     */
  bool    frag;      /* cut by a tile edge that is not a frame edge          */
  bool    rot;       /* came from a step read turned 90 degrees             */
} t_tile_det;

/**
 * @brief Confidence floor for counting, and the lower floor a detection only
 *        has to hold to keep being believed (ScopusQA #24).
 *
 * A person at the edge of what the network is sure about sits near the floor
 * and crosses it both ways between sweeps. Counting on one number turns that
 * into a count that alternates — 5, 6, 5, 6 — and every alternation is a
 * notification about a scene that never moved. Measured on 5_people.jpeg, 10
 * looks at one unchanging frame: the sixth detection appears at conf 0.45-0.50
 * against a 0.45 floor, and the count changed four times.
 *
 * So: `conf` is what a detection must reach to be COUNTED, and
 * `conf_sustain` is what it must hold to stay counted. Nothing below the full
 * floor is ever reported, drawn, or added to a total — the wider set exists
 * only so that the count is slow to come back DOWN, which is the direction
 * this flicker travels.
 */
float    tile_cfg_conf(void);
float    tile_cfg_conf_sustain(void);

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

/**
 * @brief Also look at the picture turned 90 degrees (ScopusQA #26).
 *
 * The network has a strong upright prior: it was trained on people who are
 * standing, and a person lying down is a different object to it. Measured on
 * the bench, one person composited into the same scene at the same pixel
 * size, upright and on their side, whole-frame pass:
 *
 *     scene                     upright   on their side
 *     street, 500 px            found     found, conf 0.63 -> 0.37
 *     street, 240 px            found     MISSED, and read as a car
 *     park,   500 px            found     found
 *     park,   240 px            found     MISSED
 *
 * and the same six pictures turned 90 degrees before the network sees them -
 * which stands the lying people up and lays the standing ones down, reverses
 * the result exactly: every lying person is found, every standing one is lost.
 * So neither orientation is a substitute for the other, and the fix is to run
 * both and merge, which is what this does.
 *
 *   TILE_ROT_OFF   as before, upright only
 *   TILE_ROT_FULL  one extra step: the WHOLE FRAME turned 90 degrees.
 *                  +1 inference in 13 (~8%).
 *   TILE_ROT_AUTO  the rotated whole frame, PLUS a rotated second look at the
 *                  few tiles that hold something a lying person could be.
 *                  0 to 4 extra inferences on top of FULL, and 0 is the
 *                  ordinary case. The default.
 *   TILE_ROT_ALL   every step runs both ways. 2x inferences, so a sweep goes
 *                  from ~1.5 s to ~3 s, and the debounce window is two sweeps,
 *                  so every notification is late by the same factor.
 *
 * Boxes from a rotated step are turned back into frame coordinates before the
 * merge, so nothing downstream, overlay, counts, notifications, knows this
 * happened.
 *
 * Why AUTO exists, when FULL was shipped as the answer to #26 (reopened
 * 2026-09-06: "in off and full mode there is still no detection; in all mode
 * there was detection but it takes a long time")
 * ------------------------------------------------------------------------
 * FULL rotates the whole 800x600 frame into a 256x256 input, a 3.1x downscale.
 * That is enough for a person who fills much of the frame and nothing like
 * enough for one at ordinary range, which is the case QA was pointing at. The
 * per-step measurement, 15 pictures, every step of a sweep run separately and
 * its raw boxes read off (`scopus/step_probe.py`):
 *
 *   best person confidence      upright   rotated whole frame   rotated TILE
 *   ITP's grass scene              0.50            0.50             0.78
 *   ITP's park scene               0.63            0.63             0.78
 *   ITP's street scene             0.71            0.78             0.81
 *   park, person fills 240 px      0.45            MISSED           0.81
 *   street, person fills 240 px    0.55            0.33             0.71
 *   park, person fills 500 px      0.55            0.78             0.81
 *   street, person fills 500 px    0.67            0.71             0.86
 *
 * A rotated tile is worth 0.71 to 0.86 on every one of them; the rotated whole
 * frame is worth 0.00 to 0.78 and twice lands under the 0.45 floor. So the
 * resolution matters at least as much as the rotation, and only ALL had it.
 *
 * What AUTO adds is a trigger, so the resolution can be paid for where it is
 * needed instead of everywhere. A person lying down is reported by the upright
 * pass as a WIDE box - 0.55x0.18, 0.67x0.16, 0.75x0.22 - whatever class it
 * lands on, while a standing person is 0.11x0.54. Over the same 15 pictures:
 *
 *   every lying scene           2 to 5 tiles hold a wide box, and a rotated
 *                               look at one of them finds the person
 *   every upright scene         NO tile holds a wide box: AUTO costs nothing
 *                               and cannot introduce a second count of a
 *                               person it already has
 *
 * That last line is the one that pays for the complexity: ALL runs a rotated
 * pass over standing people too, and on one of these pictures it invented a
 * second person at 0.45 from a man who was already counted.
 */
typedef enum
{
  TILE_ROT_OFF = 0,
  TILE_ROT_FULL,
  TILE_ROT_ALL,
  TILE_ROT_AUTO,      /* appended, not inserted: the registry stores this  */
} t_tile_rot;

void       tile_cfg_set_rotate(t_tile_rot r);
t_tile_rot tile_cfg_get_rotate(void);
const char *tile_rot_name(t_tile_rot r);

/** Most tiles TILE_ROT_AUTO will take a second, rotated look at in one sweep.
 *  Four, because five was the most any of the fifteen measured scenes asked
 *  for and a busy street must not be able to double the sweep by accident. */
#define TILE_ROT_AUTO_MAX   4U

/** How much wider than tall a box has to be before the thing it frames could
 *  be a person lying down. A standing person measures 0.2, a lying one 3 to 6,
 *  and a car 4 or so, the gap is wide enough that this number is not a
 *  tuning parameter. A car triggering a look costs one inference and answers
 *  the question the look is asking, since a lying person at range IS read as
 *  a car (#26, and see the miscount note above). */
#define TILE_ROT_WIDE_RATIO 1.2f

/** How many tiles the last sweep took a second look at, and how many it would
 *  have looked at with no cap, `detect rotate query` prints both, so a scene
 *  that is over the cap says so instead of quietly losing looks. */
void     tile_sweep_relook_stats(uint32_t *done, uint32_t *wanted);

void     tile_cfg_get(uint16_t *cols, uint16_t *rows, uint16_t *crop,
                      uint16_t *ovl_h, uint16_t *ovl_v,
                      float *conf, float *iou);

/** Tiles per sweep — cols * rows. */
uint32_t tile_cfg_count(void);

/**
 * @brief Set everything that belongs to the LIVE main pipe: ScopusQA #22's
 *        4 columns x 3 rows at the NN's own 256 px side, and both merge rules.
 *
 * The factory defaults describe a sensor-sized uploaded frame and degenerate
 * on an 800x600 one — a 576 px crop over a 600 px axis puts all three rows
 * within 24 px of each other. This is the geometry the main path arms itself
 * with, and `tile live` uses it too.
 *
 * It also re-asserts fullpass and edgedrop, because the shell's copies of them
 * are an experiment bench and the main path must not inherit where an
 * experiment happened to stop (ScopusQA #24 — see tile_detect.c for the
 * measured table).
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
 * @brief How many steps this sweep has, RE-READ AFTER EVERY STEP.
 *
 * tile_sweep_begin()'s return value is only the count a sweep starts with.
 * Under TILE_ROT_AUTO the sweep grows when the upright block closes, by the
 * number of tiles it nominated for a rotated second look, so a caller that
 * cached the first number stops the sweep before those steps run and the
 * feature silently does nothing.
 */
uint32_t tile_sweep_steps(void);

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
 *  and skip entries with keep == false. Entries with sustain == true and
 *  keep == false are the sustain set — see tile_cfg_conf_sustain(); they are
 *  not detections and must not be counted, drawn or reported. */
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

/* ── Standing in for the lens ──────────────────────────────────────────── */

/**
 * @brief Have the LIVE tiled sweep read an uploaded picture instead of the
 *        camera, for `ttl_ms` (0 = clear it now).
 *
 * Why this exists. The single-frame override in nn_task wins over tiling: the
 * comment there says so in as many words, and it hands the whole 256x256 input
 * to the network in one pass. So until now an uploaded picture could be run through
 * the OFFLINE sweep (`tile run`, which reports to the console) or through the
 * live single-frame path (`frame run`), and there was NO way to put a known
 * picture through the sweep that actually drives the product: the merge, the
 * count, the debounce, the §4.2 notification.
 *
 * ScopusQA #26 was reopened on exactly that ambiguity. QA reported that a
 * setting did not work on their live scene while the same setting measured
 * correct on the bench, and neither side could run the other's test: the bench
 * had no lying person in front of a lens, and QA had no way to feed the live
 * path a picture. This closes that, the sweep, the counts and the
 * notifications all run, on pixels both sides can hold in a file.
 *
 * The frame is BORROWED, and it must be RGB888 and stay valid until the
 * injection expires. It expires on its own for the same reason the
 * single-frame override does: a unit left injecting is a unit that is not
 * watching anything, and nobody remembers.
 */
void     tile_inject_set(const uint8_t *frame, uint16_t fw, uint16_t fh,
                         uint32_t ttl_ms);

/** The injected frame, or NULL when there is none or it has expired.
 *  Expiry is reported once through `*expired_out` so the caller can say so. */
const uint8_t *tile_inject_get(uint16_t *fw_out, uint16_t *fh_out,
                               bool *expired_out);

/** Seconds left on the injection, 0 when nothing is injected. */
uint32_t tile_inject_left_s(void);

#ifdef __cplusplus
}
#endif

#endif /* TILE_DETECT_H */
