/**
 ******************************************************************************
 * @file    motion_sensor.c
 * @brief   Board-movement detection from the LSM6DSO32 — implementation.
 *          See `motion_sensor.h` for what this is for and why it has two legs.
 ******************************************************************************
 */
#include "motion_sensor.h"

#include "common.h"
#include "n6cam_i2c.h"
#include "registry.h"      /* NOTIFY_RSN_MOTION_START / _STOP */
#include "shell_task.h"    /* shell_notify_emit               */

#include <string.h>

/* ------------------------------------------------------------------------- */
/* LSM6DSO32 register map — only the handful this detector touches.          */
/* ------------------------------------------------------------------------- */
#define LSM6_WHO_AM_I         0x0FU
#define LSM6_CTRL1_XL         0x10U
#define LSM6_CTRL3_C          0x12U
#define LSM6_CTRL5_C          0x14U
#define LSM6_WAKE_UP_SRC      0x1BU
#define LSM6_OUTX_L_A         0x28U
#define LSM6_TAP_CFG0         0x56U
#define LSM6_TAP_CFG2         0x58U
#define LSM6_WAKE_UP_THS      0x5BU
#define LSM6_WAKE_UP_DUR      0x5CU
#define LSM6_MD1_CFG          0x5EU

#define LSM6_WHO_AM_I_VALUE   0x6CU

/* The part answers on one of two addresses depending on how SA0 is strapped.
 * The BSP's own board self-test reads it at 0xD7, i.e. 7-bit 0x6B, so that is
 * tried first; the other is kept so this code also works on a board built the
 * other way round, which costs one failed transfer at boot and nothing after. */
static const uint16_t LSM6_ADDRS[2] = { 0xD6U, 0xD4U };

/* ODR 52 Hz, FS ±4 g (the "32" part's smallest range).
 *
 * 52 Hz is not an arbitrary pick: the wake-up slope filter differentiates
 * consecutive samples, so the ODR sets what "sudden" means, and 52 Hz makes a
 * shove of a few tens of milliseconds visible while keeping the part in its
 * low-power band. FS ±4 g keeps the threshold resolution fine (see below) and
 * still leaves headroom for a knock. */
#define LSM6_CTRL1_XL_CFG     0x30U     /* ODR_XL = 0011 (52 Hz), FS_XL = 00 */

/* WAKE_UP_DUR.WAKE_THS_W = 1 → one threshold LSB is FS/256 rather than FS/64,
 * i.e. 15.6 mg at ±4 g instead of 62.5 mg. Without it the gentlest setting the
 * hardware can express is already a firm knock. */
#define LSM6_WAKE_THS_W       0x10U
#define LSM6_THS_LSB_UG       15625U    /* µg per threshold LSB at ±4 g */

/* Accelerometer output scaling at ±4 g: 0.122 mg/LSB. */
#define LSM6_OUT_UG_PER_LSB   122U

/* How far the sensitivity dial can move the threshold. The top end is a
 * deliberate shove of the box; the bottom is about as low as the part's own
 * noise allows without the unit reporting its own power supply. */
#define MOTION_THS_LSB_MIN    1U        /* ~16 mg  at sensitivity 100 */
#define MOTION_THS_LSB_MAX    32U       /* ~500 mg at sensitivity 0   */

/* Resting-attitude tracker. The reference vector follows the box while it is
 * still, so that a unit left at a new angle after a move settles back to "no
 * motion" instead of reporting for ever, and so that temperature drift never
 * looks like movement. Frozen while motion is in progress — tracking then
 * would chase the very signal being measured. 1/16 per poll at ~5 Hz is a
 * time constant of about three seconds. */
#define MOTION_REF_SHIFT      4U

/* Self-test: the datasheet's own pass band for the ±4 g range is wide, and
 * what matters here is that the mass moved at all rather than by how much. */
#define MOTION_SELFTEST_MIN_MG   30
#define MOTION_SELFTEST_MAX_MG   2000

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */
static bool      _present      = false;
static bool      _probed       = false;   /* absence is reported once only */
static uint16_t  _addr         = 0U;
static uint8_t   _who          = 0U;

static uint8_t   _sensitivity  = 50U;
static uint32_t  _timeout_s    = 30U;
static uint8_t   _ths_lsb      = 0U;
static uint32_t  _ths_mg       = 0U;

static bool      _active       = false;   /* the §6 `mtn` state            */
static bool      _forced       = false;   /* asserted by `motion simulate` */
static bool      _ref_valid    = false;
static int32_t   _ref_q4[3]    = { 0 };   /* resting vector, mg × 16       */
static uint32_t  _last_move    = 0U;      /* ticks                         */
static uint32_t  _started      = 0U;      /* ticks                         */
static uint32_t  _last_dev_mg  = 0U;
static uint32_t  _peak_dev_mg  = 0U;
static uint32_t  _starts       = 0U;
static uint32_t  _stops        = 0U;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static int32_t _rd(uint16_t reg, uint8_t *buff, uint16_t size)
{
  return bsp_i2c_read_reg8(I2C_SENSORS, _addr, reg, buff, size, 100U);
}

static int32_t _wr(uint16_t reg, uint8_t val)
{
  uint8_t v = val;
  return bsp_i2c_write_reg8(I2C_SENSORS, _addr, reg, &v, 1U, 100U);
}

/** Integer square root — the deviation is only ever compared to a threshold
 *  in mg, so a float here would buy nothing and cost the FPU context. */
static uint32_t _isqrt(uint32_t v)
{
  uint32_t rem = 0U, root = 0U;
  for (uint32_t i = 0U; i < 16U; i++)
  {
    root <<= 1U;
    rem = (rem << 2U) | (v >> 30U);
    v <<= 2U;
    if (root < rem)
    {
      rem -= root | 1U;
      root += 2U;
    }
  }
  return root >> 1U;
}

/** Sensitivity 0..100 → wake-up threshold. Linear in the sensor's own
 *  threshold units, which is linear in mg, and inverted: a *higher*
 *  sensitivity must mean a *lower* threshold. */
static void _apply_threshold(void)
{
  uint32_t sens = (_sensitivity > 100U) ? 100U : _sensitivity;
  uint32_t span = MOTION_THS_LSB_MAX - MOTION_THS_LSB_MIN;
  uint32_t lsb  = MOTION_THS_LSB_MIN + (((100U - sens) * span) + 50U) / 100U;

  _ths_lsb = (uint8_t)lsb;
  _ths_mg  = (lsb * LSM6_THS_LSB_UG) / 1000U;

  if (_present)
  {
    (void)_wr(LSM6_WAKE_UP_THS, _ths_lsb);           /* WK_THS[5:0]        */
  }
}

/** Read the three axes, in mg. */
static int32_t _read_axes(int32_t mg[3])
{
  uint8_t raw[6];
  int32_t rc = _rd(LSM6_OUTX_L_A, raw, sizeof(raw));
  if (rc != BSP_OK) { return rc; }

  for (uint32_t i = 0U; i < 3U; i++)
  {
    int16_t v = (int16_t)((uint16_t)raw[2U * i] | ((uint16_t)raw[(2U * i) + 1U] << 8U));
    mg[i] = ((int32_t)v * (int32_t)LSM6_OUT_UG_PER_LSB) / 1000;
  }
  return BSP_OK;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

bool motion_sensor_init(uint8_t sensitivity, uint32_t no_motion_timeout_s)
{
  if (_probed) { return _present; }
  _probed      = true;
  _sensitivity = sensitivity;
  _timeout_s   = no_motion_timeout_s;

  /* The sensors bus is only brought up by system_task when the ToF sensor is
   * compiled in, and it is not — so bring it up here. It is the same call with
   * the same arguments, and this is now its only user. */
  int32_t rc = bsp_i2c_init(I2C_SENSORS, 400000U);
  if (rc != BSP_OK)
  {
    LERROR(TRACE_SHELL, "motion: sensors I2C init failed rc=%ld — no motion "
           "detection this boot", (long)rc);
    _apply_threshold();
    return false;
  }

  for (uint32_t i = 0U; (i < 2U) && !_present; i++)
  {
    _addr = LSM6_ADDRS[i];
    uint8_t who = 0U;
    if ((_rd(LSM6_WHO_AM_I, &who, 1U) == BSP_OK) && (who == LSM6_WHO_AM_I_VALUE))
    {
      _present = true;
      _who     = who;
    }
  }

  if (!_present)
  {
    _addr = 0U;
    LWARNING(TRACE_SHELL, "motion: no inertial sensor answered on the sensors "
             "I2C — motion start/stop will not be produced");
    _apply_threshold();
    return false;
  }

  /* Reset first: a warm reboot leaves the part configured by the previous
   * run, and a half-configured sensor is harder to reason about than a cold
   * one. The bit clears itself when the reset is done. */
  (void)_wr(LSM6_CTRL3_C, 0x01U);                    /* SW_RESET            */
  for (uint32_t i = 0U; i < 20U; i++)
  {
    uint8_t c3 = 0U;
    HAL_Delay(1);
    if ((_rd(LSM6_CTRL3_C, &c3, 1U) == BSP_OK) && ((c3 & 0x01U) == 0U)) { break; }
  }

  (void)_wr(LSM6_CTRL3_C, 0x44U);          /* BDU + IF_INC: 6-byte reads are
                                            * one sample, not two halves     */
  (void)_wr(LSM6_CTRL1_XL, LSM6_CTRL1_XL_CFG);
  (void)_wr(LSM6_TAP_CFG0, 0x41U);         /* INT_CLR_ON_READ + LIR: latch the
                                            * event until we read it, or a
                                            * knock between two polls is lost.
                                            * SLOPE_FDS stays 0 — the slope
                                            * filter feeds the wake-up path and
                                            * leaves the output registers
                                            * showing real gravity, which is
                                            * what `motion read` is for.      */
  (void)_wr(LSM6_WAKE_UP_DUR, LSM6_WAKE_THS_W);   /* WAKE_DUR=0: report the
                                                   * first sample over the
                                                   * threshold               */
  (void)_wr(LSM6_MD1_CFG, 0x20U);          /* route wake-up to INT1. The pin is
                                            * not read by anything here; the
                                            * routing is what makes the latch
                                            * behave in latched mode.         */
  (void)_wr(LSM6_TAP_CFG2, 0x80U);         /* INTERRUPTS_ENABLE — without it
                                            * the embedded functions never run
                                            * and WAKE_UP_SRC stays empty     */
  _apply_threshold();

  /* Clear anything the configuration itself latched. */
  uint8_t src = 0U;
  (void)_rd(LSM6_WAKE_UP_SRC, &src, 1U);

  _last_move = (uint32_t)tx_time_get();
  LINFO(TRACE_SHELL, "motion: LSM6DSO32 at 0x%02X, sensitivity=%u "
        "(threshold %lu mg), no-motion timeout=%lu s",
        (unsigned)_addr, (unsigned)_sensitivity,
        (unsigned long)_ths_mg, (unsigned long)_timeout_s);
  return true;
}

void motion_sensor_config(uint8_t sensitivity, uint32_t no_motion_timeout_s)
{
  _sensitivity = (sensitivity > 100U) ? 100U : sensitivity;
  _timeout_s   = no_motion_timeout_s;
  _apply_threshold();
  LINFO(TRACE_SHELL, "motion: sensitivity=%u (threshold %lu mg), "
        "no-motion timeout=%lu s",
        (unsigned)_sensitivity, (unsigned long)_ths_mg,
        (unsigned long)_timeout_s);
}

bool motion_sensor_present(void) { return _present; }

bool motion_sensor_state(void) { return _active; }

int32_t motion_sensor_read(int32_t mg[3])
{
  if (!_present) { return BSP_ERROR_NO_INIT; }
  return _read_axes(mg);
}

void motion_sensor_force(bool active)
{
  uint32_t now = (uint32_t)tx_time_get();

  _forced = active;
  if (active && !_active)
  {
    _active      = true;
    _started     = now;
    _last_move   = now;
    _peak_dev_mg = 0U;
    _starts++;
    shell_notify_emit(NOTIFY_RSN_MOTION_START, 0U);
  }
  else if (!active && _active)
  {
    _active   = false;
    _ref_valid = false;
    _stops++;
    shell_notify_emit(NOTIFY_RSN_MOTION_STOP,
                      (now - _started) / TX_TIMER_TICKS_PER_SECOND);
  }
}

int32_t motion_sensor_selftest(int32_t *delta_mg)
{
  if (!_present) { return BSP_ERROR_NO_INIT; }

  int32_t before[3] = { 0 }, after[3] = { 0 };
  if (_read_axes(before) != BSP_OK) { return BSP_ERROR_COMPONENT; }

  /* Positive-sign accelerometer self-test: an electrostatic force deflects
   * the proof mass, which is a real mechanical stimulus and therefore also a
   * real wake-up event. Two settling times of ~100 ms bracket the reading —
   * the datasheet asks for the output to settle after the mode change. */
  (void)_wr(LSM6_CTRL5_C, 0x01U);          /* ST_XL = 01 */
  HAL_Delay(120);
  int32_t rc = _read_axes(after);
  HAL_Delay(20);
  (void)_wr(LSM6_CTRL5_C, 0x00U);
  if (rc != BSP_OK) { return BSP_ERROR_COMPONENT; }

  int64_t sum = 0;
  for (uint32_t i = 0U; i < 3U; i++)
  {
    int32_t d = after[i] - before[i];
    sum += (int64_t)d * (int64_t)d;
  }
  int32_t delta = (int32_t)_isqrt((uint32_t)sum);
  if (delta_mg != NULL) { *delta_mg = delta; }

  return ((delta >= MOTION_SELFTEST_MIN_MG) && (delta <= MOTION_SELFTEST_MAX_MG))
         ? BSP_OK : BSP_ERROR_COMPONENT;
}

void motion_sensor_poll(void)
{
  if (!_present || _forced) { return; }

  uint32_t now = (uint32_t)tx_time_get();

  /* Latched hardware event first — reading it clears it. */
  uint8_t src = 0U;
  bool    wake = false;
  if (_rd(LSM6_WAKE_UP_SRC, &src, 1U) == BSP_OK)
  {
    wake = ((src & 0x08U) != 0U);          /* WU_IA */
  }

  int32_t a[3] = { 0 };
  if (_read_axes(a) != BSP_OK) { return; }

  if (!_ref_valid)
  {
    for (uint32_t i = 0U; i < 3U; i++) { _ref_q4[i] = a[i] << MOTION_REF_SHIFT; }
    _ref_valid = true;
  }

  uint32_t sq = 0U;
  for (uint32_t i = 0U; i < 3U; i++)
  {
    int32_t d = a[i] - (_ref_q4[i] >> MOTION_REF_SHIFT);
    sq += (uint32_t)(d * d);
  }
  uint32_t dev = _isqrt(sq);
  _last_dev_mg = dev;

  bool moving = wake || (dev > _ths_mg);

  if (moving)
  {
    _last_move = now;
    if (dev > _peak_dev_mg) { _peak_dev_mg = dev; }

    if (!_active)
    {
      _active  = true;
      _started = now;
      _starts++;
      LINFO(TRACE_SHELL, "motion: started (%s, deviation %lu mg, "
            "threshold %lu mg)", wake ? "sensor wake-up" : "attitude change",
            (unsigned long)dev, (unsigned long)_ths_mg);
      /* rsd carries the deviation that opened the episode, in mg — the one
       * number a receiver can use to tell a nudge from a hard knock. */
      shell_notify_emit(NOTIFY_RSN_MOTION_START, dev);
    }
  }
  else if (_active)
  {
    if ((now - _last_move) >= (_timeout_s * TX_TIMER_TICKS_PER_SECOND))
    {
      uint32_t dur = (now - _started) / TX_TIMER_TICKS_PER_SECOND;
      _active    = false;
      _ref_valid = false;   /* re-baseline: the box may rest at a new angle */
      _stops++;
      LINFO(TRACE_SHELL, "motion: stopped after %lu s still (episode %lu s, "
            "peak %lu mg)", (unsigned long)_timeout_s, (unsigned long)dur,
            (unsigned long)_peak_dev_mg);
      /* rsd carries how long the episode lasted, in seconds. */
      shell_notify_emit(NOTIFY_RSN_MOTION_STOP, dur);
    }
  }
  else
  {
    /* Still: let the reference follow the box. */
    for (uint32_t i = 0U; i < 3U; i++)
    {
      _ref_q4[i] += ((a[i] << MOTION_REF_SHIFT) - _ref_q4[i]) >> MOTION_REF_SHIFT;
    }
  }
}

void motion_sensor_status(t_motion_status *st)
{
  if (st == NULL) { return; }
  memset(st, 0, sizeof(*st));

  st->present      = _present;
  st->who_am_i     = _who;
  st->addr         = _addr;
  st->sensitivity  = _sensitivity;
  st->timeout_s    = _timeout_s;
  st->threshold_mg = _ths_mg;
  st->active       = _active;
  st->forced       = _forced;
  st->starts       = _starts;
  st->stops        = _stops;
  st->last_dev_mg  = _last_dev_mg;
  st->peak_dev_mg  = _peak_dev_mg;
  st->still_s      = _active ? 0U
                    : (((uint32_t)tx_time_get() - _last_move) /
                       TX_TIMER_TICKS_PER_SECOND);
}
