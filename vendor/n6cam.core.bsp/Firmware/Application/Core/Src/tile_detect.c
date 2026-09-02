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

/* Live-pipe geometry — ScopusQA #22: 12 tiles, 3 rows x 4 columns. The crop is
 * the NN's own side, so a tile is carried into the network at 1:1 with no
 * resampling loss; 4x256 = 1024 over 800 px and 3x256 = 768 over 600 px, so
 * auto-overlap covers the frame with ~75 px of margin on each seam. */
#define TILE_LIVE_COLS      4U
#define TILE_LIVE_ROWS      3U
#define TILE_LIVE_CROP      TILE_NN_SIDE

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

/* What fraction of the confidence floor a detection has to hold to keep being
 * believed. 0.75 of the 0.45 default is 0.34 — comfortably below the 0.45-0.50
 * the flickering sixth person in 5_people.jpeg measured at, and comfortably
 * above the noise that a 0.2 floor would let in. See tile_detect.h. */
#define TILE_SUSTAIN_FRAC   0.75f

/* Weak detections may never crowd real ones out of the accumulator: they exist
 * only to slow the count's descent, and a sweep that dropped a real detection
 * to make room for one would be worse off than before. */
#define TILE_WEAK_LIMIT     ((TILE_MAX_DETS * 3U) / 4U)

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
  return (uint32_t)_cfg_cols * (uint32_t)_cfg_rows
       + (_cfg_fullpass ? 1U : 0U);
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

/* Bilinear-resize the square region [cx,cy, crop x crop] of an RGB888 frame
 * into the TILE_NN_SIDE^2 NN input. Source coordinates are clamped so we never
 * read outside the frame. */
static void _resize_crop(const uint8_t *src, uint16_t fw, uint16_t fh,
                         uint16_t cx, uint16_t cy,
                         uint16_t cw, uint16_t ch, uint8_t *dst)
{
  const uint32_t N     = TILE_NN_SIDE;
  /* Separate steps per axis: a tile is square, but the whole-frame pass is
   * not, and it has to squash 800x600 into 256x256 exactly the way the
   * ancillary pipe does — otherwise it is not the comparison it claims. */
  const float    stepx = (cw > 1U) ? (float)(cw - 1U) / (float)(N - 1U) : 0.0f;
  const float    stepy = (ch > 1U) ? (float)(ch - 1U) / (float)(N - 1U) : 0.0f;

  for (uint32_t oy = 0U; oy < N; oy++)
  {
    float    fy = (float)cy + (float)oy * stepy;
    uint32_t y0 = (uint32_t)fy;
    if (y0 > (uint32_t)(fh - 2U)) y0 = fh - 2U;
    float wy = fy - (float)y0;

    for (uint32_t ox = 0U; ox < N; ox++)
    {
      float    fx = (float)cx + (float)ox * stepx;
      uint32_t x0 = (uint32_t)fx;
      if (x0 > (uint32_t)(fw - 2U)) x0 = fw - 2U;
      float wx = fx - (float)x0;

      const uint8_t *p00 = src + ((size_t)y0 * fw + x0) * 3U;
      const uint8_t *p01 = p00 + 3U;
      const uint8_t *p10 = p00 + (size_t)fw * 3U;
      const uint8_t *p11 = p10 + 3U;
      uint8_t *d = dst + ((size_t)oy * N + ox) * 3U;

      for (uint32_t ch = 0U; ch < 3U; ch++)
      {
        float top = (float)p00[ch] * (1.0f - wx) + (float)p01[ch] * wx;
        float bot = (float)p10[ch] * (1.0f - wx) + (float)p11[ch] * wx;
        float v   = top * (1.0f - wy) + bot * wy;
        d[ch] = (uint8_t)(v + 0.5f);
      }
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
        /* keep the higher-confidence one */
        if (_sw_dets[j].conf > _sw_dets[i].conf)
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
                       uint16_t *cw, uint16_t *ch)
{
  if (_sw_full && (idx >= _sw_tiles))
  {
    *cx = 0U; *cy = 0U; *cw = _sw_fw; *ch = _sw_fh;
    return;
  }
  uint16_t c = (uint16_t)(idx % _sw_cols);
  uint16_t r = (uint16_t)(idx / _sw_cols);
  if (r >= _sw_rows) { r = (uint16_t)(_sw_rows - 1U); }
  *cx = _sw_xs[c]; *cy = _sw_ys[r];
  *cw = _sw_crop;  *ch = _sw_crop;
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
  return _sw_tiles + (_sw_full ? 1U : 0U);
}

void tile_sweep_crop(uint32_t idx, uint8_t *dst)
{
  if ((_sw_frame == NULL) || (_sw_cols == 0U)) return;
  if (idx >= (_sw_tiles + (_sw_full ? 1U : 0U))) return;

  uint16_t cx, cy, cw, ch;
  _step_rect(idx, &cx, &cy, &cw, &ch);
  _resize_crop(_sw_frame, _sw_fw, _sw_fh, cx, cy, cw, ch, dst);
}

void tile_sweep_collect(uint32_t idx, const t_nn_box *boxes, uint32_t n)
{
  if ((_sw_cols == 0U) || (boxes == NULL)) return;
  if (idx >= (_sw_tiles + (_sw_full ? 1U : 0U))) return;

  uint16_t cx, cy, cw, ch;
  _step_rect(idx, &cx, &cy, &cw, &ch);

  const float inv_fw = 1.0f / (float)_sw_fw;
  const float inv_fh = 1.0f / (float)_sw_fh;

  const bool is_tile = !(_sw_full && (idx >= _sw_tiles));

  const float sustain_floor = tile_cfg_conf_sustain();

  for (uint32_t i = 0U; i < n; i++)
  {
    _sw_n_raw++;
    if (boxes[i].conf < sustain_floor) continue;

    /* Below the full floor this is not a detection — it is only evidence that
     * something the sweep already counted has not gone away. Admitted, marked,
     * and never counted. */
    const bool weak = (boxes[i].conf < _cfg_conf);
    if (weak && (_sw_n_acc >= TILE_WEAK_LIMIT)) continue;

    /* Fragment rejection — see the note on _cfg_edgedrop. Only tiles; the
     * whole-frame pass has no cut edges, only frame edges. */
    if (is_tile && _cfg_edgedrop)
    {
      const float m  = TILE_EDGE_MARGIN;
      const float x1 = boxes[i].x_center - boxes[i].width  * 0.5f;
      const float x2 = boxes[i].x_center + boxes[i].width  * 0.5f;
      const float y1 = boxes[i].y_center - boxes[i].height * 0.5f;
      const float y2 = boxes[i].y_center + boxes[i].height * 0.5f;

      if (((x1 < m)        && (cx > 0U))            ||
          ((x2 > 1.0f - m) && ((uint32_t)cx + cw < (uint32_t)_sw_fw)) ||
          ((y1 < m)        && (cy > 0U))            ||
          ((y2 > 1.0f - m) && ((uint32_t)cy + ch < (uint32_t)_sw_fh)))
      {
        continue;
      }
    }

    if (_sw_n_acc >= TILE_MAX_DETS) { _sw_sat = true; continue; }

    /* step-normalized -> step px -> full-frame px -> full-frame normalized */
    float bx = (float)cx + boxes[i].x_center * (float)cw;
    float by = (float)cy + boxes[i].y_center * (float)ch;
    float bw = boxes[i].width  * (float)cw;
    float bh = boxes[i].height * (float)ch;

    t_tile_det *d = &_sw_dets[_sw_n_acc++];
    d->x1   = (bx - bw * 0.5f) * inv_fw;
    d->y1   = (by - bh * 0.5f) * inv_fh;
    d->x2   = (bx + bw * 0.5f) * inv_fw;
    d->y2   = (by + bh * 0.5f) * inv_fh;
    d->conf    = boxes[i].conf;
    d->cls     = boxes[i].class_index;
    d->keep    = !weak;
    d->sustain = true;
  }
}

uint32_t tile_sweep_finish(void)
{
  _nms(_sw_n_acc, _cfg_iou);

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
