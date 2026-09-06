/* Host-runnable test for `detect rotate auto`, the turned second look
 * (ScopusQA #26, reopened 2026-09-06).
 *
 * What it pins down, in the order the sweep does it:
 *
 *   1. A sweep starts at 14 steps, 12 tiles, the whole frame, and the whole
 *      frame turned, and GROWS when the upright block nominates tiles. A
 *      caller that cached tile_sweep_begin()'s answer would stop before the
 *      new steps and the feature would silently do nothing, which is the one
 *      way this can fail without any output looking wrong.
 *   2. A tall box nominates nothing. This is the cost claim: a scene of people
 *      standing up must pay exactly what `full` paid.
 *   3. A wide box nominates the tiles it covers, whatever class it landed on,
 *      and whatever its confidence, a person on the ground is the detection
 *      that sits UNDER the counting floor, so a trigger that waited for a
 *      countable box would fire only where it was not needed.
 *   4. The nominations are capped, and the widest boxes win the cap.
 *   5. The appended step really is that tile, really is turned. Checked by
 *      reading the crop back: the frame is painted so that every pixel says
 *      where in the frame it came from, so one pixel of the crop identifies
 *      both the tile and the orientation. A crop/collect mismatch here would
 *      put boxes in the wrong place and nothing but the overlay would show it.
 *
 * Build and run: python3 tests/test_tile_rotate.py
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tile_detect.h"

/* The stub's camera. Nothing in this test streams. */
uint8_t *camera_get_buffer(int pipe) { (void)pipe; return NULL; }

/* The stub's clock, so an expiry can be reached without waiting for one. */
uint32_t host_tick_ms = 0U;

#define FW   ((uint16_t)CAMERA_MAIN_WIDTH)
#define FH   ((uint16_t)CAMERA_MAIN_HEIGHT)
#define NN   ((uint32_t)TILE_NN_SIDE)

static int _fails = 0;

static void _check(bool ok, const char *what)
{
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) _fails++;
}

/* A frame that says where each pixel is: red carries x/4, green y/4, and both
 * fit in a byte for any frame up to 1024x1024. Reading one pixel of a crop
 * therefore gives back the source coordinate it was taken from. */
static uint8_t *_paint_frame(void)
{
  uint8_t *f = malloc((size_t)FW * FH * 3U);
  if (f == NULL) { perror("malloc"); exit(2); }
  for (uint32_t y = 0U; y < FH; y++)
  {
    for (uint32_t x = 0U; x < FW; x++)
    {
      uint8_t *p = f + ((size_t)y * FW + x) * 3U;
      p[0] = (uint8_t)(x / 4U);
      p[1] = (uint8_t)(y / 4U);
      p[2] = 0U;
    }
  }
  return f;
}

/* One box, in the step's own normalized coordinates. */
static t_nn_box _box(float xc, float yc, float w, float h, float conf, int cls)
{
  t_nn_box b;
  memset(&b, 0, sizeof(b));
  b.x_center    = xc;
  b.y_center    = yc;
  b.width       = w;
  b.height      = h;
  b.conf        = conf;
  b.class_index = cls;
  return b;
}

/* Walk the upright block, handing `on_step` a box on the steps it names.
 * Returns the number of steps the sweep says it has once the block is done. */
static uint32_t _run_upright(const uint8_t *frame, const t_nn_box *per_step,
                             const bool *give, uint32_t n_steps)
{
  for (uint32_t i = 0U; i < n_steps; i++)
  {
    if (give[i]) { tile_sweep_collect(i, &per_step[i], 1U); }
    else         { tile_sweep_collect(i, NULL, 0U); }
  }
  (void)frame;
  return tile_sweep_steps();
}

/* Which tile origin the crop of `step` was taken from, and whether it was
 * turned. dst(0,0) is src(cx, cy) upright and src(cx, cy + ch - 1) turned, so
 * the painted frame answers both at once. */
static void _crop_origin(uint32_t step, uint8_t *buf,
                         uint32_t *x_out, uint32_t *y_out)
{
  tile_sweep_crop(step, buf);
  *x_out = (uint32_t)buf[0] * 4U;
  *y_out = (uint32_t)buf[1] * 4U;
}

int main(void)
{
  uint8_t *frame = _paint_frame();
  uint8_t *crop  = malloc((size_t)NN * NN * 3U);
  if (crop == NULL) { perror("malloc"); return 2; }

  /* The live geometry, which is what the main path arms: 4x3 tiles of 320 px
   * over 800x600, whole-frame pass on. */
  tile_cfg_for_live();

  const uint16_t xs[4] = {0U, 160U, 320U, 480U};
  const uint16_t ys[3] = {0U, 140U, 280U};
  const uint32_t tiles = 12U;
  const uint32_t base  = tiles + 1U;          /* + the whole-frame pass      */

  t_nn_box boxes[16];
  bool     give[16];

  /* ── 1. the shape of a sweep, per setting ──────────────────────────────*/
  printf("step counts\n");
  tile_cfg_set_rotate(TILE_ROT_OFF);
  _check(tile_sweep_begin(frame, FW, FH) == base, "off: 13 steps");

  tile_cfg_set_rotate(TILE_ROT_FULL);
  _check(tile_sweep_begin(frame, FW, FH) == base + 1U, "full: 14 steps");

  tile_cfg_set_rotate(TILE_ROT_ALL);
  _check(tile_sweep_begin(frame, FW, FH) == base * 2U, "all: 26 steps");

  tile_cfg_set_rotate(TILE_ROT_AUTO);
  _check(tile_sweep_begin(frame, FW, FH) == base + 1U,
         "auto: starts at 14, the same as full");

  /* ── 2. a scene of people standing up costs nothing ────────────────────*/
  printf("a tall box nominates nothing\n");
  memset(give, 0, sizeof(give));
  for (uint32_t i = 0U; i < tiles; i++)
  {
    boxes[i] = _box(0.5f, 0.5f, 0.12f, 0.55f, 0.80f, 0);   /* standing */
    give[i]  = true;
  }
  boxes[tiles] = _box(0.5f, 0.5f, 0.12f, 0.55f, 0.80f, 0);
  give[tiles]  = true;

  tile_cfg_set_rotate(TILE_ROT_AUTO);
  (void)tile_sweep_begin(frame, FW, FH);
  {
    uint32_t steps = _run_upright(frame, boxes, give, base);
    uint32_t done = 0U, wanted = 0U;
    tile_sweep_relook_stats(&done, &wanted);
    _check(steps == base + 1U, "12 standing people leave the sweep at 14");
    _check((done == 0U) && (wanted == 0U), "nothing nominated");
  }

  /* ── 3. a wide box nominates, under the floor and off-class ────────────*/
  printf("a wide box nominates the tile it lies in\n");
  memset(give, 0, sizeof(give));
  memset(boxes, 0, sizeof(boxes));
  /* Tile 5 is xs[1], ys[1] = (160,140). A box in the middle of it, 0.55 x
   * 0.15 of a 320 px tile, is 176 x 48 px: a person on the ground. conf 0.31
   * is under the 0.34 sustain floor, and class 2 is "car", which is what the
   * network calls one at range. */
  boxes[5] = _box(0.5f, 0.5f, 0.55f, 0.15f, 0.31f, 2);
  give[5]  = true;

  (void)tile_sweep_begin(frame, FW, FH);
  {
    uint32_t steps = _run_upright(frame, boxes, give, base);
    uint32_t done = 0U, wanted = 0U;
    tile_sweep_relook_stats(&done, &wanted);
    _check(steps == base + 2U, "the sweep grew by one step");
    _check(done == 1U, "one second look taken");
    _check(wanted == 1U, "one asked for");

    /* ── 5. and that step is that tile, turned ──────────────────────────*/
    uint32_t ox = 0U, oy = 0U;
    _crop_origin(base + 1U, crop, &ox, &oy);
    /* Turned: dst(0,0) comes from the tile's BOTTOM-left corner. */
    _check((ox / 4U) == (uint32_t)(xs[1] / 4U),
           "the appended step crops tile column 1");
    _check((oy / 4U) == (uint32_t)((ys[1] + 320U - 1U) / 4U),
           "...from its bottom edge, i.e. turned 90 deg");
  }

  /* ── 4. the cap, and who wins it ───────────────────────────────────────*/
  printf("the cap\n");
  memset(give, 0, sizeof(give));
  memset(boxes, 0, sizeof(boxes));
  /* Six wide boxes, each small enough to stay inside its own tile, with
   * ratios rising along the row so the expected winners are known: tiles 6..11
   * carry ratios 2, 3, 4, 5, 6, 7 and the four widest are tiles 8, 9, 10, 11.
   * A box 0.08 tall and 0.16..0.56 wide is well inside a 320 px tile. */
  for (uint32_t t = 6U; t < 12U; t++)
  {
    const float ratio = (float)(t - 4U);            /* 2, 3, 4, 5, 6, 7      */
    boxes[t] = _box(0.5f, 0.5f, 0.08f * ratio, 0.08f, 0.40f, 0);
    give[t]  = true;
  }

  (void)tile_sweep_begin(frame, FW, FH);
  {
    uint32_t steps = _run_upright(frame, boxes, give, base);
    uint32_t done = 0U, wanted = 0U;
    tile_sweep_relook_stats(&done, &wanted);
    _check(wanted >= 6U, "six tiles asked for a look");
    _check(done == TILE_ROT_AUTO_MAX, "four were taken");
    _check(steps == base + 1U + TILE_ROT_AUTO_MAX,
           "the sweep is 14 + 4 = 18 steps, never more");

    /* Widest first: tile 11 (ratio 7) is the first appended step, then 10, 9,
     * 8. Tile t sits at column t % 4, row t / 4. */
    const uint32_t want_tile[4] = {11U, 10U, 9U, 8U};
    for (uint32_t k = 0U; k < 4U; k++)
    {
      uint32_t ox = 0U, oy = 0U;
      char     what[96];
      _crop_origin(base + 1U + k, crop, &ox, &oy);
      snprintf(what, sizeof(what),
               "look %u is tile %u, the %s-widest", (unsigned)(k + 1U),
               (unsigned)want_tile[k], (k == 0U) ? "first" : "next");
      _check((ox / 4U) == (uint32_t)(xs[want_tile[k] % 4U] / 4U) &&
             (oy / 4U) == (uint32_t)((ys[want_tile[k] / 4U] + 320U - 1U) / 4U),
             what);
    }
  }

  /* ── 3b. the whole-frame pass nominates too ────────────────────────────*/
  printf("the whole-frame pass can nominate\n");
  memset(give, 0, sizeof(give));
  memset(boxes, 0, sizeof(boxes));
  /* A box only the whole-frame step reports: 0.30 x 0.06 of 800x600 is
   * 240 x 36 px, lying across the middle of the frame. */
  boxes[tiles] = _box(0.5f, 0.5f, 0.30f, 0.06f, 0.35f, 0);
  give[tiles]  = true;

  (void)tile_sweep_begin(frame, FW, FH);
  {
    uint32_t steps = _run_upright(frame, boxes, give, base);
    uint32_t done = 0U, wanted = 0U;
    tile_sweep_relook_stats(&done, &wanted);
    _check(steps > base + 1U, "the sweep grew");
    _check((done >= 1U) && (done <= TILE_ROT_AUTO_MAX),
           "the tiles under the box were nominated, within the cap");
    _check(wanted >= done, "the count asked for is never below the count taken");
  }

  /* ── 6. off is still off ───────────────────────────────────────────────*/
  printf("the control\n");
  tile_cfg_set_rotate(TILE_ROT_OFF);
  memset(give, 0, sizeof(give));
  boxes[5] = _box(0.5f, 0.5f, 0.55f, 0.15f, 0.31f, 2);
  give[5]  = true;
  (void)tile_sweep_begin(frame, FW, FH);
  {
    uint32_t steps = _run_upright(frame, boxes, give, base);
    uint32_t done = 0U, wanted = 0U;
    tile_sweep_relook_stats(&done, &wanted);
    _check(steps == base, "rotate off: a wide box changes nothing");
    _check((done == 0U) && (wanted == 0U), "rotate off: nothing nominated");
  }

  /* ── 6b. what a turned step is allowed to contribute ───────────────────*/
  tile_cfg_set_rotate(TILE_ROT_AUTO);

  /* The turned step's box is given in ITS OWN coordinates, the same rectangle
   * with the axes swapped, which is what tile_sweep_collect() undoes. */
  printf("a turned step contributes people and nothing else\n");
  {
    memset(give, 0, sizeof(give));
    memset(boxes, 0, sizeof(boxes));
    boxes[5] = _box(0.5f, 0.5f, 0.55f, 0.15f, 0.84f, 2);        /* car    */
    give[5]  = true;
    (void)tile_sweep_begin(frame, FW, FH);
    (void)_run_upright(frame, boxes, give, base);

    t_nn_box turned = _box(0.5f, 0.5f, 0.15f, 0.55f, 0.45f, 6); /* "train" */
    tile_sweep_collect(base + 1U, &turned, 1U);

    _check(tile_sweep_finish() == 1U, "the turned vehicle is dropped");

    uint32_t n_dets = 0U;
    const t_tile_det *dets = tile_sweep_dets(&n_dets);
    int32_t cls = -1;
    for (uint32_t i = 0U; i < n_dets; i++) { if (dets[i].keep) cls = dets[i].cls; }
    _check(cls == 2, "the car the upright pass found is what stands");
  }

  printf("a turned person renames what the upright pass called a car\n");
  {
    /* #26's miscount: at range the upright pass reads a person on the ground
     * as a vehicle. The turned look at the same box sees the person, more
     * confidently, and one object cannot be both. */
    memset(give, 0, sizeof(give));
    memset(boxes, 0, sizeof(boxes));
    boxes[5] = _box(0.5f, 0.5f, 0.55f, 0.15f, 0.50f, 2);        /* "car"  */
    give[5]  = true;
    (void)tile_sweep_begin(frame, FW, FH);
    (void)_run_upright(frame, boxes, give, base);

    t_nn_box turned = _box(0.5f, 0.5f, 0.15f, 0.55f, 0.78f, 0); /* person */
    tile_sweep_collect(base + 1U, &turned, 1U);

    _check(tile_sweep_finish() == 1U, "one object, not a car AND a person");

    uint32_t n_dets = 0U;
    const t_tile_det *dets = tile_sweep_dets(&n_dets);
    int32_t cls = -1;
    for (uint32_t i = 0U; i < n_dets; i++) { if (dets[i].keep) cls = dets[i].cls; }
    _check(cls == 0, "and it is the person");
  }

  printf("a person in front of a car is still two things\n");
  {
    /* The person's box is mostly inside the car's; the car's is not inside the
     * person's. Not the same box, so both survive, this is the pair the
     * cross-class merge must never eat. */
    memset(give, 0, sizeof(give));
    memset(boxes, 0, sizeof(boxes));
    boxes[5] = _box(0.5f, 0.5f, 0.60f, 0.40f, 0.80f, 2);        /* car    */
    give[5]  = true;
    (void)tile_sweep_begin(frame, FW, FH);
    (void)_run_upright(frame, boxes, give, base);

    t_nn_box turned = _box(0.5f, 0.5f, 0.30f, 0.12f, 0.70f, 0); /* person */
    tile_sweep_collect(base + 1U, &turned, 1U);

    _check(tile_sweep_finish() == 2U, "both kept");
  }

  /* ── 7. standing in for the lens, and giving it back ───────────────────*/
  printf("tile inject\n");
  {
    uint16_t iw = 0U, ih = 0U;
    bool     lapsed = false;

    host_tick_ms = 1000U;
    _check(tile_inject_get(&iw, &ih, &lapsed) == NULL, "nothing injected");

    tile_inject_set(frame, FW, FH, 5000U);
    _check(tile_inject_get(&iw, &ih, &lapsed) == frame, "the frame is served");
    _check((iw == FW) && (ih == FH), "at its own size");
    _check(tile_inject_left_s() == 5U, "five seconds left");

    host_tick_ms = 1000U + 4999U;
    _check(tile_inject_get(&iw, &ih, &lapsed) == frame, "still live at 4999 ms");

    host_tick_ms = 1000U + 5000U;
    _check(tile_inject_get(&iw, &ih, &lapsed) == NULL, "expired at the deadline");
    _check(lapsed, "and said so, once");

    lapsed = false;
    _check(tile_inject_get(&iw, &ih, &lapsed) == NULL, "still expired");
    _check(!lapsed, "and does not repeat itself");

    tile_inject_set(frame, FW, FH, 5000U);
    tile_inject_set(NULL, 0U, 0U, 0U);
    _check(tile_inject_get(&iw, &ih, &lapsed) == NULL, "cleared on demand");
  }

  free(crop);
  free(frame);

  printf("\n%s\n", _fails ? "FAILED" : "all checks passed");
  return _fails ? 1 : 0;
}
