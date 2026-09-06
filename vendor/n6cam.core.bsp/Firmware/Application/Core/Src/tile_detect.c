/**
  ******************************************************************************
  * @file    tile_detect.c
  * @brief   Tiled multi-crop detection engine. See tile_detect.h for why the
  *          drive stays with the caller and the state lives here.
  ******************************************************************************
  */

#include "tile_detect.h"

#include <string.h>

#include "main.h"          /* IN_PSRAM                                       */
#include "camera_task.h"   /* camera_get_buffer, DCMIPP_PIPE1                */

/* ── Configuration ─────────────────────────────────────────────────────────
 *
 * Two geometries matter and they are not the same.
 *
 * The factory defaults below (5x4, 576 px crops over 2592x1944) mirror the
 * I.T.P. Novex 90-degree-FOV worked example, and they describe a SENSOR-sized
 * frame — the one `tile upload` pushes in over CDC.
 *
 * The live main pipe is CAMERA_MAIN_WIDTH x CAMERA_MAIN_HEIGHT (800x600), and
 * a 576 px crop over a 600 px axis degenerates: three rows would sit at 0, 12
 * and 24 px, three near-identical views of the same picture and no coverage
 * gained. `tile_cfg_for_live()` is the geometry that belongs to that frame —
 * ScopusQA #22's 4 columns x 3 rows at the NN's own 256 px, which tiles 800x600
 * with real overlap and no resampling loss on the crop.
 */
#define TILE_DEF_COLS       5U
#define TILE_DEF_ROWS       4U
#define TILE_DEF_CROP       576U
#define TILE_DEF_OVL_H      0U       /* 0 = auto (even distribution)          */
#define TILE_DEF_OVL_V      0U
#define TILE_DEF_CONF       0.45f
#define TILE_DEF_IOU        0.40f

/* Live-pipe geometry — ScopusQA #22: 12 tiles, 3 rows x 4 columns.
 *
 * The crop was TILE_NN_SIDE, so a tile went into the network at 1:1 with no
 * resampling loss. That is the better tile in isolation and the worse sweep,
 * because 4x256 over 800 px and 3x256 over 600 px leave only ~75 px of overlap
 * on a seam. A person standing on one is cut by the grid, both halves are
 * discarded by the edge rule that #24 needs, and which side of the line they
 * fall on changes with a fraction of a pixel of camera tremor. The count then
 * moves on a scene that has not, and every move is a notification.
 *
 * 320 px keeps the same 12 tiles and the same cost, and buys 160 px of
 * horizontal and 180 px of vertical overlap for a 0.8x downscale into the
 * network. Measured on the bench, same frames replayed with sensor-scale noise
 * and sub-pixel shift:
 *
 *                          crop 256              crop 320
 *   crowd of ~15           9..10, 3 changes/11   9, no change in 11
 *   ScopusQA 3_people_01   3..4,  4 changes/11   4, no change in 11
 *
 * and over the 75-image QA set, on the 22 scenes whose name states a count:
 * 14 exact against 11, total count error 13 against 15, same 61 scenes with
 * any detection at all. It is better on the still pictures and steady on the
 * moving ones, so the 1:1 tile was not worth what it cost. */
#define TILE_LIVE_COLS      4U
#define TILE_LIVE_ROWS      3U
#define TILE_LIVE_CROP      320U

static uint16_t _cfg_cols  = TILE_DEF_COLS;
static uint16_t _cfg_rows  = TILE_DEF_ROWS;
static uint16_t _cfg_crop  = TILE_DEF_CROP;
static uint16_t _cfg_ovl_h = TILE_DEF_OVL_H;
static uint16_t _cfg_ovl_v = TILE_DEF_OVL_V;
static float    _cfg_conf  = TILE_DEF_CONF;
static float    _cfg_iou   = TILE_DEF_IOU;

/* Run the WHOLE frame as one extra pass alongside the tiles.
 *
 * Tiles alone are not strictly better, which the ScopusQA image set showed
 * plainly: they recover small and distant objects the single downscale loses,
 * and they lose objects that overflow a 256 px tile — a car close to the lens
 * is a fragment in every tile that contains it, and a fragment is either
 * missed or misread. The whole-frame pass is exactly the detector that used to
 * run on the main path, so keeping it means tiling can only add.
 *
 * It costs one inference in thirteen. The merge is the same cross-tile NMS:
 * where both passes see the same object, the higher-confidence box wins. */
static bool     _cfg_fullpass = true;

/* Reject a tile detection whose box runs into a tile edge that is not also a
 * frame edge (default on).
 *
 * This is the fix for the way tiling gets counting wrong, and it is worth
 * stating precisely because the failure is not obvious. An object larger than
 * a tile is cut by the grid, and each tile that contains a piece of it reports
 * that piece as a whole object. Two half-people overlap only along the seam,
 * so their IoU is small and cross-tile NMS — which is an IoU test — keeps both.
 * Measured on the ScopusQA set with tiles alone: 5_people.jpeg came back as 19
 * people, 4_people.jpeg as 12, 3_people_01.jpeg as 11.
 *
 * A box that stops at a tile edge is a fragment, so drop it: whatever it is
 * part of is seen whole either by a neighbouring tile (that is what the
 * overlap is for) or by the whole-frame pass. A box that stops at the FRAME
 * edge is not a fragment — the object really does leave the picture there —
 * so those are kept, which is why this tests the two separately. */
static bool     _cfg_edgedrop = true;

/* Also look at the picture turned 90 degrees (ScopusQA #26). See the note on
 * tile_cfg_set_rotate() in the header for the measurement that decided this.
 *
 * TILE_ROT_AUTO by default: the rotated whole frame that TILE_ROT_FULL was,
 * plus a rotated second look at the handful of tiles holding a box wide enough
 * to be a person on the ground. On a scene with nobody lying down that is the
 * same 14 inferences FULL costs; on one with somebody it buys the tile
 * resolution that is what actually finds them, without ALL's doubled sweep and
 * doubled notification delay. */
static t_tile_rot _cfg_rotate = TILE_ROT_AUTO;

/* What fraction of the confidence floor a detection has to hold to keep being
 * believed. 0.75 of the 0.45 default is 0.34 — comfortably below the 0.45-0.50
 * the flickering sixth person in 5_people.jpeg measured at, and comfortably
 * above the noise that a 0.2 floor would let in. See tile_detect.h. */
#define TILE_SUSTAIN_FRAC   0.75f

/* Weak detections may never crowd real ones out of the accumulator: they exist
 * only to slow the count's descent, and a sweep that dropped a real detection
 * to make room for one would be worse off than before. */
#define TILE_WEAK_LIMIT     ((TILE_MAX_DETS * 3U) / 4U)

/* COCO's person. The only class a turned step may contribute, see the note in
 * tile_sweep_collect(). */
#define TILE_CLASS_PERSON   0

/* How close to a tile edge counts as touching it, as a fraction of the tile.
 * Generous on purpose: a detector rarely puts a fragment's box exactly on the
 * cut, and half a person 3 px short of the seam is still half a person. */
#define TILE_EDGE_MARGIN    0.03f

void tile_cfg_default(void)
{
  _cfg_cols  = TILE_DEF_COLS;
  _cfg_rows  = TILE_DEF_ROWS;
  _cfg_crop  = TILE_DEF_CROP;
  _cfg_ovl_h = TILE_DEF_OVL_H;
  _cfg_ovl_v = TILE_DEF_OVL_V;
  _cfg_conf  = TILE_DEF_CONF;
  _cfg_iou   = TILE_DEF_IOU;
  _cfg_fullpass = true;
  _cfg_edgedrop = true;
  _cfg_rotate   = TILE_ROT_AUTO;
}

void tile_cfg_set_grid(uint16_t cols, uint16_t rows)
{
  _cfg_cols = cols;
  _cfg_rows = rows;
}

void tile_cfg_set_crop(uint16_t crop_px)      { _cfg_crop = crop_px; }

void tile_cfg_set_overlap(uint16_t h, uint16_t v)
{
  _cfg_ovl_h = h;
  _cfg_ovl_v = v;
}

float tile_cfg_conf(void)            { return _cfg_conf; }
float tile_cfg_conf_sustain(void)    { return _cfg_conf * TILE_SUSTAIN_FRAC; }

void tile_cfg_set_fullpass(bool on)  { _cfg_fullpass = on; }
bool tile_cfg_get_fullpass(void)     { return _cfg_fullpass; }

void tile_cfg_set_edgedrop(bool on)  { _cfg_edgedrop = on; }
bool tile_cfg_get_edgedrop(void)     { return _cfg_edgedrop; }

void tile_cfg_set_rotate(t_tile_rot r)
{
  _cfg_rotate = (r > TILE_ROT_AUTO) ? TILE_ROT_AUTO : r;
}
t_tile_rot tile_cfg_get_rotate(void) { return _cfg_rotate; }

const char *tile_rot_name(t_tile_rot r)
{
  switch (r)
  {
    case TILE_ROT_OFF:  return "off";
    case TILE_ROT_FULL: return "full";
    case TILE_ROT_ALL:  return "all";
    case TILE_ROT_AUTO: return "auto";
    default:            return "?";
  }
}

void tile_cfg_set_thresh(float conf, float iou)
{
  _cfg_conf = conf;
  _cfg_iou  = iou;
}

void tile_cfg_get(uint16_t *cols, uint16_t *rows, uint16_t *crop,
                  uint16_t *ovl_h, uint16_t *ovl_v, float *conf, float *iou)
{
  if (cols)  *cols  = _cfg_cols;
  if (rows)  *rows  = _cfg_rows;
  if (crop)  *crop  = _cfg_crop;
  if (ovl_h) *ovl_h = _cfg_ovl_h;
  if (ovl_v) *ovl_v = _cfg_ovl_v;
  if (conf)  *conf  = _cfg_conf;
  if (iou)   *iou   = _cfg_iou;
}

uint32_t tile_cfg_count(void)
{
  uint32_t base = (uint32_t)_cfg_cols * (uint32_t)_cfg_rows
                + (_cfg_fullpass ? 1U : 0U);
  switch (_cfg_rotate)
  {
    case TILE_ROT_FULL: return base + 1U;    /* the whole frame, turned      */
    case TILE_ROT_AUTO: return base + 1U;    /* ...and 0..4 more, on demand  */
    case TILE_ROT_ALL:  return base * 2U;    /* everything, both ways        */
    default:            return base;
  }
}

void tile_cfg_for_live(void)
{
  _cfg_cols  = TILE_LIVE_COLS;
  _cfg_rows  = TILE_LIVE_ROWS;
  _cfg_crop  = TILE_LIVE_CROP;
  _cfg_ovl_h = 0U;                  /* auto: even spread over 800x600        */
  _cfg_ovl_v = 0U;

  /* And the thresholds and the two merge rules, which the main path does not
   * get to inherit from whatever the last `tile` experiment left behind
   * (ScopusQA #24).
   *
   * The confidence floor belongs in this list for the plainest reason of all:
   * a `tile thresh 20 40` typed to look at what the network is thinking leaves
   * the live detector counting everything down to 0.20, which is noise, and
   * nothing about the running system says so.
   *
   * They were settled by measurement and they are not preferences: with
   * edgedrop off, every person taller than a 256 px tile is cut by the grid
   * and each piece counted, so 3_people_01.jpeg reads as 9-12 people and the
   * number lands somewhere new on nearly every sweep — a notification each
   * time, which is exactly what ITP reported against a picture that never
   * moved. With fullpass off the opposite failure: every box on that image
   * touches a seam, all of them are dropped, and the sweep reports nobody.
   * Measured on the bench, 12 looks at one unchanging frame:
   *
   *     fullpass off edgedrop off   9..12 people, 9 changes
   *     fullpass on  edgedrop off   9..11 people, 9 changes
   *     fullpass off edgedrop on    0 people,     0 changes
   *     fullpass on  edgedrop on    3 people,     1 excursion to 4
   *
   * The shell keeps both switches so the next person can re-measure that
   * table, and turning one off still works while tiling is live — it now says
   * what it is about to do. What it may not do is decide the product's
   * behaviour by accident. */
  _cfg_conf     = TILE_DEF_CONF;
  _cfg_iou      = TILE_DEF_IOU;
  _cfg_fullpass = true;
  _cfg_edgedrop = true;

  /* Rotation is NOT re-asserted here. It is the one knob in this list that
   * the operator sets deliberately and expects to survive, `detect rotate`
   * persists it in the registry and nn_task restores it at boot, whereas
   * the four above are experiment settings the main path must never inherit
   * (ScopusQA #24). Re-asserting it would quietly undo `detect rotate all`
   * on the next `detect mode tile`. */
}

/* ── Sweep state ───────────────────────────────────────────────────────── */

static const uint8_t *_sw_frame   = NULL;
static uint16_t       _sw_fw      = 0U;
static uint16_t       _sw_fh      = 0U;
static uint16_t       _sw_crop    = 0U;
static uint16_t       _sw_xs[TILE_MAX_AXIS];
static uint16_t       _sw_ys[TILE_MAX_AXIS];
static uint16_t       _sw_cols    = 0U;
static uint16_t       _sw_rows    = 0U;
static uint32_t       _sw_n_acc   = 0U;
static uint32_t       _sw_n_raw   = 0U;
static bool           _sw_sat     = false;
static bool           _sw_full    = false;  /* this sweep has a whole-frame pass */
static uint32_t       _sw_tiles   = 0U;     /* tile count, excluding that pass   */
static t_tile_rot     _sw_rot     = TILE_ROT_OFF; /* rotation this sweep is doing */
static uint32_t       _sw_base    = 0U;     /* steps in the upright block        */

/* TILE_ROT_AUTO's second look (ScopusQA #26, reopened).
 *
 * `_sw_wide[t]` is the widest box the upright block put over tile t, as a
 * width/height ratio; anything at or above TILE_ROT_WIDE_RATIO could be a
 * person on the ground and is worth one rotated inference. `_sw_relook` is the
 * shortlist that survived the cap, filled once the upright block is done, and
 * `_sw_n_wanted` is how long the list would have been without the cap, a
 * number worth reporting rather than hiding, because it is the one that says
 * the scene is over budget. */
static float          _sw_wide[TILE_MAX_AXIS * TILE_MAX_AXIS];
static uint16_t       _sw_relook[TILE_ROT_AUTO_MAX];
static uint32_t       _sw_n_relook  = 0U;
static uint32_t       _sw_n_wanted  = 0U;
static bool           _sw_auto_done = false;

/* The same two numbers, latched, for anyone asking from outside the sweep.
 * The live loop starts the next sweep the moment it finishes one, so a command
 * that read the working counters would nearly always catch them at zero, part
 * way through the upright block, and report that no second look was taken on a
 * unit taking one every sweep. */
static uint32_t       _sw_last_relook = 0U;
static uint32_t       _sw_last_wanted = 0U;

static t_tile_det     _sw_dets[TILE_MAX_DETS];

/* Live-frame scratch. RGB888 expansion of the DCMIPP main pipe — its own
 * buffer rather than a loan of the firmware-update stash, so a live sweep and
 * an uploaded frame can coexist and neither has to know about the other.
 *
 * IN_PSRAM_HI, not IN_PSRAM. Under the old single PSRAM region this buffer
 * landed at 0x919ff200 and so straddled the model's pool at 0x91a00000: all
 * but its first 3584 bytes sat on the NN's working memory, a tiled sweep
 * wrote a picture through the model's buffers on every pass, and rows 2 to
 * 220 of a `frame grab live` came back as float32 activations. Above the pool
 * it collides with nothing. */
static __ALIGN_BEGIN uint8_t
  _live_rgb[(uint32_t)CAMERA_MAIN_WIDTH * CAMERA_MAIN_HEIGHT * 3U]
  __ALIGN_END IN_PSRAM_HI;

/* ── Geometry ──────────────────────────────────────────────────────────── */

void tile_axis_origins(uint16_t n, uint16_t span, uint16_t crop,
                       uint16_t ovl, uint16_t *origins, uint16_t *stride_out)
{
  uint16_t c    = (crop > span) ? span : crop;
  uint16_t last = (uint16_t)(span - c);

  if (n <= 1U)
  {
    origins[0]  = 0U;
    *stride_out = 0U;
    return;
  }

  uint32_t stride;
  if (ovl > 0U)
  {
    stride = (c > ovl) ? (uint32_t)(c - ovl) : 1U;
  }
  else
  {
    stride = (uint32_t)last / (uint32_t)(n - 1U);
    if (stride == 0U) stride = 1U;
  }

  for (uint16_t i = 0U; i < n; i++)
  {
    uint32_t o = (uint32_t)i * stride;
    if (o > last) o = last;
    origins[i] = (uint16_t)o;
  }
  origins[n - 1U] = last;   /* guarantee the last tile reaches the far edge  */
  *stride_out = (uint16_t)stride;
}

/* ── Crop + resize ─────────────────────────────────────────────────────── */

/* One bilinear sample of an RGB888 frame at (fx, fy), clamped so we never read
 * outside it. Its own function because the upright and rotated walks visit the
 * source in different orders and there must be exactly one copy of the
 * interpolation for them to agree. */
static void _bilinear(const uint8_t *src, uint16_t fw, uint16_t fh,
                      float fx, float fy, uint8_t *d)
{
  uint32_t x0 = (uint32_t)fx;
  if (x0 > (uint32_t)(fw - 2U)) x0 = fw - 2U;
  uint32_t y0 = (uint32_t)fy;
  if (y0 > (uint32_t)(fh - 2U)) y0 = fh - 2U;
  const float wx = fx - (float)x0;
  const float wy = fy - (float)y0;

  const uint8_t *p00 = src + ((size_t)y0 * fw + x0) * 3U;
  const uint8_t *p01 = p00 + 3U;
  const uint8_t *p10 = p00 + (size_t)fw * 3U;
  const uint8_t *p11 = p10 + 3U;

  for (uint32_t c = 0U; c < 3U; c++)
  {
    float top = (float)p00[c] * (1.0f - wx) + (float)p01[c] * wx;
    float bot = (float)p10[c] * (1.0f - wx) + (float)p11[c] * wx;
    float v   = top * (1.0f - wy) + bot * wy;
    d[c] = (uint8_t)(v + 0.5f);
  }
}

/* Bilinear-resize the region [cx,cy, cw x ch] of an RGB888 frame into the
 * TILE_NN_SIDE^2 NN input, optionally turning it 90 degrees clockwise on the
 * way (ScopusQA #26). */
static void _resize_crop(const uint8_t *src, uint16_t fw, uint16_t fh,
                         uint16_t cx, uint16_t cy,
                         uint16_t cw, uint16_t ch, bool rot, uint8_t *dst)
{
  const uint32_t N     = TILE_NN_SIDE;
  /* Separate steps per axis: a tile is square, but the whole-frame pass is
   * not, and it has to squash 800x600 into 256x256 exactly the way the
   * ancillary pipe does — otherwise it is not the comparison it claims. */
  const float    stepx = (cw > 1U) ? (float)(cw - 1U) / (float)(N - 1U) : 0.0f;
  const float    stepy = (ch > 1U) ? (float)(ch - 1U) / (float)(N - 1U) : 0.0f;

  if (!rot)
  {
    for (uint32_t oy = 0U; oy < N; oy++)
    {
      const float fy = (float)cy + (float)oy * stepy;
      for (uint32_t ox = 0U; ox < N; ox++)
      {
        _bilinear(src, fw, fh, (float)cx + (float)ox * stepx, fy,
                  dst + ((size_t)oy * N + ox) * 3U);
      }
    }
    return;
  }

  /* Turned 90 degrees CLOCKWISE (ScopusQA #26). The source's bottom-left
   * corner becomes the destination's top-left, so for destination (ox, oy):
   *
   *     u = oy / (N-1)          along the source's width
   *     v = 1 - ox / (N-1)      down the source's height
   *
   * tile_sweep_collect() inverts exactly this to put the boxes back. Keep
   * the two together: a mismatch draws boxes in the wrong place and nothing
   * but the overlay would show it. */
  for (uint32_t oy = 0U; oy < N; oy++)
  {
    const float fx = (float)cx + (float)oy * stepx;
    for (uint32_t ox = 0U; ox < N; ox++)
    {
      const float fy = (float)cy + (float)((N - 1U) - ox) * stepy;
      _bilinear(src, fw, fh, fx, fy, dst + ((size_t)oy * N + ox) * 3U);
    }
  }
}

/* ── NMS ───────────────────────────────────────────────────────────────── */

static float _iou_of(const t_tile_det *a, const t_tile_det *b)
{
  float ix1 = (a->x1 > b->x1) ? a->x1 : b->x1;
  float iy1 = (a->y1 > b->y1) ? a->y1 : b->y1;
  float ix2 = (a->x2 < b->x2) ? a->x2 : b->x2;
  float iy2 = (a->y2 < b->y2) ? a->y2 : b->y2;
  float iw  = ix2 - ix1;
  float ih  = iy2 - iy1;
  if (iw <= 0.0f || ih <= 0.0f) return 0.0f;

  float inter = iw * ih;
  float ua    = (a->x2 - a->x1) * (a->y2 - a->y1)
              + (b->x2 - b->x1) * (b->y2 - b->y1) - inter;
  return (ua > 0.0f) ? (inter / ua) : 0.0f;
}

/* Greedy class-aware NMS over the first `n` accumulated entries. O(n^2) with
 * n <= TILE_MAX_DETS.
 *
 * Suppression runs over `sustain`, the wider set, and `keep` arrives carrying
 * whether the box cleared the counting floor. Weak boxes can only ever be
 * suppressed, never suppress: the survivor of an overlap is the more confident
 * one, and a weak box is by definition the less confident of the pair. */
static void _nms(uint32_t n, float iou_th)
{
  for (uint32_t i = 0U; i < n; i++)
  {
    if (!_sw_dets[i].sustain) continue;
    for (uint32_t j = i + 1U; j < n; j++)
    {
      if (!_sw_dets[j].sustain) continue;
      if (_sw_dets[j].cls != _sw_dets[i].cls) continue;
      if (_iou_of(&_sw_dets[i], &_sw_dets[j]) > iou_th)
      {
        /* A whole box beats a fragment whatever the confidences say. The
         * fragment is a piece of something and its box describes the piece,
         * so letting a confident fragment suppress the box that actually
         * frames the object would keep the count and lose the geometry, and
         * an overlay drawn on half a person is how this looked on the bench.
         * Between two of a kind, the more confident one wins as before. */
        const bool i_frag = _sw_dets[i].frag;
        const bool j_frag = _sw_dets[j].frag;
        const bool j_wins = (i_frag != j_frag)
                          ? i_frag
                          : (_sw_dets[j].conf > _sw_dets[i].conf);
        if (j_wins)
        {
          _sw_dets[i].sustain = false;
          break;                 /* i is gone; stop comparing it             */
        }
        _sw_dets[j].sustain = false;
      }
    }
  }

  /* A detection is counted only if it survived AND cleared the full floor. */
  for (uint32_t i = 0U; i < n; i++)
  {
    _sw_dets[i].keep = _sw_dets[i].keep && _sw_dets[i].sustain;
  }
}

/* ── Sweep cursor ──────────────────────────────────────────────────────── */

/* The rectangle step `idx` looks at. Steps [0, _sw_tiles) are grid tiles;
 * the last step, when the full pass is on, is the whole frame. Having one
 * function answer this keeps crop and collect from ever disagreeing about
 * which pixels a detection came from — which would put boxes in the wrong
 * place and be invisible until someone looked at the overlay. */
static void _step_rect(uint32_t idx, uint16_t *cx, uint16_t *cy,
                       uint16_t *cw, uint16_t *ch, bool *rot, bool *is_tile)
{
  /* Steps [0, _sw_base) are the upright block. What follows depends on the
   * rotation setting (ScopusQA #26):
   *
   *   TILE_ROT_FULL  exactly one more step, the whole frame, turned.
   *   TILE_ROT_ALL   a second copy of the whole block, every step turned.
   *
   * ROT_FULL adds its whole-frame step whether or not `fullpass` is on: the
   * two switches answer different questions, and a unit with fullpass off
   * should still be able to see a person lying down. */
  bool     turned = false;
  uint32_t step   = idx;

  if (idx >= _sw_base)
  {
    turned = true;
    if (_sw_rot == TILE_ROT_ALL)
    {
      step = idx - _sw_base;
    }
    else if ((_sw_rot == TILE_ROT_AUTO) && (idx > _sw_base))
    {
      /* The rotated whole frame first, exactly as FULL does it, then the
       * shortlist. Modulo so a step index that outran the list cannot read
       * past it, it cannot happen, and a crop of the wrong tile would be
       * invisible in every output. */
      const uint32_t k = (idx - _sw_base - 1U) % TILE_ROT_AUTO_MAX;
      step = (k < _sw_n_relook) ? (uint32_t)_sw_relook[k] : _sw_tiles;
    }
    else
    {
      step = _sw_tiles;
    }
  }

  if (rot) { *rot = turned; }

  const bool whole = (step >= _sw_tiles);
  if (is_tile) { *is_tile = !whole; }

  if (whole)
  {
    *cx = 0U; *cy = 0U; *cw = _sw_fw; *ch = _sw_fh;
    return;
  }
  uint16_t c = (uint16_t)(step % _sw_cols);
  uint16_t r = (uint16_t)(step / _sw_cols);
  if (r >= _sw_rows) { r = (uint16_t)(_sw_rows - 1U); }
  *cx = _sw_xs[c]; *cy = _sw_ys[r];
  *cw = _sw_crop;  *ch = _sw_crop;
}

/* Steps in this sweep, upright block plus whatever rotation adds. */
static uint32_t _sweep_steps(void)
{
  switch (_sw_rot)
  {
    case TILE_ROT_FULL: return _sw_base + 1U;
    case TILE_ROT_AUTO: return _sw_base + 1U + _sw_n_relook;
    case TILE_ROT_ALL:  return _sw_base * 2U;
    default:            return _sw_base;
  }
}

/* ── TILE_ROT_AUTO: where a second, rotated look is worth an inference ───
 *
 * The upright pass does not go quiet over a person lying down. It reports a
 * box that is far wider than it is tall, and it puts some class or other on
 * it, person, car, boat, whichever, the class is the part it gets wrong. So
 * the trigger is the SHAPE and not the class: mark every tile a wide box
 * touches, and take a rotated look at the best few once the upright block is
 * done. Measured over the fifteen #26 pictures: every lying scene marks 2 to 5
 * tiles and a rotated look at one of them finds the person at 0.63 to 0.86;
 * every upright scene marks none, so the ordinary case pays nothing.
 *
 * One look per box, not one per tile the box touches. The live tiles overlap
 * by half, so a person on the ground sits wholly inside three or four of them
 * at once, and marking all of them would spend the whole budget looking at the
 * same body from almost the same place. The tile that frames the box BEST wins
 * it: most of the box inside, and, between tiles that hold all of it, the one
 * whose centre the box is nearest, which is the one least likely to cut it at
 * a seam when it is looked at again.
 *
 * A box nominates from wherever it was seen, including the whole-frame pass,
 * so an object too big for the tile that reported it still gets looked at in
 * the tile it actually lies in. */
static void _auto_mark(uint16_t cx, uint16_t cy, uint16_t cw, uint16_t ch,
                       const t_nn_box *b)
{
  const float bw = b->width  * (float)cw;      /* the step's px, = frame px */
  const float bh = b->height * (float)ch;
  if ((bh <= 0.0f) || (bw <= 0.0f)) return;

  const float ratio = bw / bh;
  if (ratio < TILE_ROT_WIDE_RATIO) return;

  const float bx = (float)cx + b->x_center * (float)cw;
  const float by = (float)cy + b->y_center * (float)ch;
  const float x1 = bx - bw * 0.5f, x2 = bx + bw * 0.5f;
  const float y1 = by - bh * 0.5f, y2 = by + bh * 0.5f;
  const float area = bw * bh;

  uint32_t best      = _sw_tiles;
  float    best_in   = 0.0f;
  float    best_dist = 0.0f;

  for (uint32_t t = 0U; t < _sw_tiles; t++)
  {
    const float tx1 = (float)_sw_xs[t % _sw_cols];
    const float ty1 = (float)_sw_ys[t / _sw_cols];
    const float tx2 = tx1 + (float)_sw_crop;
    const float ty2 = ty1 + (float)_sw_crop;

    const float iw = ((x2 < tx2) ? x2 : tx2) - ((x1 > tx1) ? x1 : tx1);
    const float ih = ((y2 < ty2) ? y2 : ty2) - ((y1 > ty1) ? y1 : ty1);
    if ((iw <= 0.0f) || (ih <= 0.0f)) continue;

    const float inside = (iw * ih) / area;
    const float dx     = bx - (tx1 + (float)_sw_crop * 0.5f);
    const float dy     = by - (ty1 + (float)_sw_crop * 0.5f);
    const float dist   = (dx * dx) + (dy * dy);

    /* More of the box first, and only then nearer its middle: a tile that
     * holds all of it beats one that holds most of it, however well centred
     * the second is. The 0.01 is there so two tiles that both hold the whole
     * box are decided on distance rather than on float noise. */
    const bool better = (best == _sw_tiles) ||
                        (inside > best_in + 0.01f) ||
                        ((inside > best_in - 0.01f) && (dist < best_dist));
    if (better)
    {
      best      = t;
      best_in   = inside;
      best_dist = dist;
    }
  }

  if ((best < _sw_tiles) && (ratio > _sw_wide[best]))
  {
    _sw_wide[best] = ratio;
  }
}

/* Turn the marks into the shortlist, widest first, capped. Widest first
 * because the ratio is the evidence: a car is wide, a person on the ground is
 * wider still relative to a standing one, and when a scene offers more
 * candidates than the budget the flattest boxes are the ones to spend it on.
 * Called once, when the upright block closes. */
static void _auto_finalize(void)
{
  if (_sw_auto_done) return;
  _sw_auto_done = true;

  _sw_n_relook = 0U;
  _sw_n_wanted = 0U;

  for (uint32_t t = 0U; t < _sw_tiles; t++)
  {
    if (_sw_wide[t] <= 0.0f) continue;
    _sw_n_wanted++;
  }

  while (_sw_n_relook < TILE_ROT_AUTO_MAX)
  {
    uint32_t best = _sw_tiles;
    for (uint32_t t = 0U; t < _sw_tiles; t++)
    {
      if (_sw_wide[t] <= 0.0f) continue;
      if ((best == _sw_tiles) || (_sw_wide[t] > _sw_wide[best])) best = t;
    }
    if (best == _sw_tiles) break;

    _sw_relook[_sw_n_relook++] = (uint16_t)best;
    _sw_wide[best] = 0.0f;              /* taken */
  }

  _sw_last_relook = _sw_n_relook;
  _sw_last_wanted = _sw_n_wanted;
}

void tile_sweep_relook_stats(uint32_t *done, uint32_t *wanted)
{
  if (done)   *done   = _sw_last_relook;
  if (wanted) *wanted = _sw_last_wanted;
}

uint32_t tile_sweep_begin(const uint8_t *frame, uint16_t fw, uint16_t fh)
{
  uint16_t crop = _cfg_crop;        /* clamp so a crop can never exceed the   */
  if (crop > fw) crop = fw;         /* frame — that axis becomes one region   */
  if (crop > fh) crop = fh;

  _sw_frame = frame;
  _sw_fw    = fw;
  _sw_fh    = fh;
  _sw_crop  = crop;
  _sw_cols  = _cfg_cols;
  _sw_rows  = _cfg_rows;
  _sw_n_acc = 0U;
  _sw_n_raw = 0U;
  _sw_sat   = false;

  uint16_t sx = 0U, sy = 0U;
  tile_axis_origins(_sw_cols, fw, crop, _cfg_ovl_h, _sw_xs, &sx);
  tile_axis_origins(_sw_rows, fh, crop, _cfg_ovl_v, _sw_ys, &sy);

  _sw_tiles = (uint32_t)_sw_cols * (uint32_t)_sw_rows;
  _sw_full  = _cfg_fullpass;
  _sw_rot   = _cfg_rotate;
  _sw_base  = _sw_tiles + (_sw_full ? 1U : 0U);

  memset(_sw_wide, 0, sizeof(_sw_wide));
  _sw_n_relook  = 0U;
  _sw_n_wanted  = 0U;
  _sw_auto_done = false;
  if (_sw_rot != TILE_ROT_AUTO)
  {
    _sw_last_relook = 0U;             /* nothing to report in the other modes */
    _sw_last_wanted = 0U;
  }

  return _sweep_steps();
}

uint32_t tile_sweep_steps(void) { return _sweep_steps(); }

void tile_sweep_crop(uint32_t idx, uint8_t *dst)
{
  if ((_sw_frame == NULL) || (_sw_cols == 0U)) return;
  if (idx >= _sweep_steps()) return;

  uint16_t cx, cy, cw, ch;
  bool     rot;
  _step_rect(idx, &cx, &cy, &cw, &ch, &rot, NULL);
  _resize_crop(_sw_frame, _sw_fw, _sw_fh, cx, cy, cw, ch, rot, dst);
}

void tile_sweep_collect(uint32_t idx, const t_nn_box *boxes, uint32_t n)
{
  if (_sw_cols == 0U) return;
  if (idx >= _sweep_steps()) return;

  /* A step that found nothing is still a step, and it still closes the upright
   * block. Returning early on it used to skip the shortlist below, so on a
   * scene whose LAST upright step happened to be empty, which is most quiet
   * scenes, `detect rotate auto` took no second look at all and there was
   * nothing anywhere to say so. */
  if (boxes == NULL) { n = 0U; }

  uint16_t cx, cy, cw, ch;
  bool     rot, is_tile;
  _step_rect(idx, &cx, &cy, &cw, &ch, &rot, &is_tile);

  const float inv_fw = 1.0f / (float)_sw_fw;
  const float inv_fh = 1.0f / (float)_sw_fh;

  const float sustain_floor = tile_cfg_conf_sustain();

  for (uint32_t i = 0U; i < n; i++)
  {
    _sw_n_raw++;

    /* Nominate tiles for a rotated second look BEFORE any floor is applied:
     * a person on the ground is exactly the detection that sits under the
     * counting floor, and a trigger that only fired on boxes already good
     * enough to count would fire only where it was not needed. */
    if ((_sw_rot == TILE_ROT_AUTO) && !rot && !_sw_auto_done)
    {
      _auto_mark(cx, cy, cw, ch, &boxes[i]);
    }

    if (boxes[i].conf < sustain_floor) continue;

    /* A turned step contributes PEOPLE and nothing else.
     *
     * Rotation is not a general second opinion, it is a correction for one
     * specific thing the network does: it was trained on people standing up,
     * so it reads a person on the ground as some other object, or as nothing.
     * A car is not more canonical on its side, a turned view of one is a view
     * the network never trained on, and what comes back is noise wearing a
     * class name. Measured over the 77-image QA set: every disagreement `auto`
     * had with `off` was a turned step inventing a vehicle over something the
     * upright pass had already counted, a car returning as a "train", a
     * person and a car together returning as a "motorcycle" spanning both, a
     * "bicycle" in the corner of a crowd. Not one was a real object the
     * upright pass had missed.
     *
     * So the turned steps are held to what they are for. This also cleans up
     * `full`, which has been adding that bicycle to crowd_13 since it
     * shipped. */
    if (rot && (boxes[i].class_index != TILE_CLASS_PERSON)) continue;

    /* Undo the 90-degree clockwise turn _resize_crop applied, so everything
     * below works in the step's own upright coordinates and neither the edge
     * rule nor the remap has to know a rotated pass exists. Inverse of
     * (u, v) = (q, 1 - p): centre swaps axes, width and height swap. */
    float b_xc = boxes[i].x_center;
    float b_yc = boxes[i].y_center;
    float b_w  = boxes[i].width;
    float b_h  = boxes[i].height;
    if (rot)
    {
      const float xc = b_yc;
      const float yc = 1.0f - b_xc;
      const float w  = b_h;
      const float h  = b_w;
      b_xc = xc; b_yc = yc; b_w = w; b_h = h;
    }

    /* Below the full floor this is not a detection — it is only evidence that
     * something the sweep already counted has not gone away. Admitted, marked,
     * and never counted. */
    const bool weak = (boxes[i].conf < _cfg_conf);
    if (weak && (_sw_n_acc >= TILE_WEAK_LIMIT)) continue;

    /* Fragment marking, see the note on _cfg_edgedrop. Only tiles; the
     * whole-frame pass has no cut edges, only frame edges.
     *
     * This used to `continue`, and dropping the fragment outright is what
     * ScopusQA #25 turned out to be: a person WIDER THAN THE OVERLAP can be
     * cut by every tile that sees him, so every copy is a fragment, all of
     * them are discarded, and he vanishes from a picture the network reads at
     * 0.84 when the same tile is handed to it directly. Measured on the bench
     * print: 245 px wide over a 160 px overlap, found 0/6 sweeps with the
     * shipped settings.
     *
     * Simply not dropping is worse, and that was measured too: over the
     * 77-image QA set, `edgedrop off` takes the total count error from 32 to
     * 117 and the exact counts from 12 to 5, because each piece of a big
     * object is then counted as a whole one (5_people.jpeg reads 13).
     *
     * So a fragment is now admitted as EVIDENCE and never as a count: it can
     * hold a count up and it takes part in NMS, but `keep` is false, so on its
     * own it adds nothing. _rescue_fragments() below is what promotes one,
     * and only when nothing whole accounts for it. */
    bool frag = false;
    if (is_tile && _cfg_edgedrop)
    {
      const float m  = TILE_EDGE_MARGIN;
      const float x1 = b_xc - b_w * 0.5f;
      const float x2 = b_xc + b_w * 0.5f;
      const float y1 = b_yc - b_h * 0.5f;
      const float y2 = b_yc + b_h * 0.5f;

      frag = (((x1 < m)        && (cx > 0U))            ||
              ((x2 > 1.0f - m) && ((uint32_t)cx + cw < (uint32_t)_sw_fw)) ||
              ((y1 < m)        && (cy > 0U))            ||
              ((y2 > 1.0f - m) && ((uint32_t)cy + ch < (uint32_t)_sw_fh)));
    }

    /* Fragments are held to the same budget as weak boxes: neither may crowd
     * a real detection out of the accumulator. */
    if (frag && (_sw_n_acc >= TILE_WEAK_LIMIT)) continue;

    if (_sw_n_acc >= TILE_MAX_DETS) { _sw_sat = true; continue; }

    /* step-normalized -> step px -> full-frame px -> full-frame normalized */
    float bx = (float)cx + b_xc * (float)cw;
    float by = (float)cy + b_yc * (float)ch;
    float bw = b_w * (float)cw;
    float bh = b_h * (float)ch;

    t_tile_det *d = &_sw_dets[_sw_n_acc++];
    d->x1   = (bx - bw * 0.5f) * inv_fw;
    d->y1   = (by - bh * 0.5f) * inv_fh;
    d->x2   = (bx + bw * 0.5f) * inv_fw;
    d->y2   = (by + bh * 0.5f) * inv_fh;
    d->conf    = boxes[i].conf;
    d->cls     = boxes[i].class_index;
    d->frag    = frag;
    d->rot     = rot;
    d->keep    = !weak && !frag;
    d->sustain = true;
  }

  /* The upright block has just closed, so the shortlist is complete and the
   * sweep can grow by however many looks it earned. Callers re-read
   * tile_sweep_steps() every step for exactly this reason. */
  if ((_sw_rot == TILE_ROT_AUTO) && (idx + 1U >= _sw_base))
  {
    _auto_finalize();
  }
}

/* What fraction of `a` lies inside `b`. Not IoU: a fragment is a PART of the
 * object, so it is small next to the whole box that explains it and their IoU
 * is low even when one completely contains the other. Containment is the
 * question being asked here, and IoU answers a different one. */
static float _inside_of(const t_tile_det *a, const t_tile_det *b)
{
  float ix1 = (a->x1 > b->x1) ? a->x1 : b->x1;
  float iy1 = (a->y1 > b->y1) ? a->y1 : b->y1;
  float ix2 = (a->x2 < b->x2) ? a->x2 : b->x2;
  float iy2 = (a->y2 < b->y2) ? a->y2 : b->y2;
  float iw  = ix2 - ix1;
  float ih  = iy2 - iy1;
  if ((iw <= 0.0f) || (ih <= 0.0f)) return 0.0f;

  float area_a = (a->x2 - a->x1) * (a->y2 - a->y1);
  return (area_a > 0.0f) ? ((iw * ih) / area_a) : 0.0f;
}

/* Two fragments of the same object, cut apart by the same seam.
 *
 * Containment cannot answer this. Each half of a car cut down the middle holds
 * only the seam's width of the other, so neither is inside the other and both
 * would be promoted: one car counted twice. What does distinguish the halves
 * of one object from two objects standing side by side is that the halves
 * OVERLAP ALONG THE SEAM and line up across it - they share almost all of
 * their height when the seam is vertical, almost all of their width when it is
 * horizontal - while two neighbours cut by the same seam do not overlap at all.
 *
 * So: the boxes must touch, and along one axis one must cover most of the
 * other. */
#define TILE_STITCH_FRAC  0.60f

static bool _stitches_with(const t_tile_det *a, const t_tile_det *b)
{
  const float ox = ((a->x2 < b->x2) ? a->x2 : b->x2)
                 - ((a->x1 > b->x1) ? a->x1 : b->x1);
  const float oy = ((a->y2 < b->y2) ? a->y2 : b->y2)
                 - ((a->y1 > b->y1) ? a->y1 : b->y1);
  if ((ox <= 0.0f) || (oy <= 0.0f)) return false;   /* they do not even touch */

  const float aw = a->x2 - a->x1, bw = b->x2 - b->x1;
  const float ah = a->y2 - a->y1, bh = b->y2 - b->y1;
  const float min_w = (aw < bw) ? aw : bw;
  const float min_h = (ah < bh) ? ah : bh;

  return ((min_h > 0.0f) && ((oy / min_h) >= TILE_STITCH_FRAC))
      || ((min_w > 0.0f) && ((ox / min_w) >= TILE_STITCH_FRAC));
}

/* Two views of one object, when one of the views was turned (ScopusQA #26).
 *
 * A turned step is a SECOND LOOK at pixels the upright block already read, so
 * anything it finds where the upright pass already found something is the same
 * object seen better, not another one. NMS alone does not say so: the two
 * boxes frame the same person differently, one flat and one upright-ish, and
 * on ITP's street scene they measured IoU 0.35 against a 0.40 threshold, so
 * both survived and one man was counted twice.
 *
 * Containment answers it where IoU cannot, for the same reason it does for
 * fragments: the question is "is this the thing we already have", and two
 * boxes can share most of one of them while sharing little of their union.
 *
 * MUTUAL containment, though, and that is not a detail. Two views of one
 * person frame it at about the same size, so each holds most of the other. A
 * one-directional test says yes to any small box that falls inside a big one,
 * and on `crowd_13.jpg` that ate a person: a 0.45 detection sitting inside a
 * larger turned box was retired into it, 5 people to 4, on every sweep.
 *
 * Restricted to pairs where at least one side was turned, so a sweep with
 * rotation off, or one where nothing was nominated, merges exactly as it did
 * before this existed. */
#define TILE_ROT_SAME_FRAC   0.40f

/* And the same object under two different CLASS names.
 *
 * A turned look does not only move a box, it can rename it: over the 77-image
 * QA set, `auto` added a vehicle on six pictures and every one of them was a
 * car the upright pass had already found, coming back from the turned look as
 * a "train" at 0.45 over a box 93% identical to the car's at 0.84. Class-aware
 * NMS cannot merge those, by design, since two classes usually mean two
 * objects, so one car was counted twice.
 *
 * The class is the part this network gets wrong about a turned object; that is
 * the whole finding behind #26, where a person on the ground comes back as a
 * car. So when one of the two views is turned and the boxes are all but the
 * same box, they are one object and the more confident view names it. Which
 * fixes the miscount in both directions: the car stays a car at 0.84, and a
 * person the upright pass called a car at 0.50 becomes the person the turned
 * look sees at 0.78.
 *
 * MUTUAL containment, and a high bar, because the failure to avoid is a person
 * standing in front of a car: the person is mostly inside the car's box, but
 * the car is not inside the person's, so that pair is not "the same box" and
 * both survive. Two views of one object are inside each other both ways. */
#define TILE_ROT_SAME_CLASS_FRAC  0.70f

static void _merge_rotated(uint32_t n)
{
  for (uint32_t i = 0U; i < n; i++)
  {
    if (!_sw_dets[i].sustain) continue;
    for (uint32_t j = i + 1U; j < n; j++)
    {
      if (!_sw_dets[j].sustain) continue;
      if (!_sw_dets[i].rot && !_sw_dets[j].rot) continue;

      const float a = _inside_of(&_sw_dets[i], &_sw_dets[j]);
      const float b = _inside_of(&_sw_dets[j], &_sw_dets[i]);
      const bool  same_class = (_sw_dets[j].cls == _sw_dets[i].cls);

      if (!same_class)
      {
        if ((a < TILE_ROT_SAME_CLASS_FRAC) ||
            (b < TILE_ROT_SAME_CLASS_FRAC)) continue;
      }
      else if ((a < TILE_ROT_SAME_FRAC) || (b < TILE_ROT_SAME_FRAC))
      {
        continue;
      }

      /* Who survives.
       *
       * When BOTH views already count the object, the upright one keeps it,
       * whatever the confidences say. The turned view is the better detector
       * of a person on the ground and the worse describer of where they are:
       * it frames them from a picture the network was never trained on, and
       * its box drifts. On `crowd_13.jpg` that cost a person, repeatably, not
       * as flicker: the turned box won the merge, and being larger it then
       * swallowed a fifth person standing beside it, 5 people to 4 on six
       * sweeps out of six. Keeping the upright geometry when the upright pass
       * already had the object costs nothing (the count is the same either
       * way) and gives that person back.
       *
       * Otherwise the more confident view wins, which is what makes the two
       * cases this exists for come out right: when only the turned view counts
       * the object, it is the recovery; and when the two views DISAGREE ABOUT
       * THE CLASS, the turned one is the one that gets a person on the ground
       * right, so the person at 0.78 replaces the "car" at 0.50 rather than
       * standing next to it. A whole box still beats a fragment, as in the NMS
       * above. */
      const bool i_frag = _sw_dets[i].frag;
      const bool j_frag = _sw_dets[j].frag;
      const bool geometry_call = same_class && _sw_dets[i].keep &&
                                 _sw_dets[j].keep &&
                                 (_sw_dets[i].rot != _sw_dets[j].rot);
      const bool j_wins = (i_frag != j_frag)
                        ? i_frag
                        : geometry_call
                          ? _sw_dets[i].rot
                          : (_sw_dets[j].conf > _sw_dets[i].conf);
      if (j_wins)
      {
        _sw_dets[i].sustain = false;
        _sw_dets[i].keep    = false;
        break;
      }
      _sw_dets[j].sustain = false;
      _sw_dets[j].keep    = false;
    }
  }
}

/* How much of a fragment has to sit inside a counted box before that box is
 * taken to explain it. Half is deliberately lenient: the tile cuts wherever
 * the grid falls, so a piece can stick out past the whole object's box on the
 * side the network guessed at, and demanding near-total containment would
 * promote that piece into a second person. */
#define TILE_EXPLAINED_FRAC  0.50f

/* Promote fragments that nothing whole accounts for (ScopusQA #25).
 *
 * A fragment is a person the grid cut in half. Usually some other tile, or the
 * whole-frame pass, saw the same person intact, and then the fragment is noise
 * and counting it is the double count that edgedrop exists to prevent. But
 * when an object is wider than the tile overlap, NO tile holds it whole, every
 * copy is a fragment, and dropping all of them loses a person who is plainly
 * in the picture.
 *
 * So: walk the surviving fragments, most confident first. If a counted box of
 * the same class already covers one, it is explained and stays uncounted. If
 * nothing does, promote it, and stitch its siblings into it, so the pieces of
 * one object become one object and not two. */
static void _rescue_fragments(uint32_t n)
{
  for (;;)
  {
    /* The best unexplained fragment left, if any. */
    uint32_t best = n;
    for (uint32_t i = 0U; i < n; i++)
    {
      const t_tile_det *f = &_sw_dets[i];
      if (!f->sustain || !f->frag || f->keep) continue;
      if (f->conf < _cfg_conf) continue;      /* weak: evidence, never a count */

      bool explained = false;
      for (uint32_t j = 0U; j < n; j++)
      {
        if ((j == i) || !_sw_dets[j].keep) continue;
        if (_sw_dets[j].cls != f->cls) continue;
        if (_inside_of(f, &_sw_dets[j]) >= TILE_EXPLAINED_FRAC)
        {
          explained = true;
          break;
        }
      }
      if (explained) continue;

      if ((best == n) || (f->conf > _sw_dets[best].conf)) best = i;
    }

    if (best == n) break;          /* nothing left that needs rescuing */

    _sw_dets[best].keep = true;    /* this piece now stands for the object */

    /* Stitch the rest of the object into it: every other fragment cut by the
     * same seam is retired into this one, and the box grows to cover them, so
     * the count is one and the overlay frames the whole object rather than the
     * piece that happened to be the most confident. */
    for (uint32_t j = 0U; j < n; j++)
    {
      t_tile_det *sib = &_sw_dets[j];
      if ((j == best) || !sib->sustain || !sib->frag || sib->keep) continue;
      if (sib->cls != _sw_dets[best].cls) continue;
      if (!_stitches_with(sib, &_sw_dets[best])) continue;

      if (sib->x1 < _sw_dets[best].x1) _sw_dets[best].x1 = sib->x1;
      if (sib->y1 < _sw_dets[best].y1) _sw_dets[best].y1 = sib->y1;
      if (sib->x2 > _sw_dets[best].x2) _sw_dets[best].x2 = sib->x2;
      if (sib->y2 > _sw_dets[best].y2) _sw_dets[best].y2 = sib->y2;
      sib->sustain = false;        /* accounted for; not a detection of its own */
    }
  }
}

uint32_t tile_sweep_finish(void)
{
  _nms(_sw_n_acc, _cfg_iou);
  _merge_rotated(_sw_n_acc);
  _rescue_fragments(_sw_n_acc);

  uint32_t kept = 0U;
  for (uint32_t i = 0U; i < _sw_n_acc; i++)
  {
    if (_sw_dets[i].keep) kept++;
  }
  return kept;
}

const t_tile_det *tile_sweep_dets(uint32_t *n)
{
  if (n) *n = _sw_n_acc;
  return _sw_dets;
}

void tile_sweep_stats(uint32_t *raw, uint32_t *over_thresh)
{
  if (raw)         *raw         = _sw_n_raw;
  if (over_thresh) *over_thresh = _sw_n_acc;
}

bool tile_sweep_saturated(void)
{
  return _sw_sat;
}

/* ── Live-frame capture ────────────────────────────────────────────────── */

uint8_t *tile_capture_live(uint16_t *fw_out, uint16_t *fh_out)
{
  const uint16_t mw = (uint16_t)CAMERA_MAIN_WIDTH;
  const uint16_t mh = (uint16_t)CAMERA_MAIN_HEIGHT;

  uint8_t *src = camera_get_buffer(DCMIPP_PIPE1);
  if (src == NULL)
  {
    return NULL;
  }

  /* Invalidate first: the pipe is DMA'd into PSRAM by the camera behind the
   * D-cache, so without this we resize whatever the core last cached. */
  SCB_InvalidateDCache_by_Addr((uint32_t*)src, (int32_t)((uint32_t)mw * mh * 2U));

  for (uint32_t p = 0U; p < (uint32_t)mw * mh; p++)
  {
    uint16_t px = (uint16_t)src[p * 2U] | ((uint16_t)src[p * 2U + 1U] << 8);
    uint8_t  r5 = (uint8_t)((px >> 11) & 0x1FU);
    uint8_t  g6 = (uint8_t)((px >> 5)  & 0x3FU);
    uint8_t  b5 = (uint8_t)( px        & 0x1FU);
    _live_rgb[p * 3U + 0U] = (uint8_t)((r5 << 3) | (r5 >> 2));
    _live_rgb[p * 3U + 1U] = (uint8_t)((g6 << 2) | (g6 >> 4));
    _live_rgb[p * 3U + 2U] = (uint8_t)((b5 << 3) | (b5 >> 2));
  }

  if (fw_out) *fw_out = mw;
  if (fh_out) *fh_out = mh;
  return _live_rgb;
}

/* ── Standing in for the lens ──────────────────────────────────────────── */

static const uint8_t *_inj_frame   = NULL;
static uint16_t       _inj_fw      = 0U;
static uint16_t       _inj_fh      = 0U;
static uint32_t       _inj_until   = 0U;   /* HAL tick the loan runs out at */
static bool           _inj_lapsed  = false;/* expired and not yet announced */

void tile_inject_set(const uint8_t *frame, uint16_t fw, uint16_t fh,
                     uint32_t ttl_ms)
{
  if ((frame == NULL) || (ttl_ms == 0U) || (fw == 0U) || (fh == 0U))
  {
    _inj_frame  = NULL;
    _inj_lapsed = false;
    return;
  }
  _inj_frame  = frame;
  _inj_fw     = fw;
  _inj_fh     = fh;
  _inj_until  = HAL_GetTick() + ttl_ms;
  _inj_lapsed = false;
}

const uint8_t *tile_inject_get(uint16_t *fw_out, uint16_t *fh_out,
                               bool *expired_out)
{
  if (expired_out) { *expired_out = false; }
  if (_inj_frame == NULL) return NULL;

  /* Signed compare so the deadline stays correct across the tick wrap. */
  if ((int32_t)(HAL_GetTick() - _inj_until) >= 0)
  {
    _inj_frame = NULL;
    if (expired_out) { *expired_out = true; }
    _inj_lapsed = true;
    return NULL;
  }

  if (fw_out) *fw_out = _inj_fw;
  if (fh_out) *fh_out = _inj_fh;
  return _inj_frame;
}

uint32_t tile_inject_left_s(void)
{
  if (_inj_frame == NULL) return 0U;
  const int32_t left = (int32_t)(_inj_until - HAL_GetTick());
  return (left <= 0) ? 0U : ((uint32_t)left + 999U) / 1000U;
}
