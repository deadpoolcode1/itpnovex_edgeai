/**
 ******************************************************************************
 * @file    motion_sensor.h
 * @brief   Board-movement detection from the LSM6DSO32 inertial sensor.
 *
 * SoW §3.5/§4.5 "motion sensor": this is the sensor that tells us the *unit*
 * is being moved — the box picked up, knocked, tilted or shaken — and it has
 * nothing to do with objects moving inside the camera's field of view. It is
 * what raises the §4.2 motion-start (0x02) and motion-stop (0x04) events, and
 * what the §6 `mtn` field reports.
 *
 * Two knobs, both from `motion sense <sensitivity 0..100> <no_motion_timeout>`
 * and both persisted in the registry:
 *
 *   sensitivity      how hard the box has to be disturbed before it counts.
 *                    100 = the lightest touch, 0 = a deliberate shove.
 *   no-motion timeout how long the box has to be still before motion is
 *                    declared over and the stop event is sent.
 *
 * The detector runs on two independent legs, because one alone misses a real
 * class of movement:
 *
 *   - the sensor's own wake-up function (slope filter, threshold in hardware)
 *     catches a knock that starts and ends between two polls;
 *   - a software comparison of the acceleration vector against its slow
 *     resting average catches a slow lift or tilt, which a slope filter is
 *     deliberately blind to.
 *
 * Everything here is driven from the shell task's loop (`motion_sensor_poll`),
 * which is also where every other notification is composed — so motion events
 * are emitted from the same thread as all the others and need no locking of
 * their own.
 ******************************************************************************
 */
#ifndef _MOTION_SENSOR_H_
#define _MOTION_SENSOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Upper bound on the §4.5 no-motion timeout.
 *
 *  The poll compares `(now - _last_move) >= _timeout_s * TX_TIMER_TICKS_PER_SECOND`
 *  in 32-bit tick arithmetic. At 1000 ticks/s anything above ~4,294,967 s
 *  wraps, so a fat-fingered `motion sense 50 99999999` produced a *tiny*
 *  effective timeout and fired motion-stop almost at once — the exact
 *  opposite of what was asked for. A day is far past any real use and leaves
 *  three orders of magnitude of headroom under the wrap. */
#define MOTION_TIMEOUT_MAX_S    (86400UL)

/** Sensor status, as reported by `motion query`. */
typedef struct
{
  bool     present;        /*!< the part answered WHO_AM_I                  */
  uint8_t  who_am_i;       /*!< what it answered (0x6C = LSM6DSO32)         */
  uint16_t addr;           /*!< 8-bit I2C address it answered on            */
  uint8_t  sensitivity;    /*!< 0..100, as configured                       */
  uint32_t timeout_s;      /*!< no-motion timeout, seconds                  */
  uint32_t threshold_mg;   /*!< what that sensitivity works out to, in mg   */
  bool     active;         /*!< motion in progress right now (§6 `mtn`)     */
  bool     forced;         /*!< state asserted by `motion simulate`         */
  uint32_t starts;         /*!< motion-start events emitted since boot      */
  uint32_t stops;          /*!< motion-stop events emitted since boot       */
  uint32_t last_dev_mg;    /*!< deviation measured on the last poll         */
  uint32_t peak_dev_mg;    /*!< peak deviation of the current/last episode  */
  uint32_t still_s;        /*!< how long the box has been still             */
} t_motion_status;

/**
 * @brief Bring up the inertial sensor and arm the detector.
 * @param sensitivity        0..100
 * @param no_motion_timeout_s seconds of stillness before motion-stop
 * @return true if the sensor answered and was configured.
 * @note Safe to call on a board with no sensor fitted: it reports the absence
 *       once and every later call becomes a no-op, rather than retrying an
 *       I2C transfer for ever from the shell loop.
 */
bool motion_sensor_init(uint8_t sensitivity, uint32_t no_motion_timeout_s);

/**
 * @brief Re-apply the two §4.5 parameters to a running detector.
 * @note Takes effect immediately — `motion sense` is expected to change how
 *       the unit behaves now, not after the next reboot.
 */
void motion_sensor_config(uint8_t sensitivity, uint32_t no_motion_timeout_s);

/**
 * @brief One turn of the detector. Emits §4.2 motion start/stop as required.
 * @note Called from the shell task's loop (~5 Hz). Never blocks for more than
 *       an I2C transfer of a few bytes.
 */
void motion_sensor_poll(void);

/** @brief Is the sensor fitted and answering? */
bool motion_sensor_present(void);

/** @brief Current motion state — the §6 `mtn` field of every notification. */
bool motion_sensor_state(void);

/**
 * @brief Read the acceleration vector, in milli-g.
 * @param mg  filled with x, y, z
 * @return 0 on success.
 * @note At rest one axis reads about ±1000: that is gravity, and it is the
 *       cheapest proof that the part is alive and mounted the way we think.
 */
int32_t motion_sensor_read(int32_t mg[3]);

/**
 * @brief Run the sensor's electrostatic self-test.
 * @param delta_mg  filled with the shift the test produced, in mg
 * @return 0 if the sensor moved as expected.
 * @note This is the only way to make the *sensor* produce motion without a
 *       hand on the box, which is what makes it the bench's end-to-end
 *       stimulus: the step it applies goes through the same filter, the same
 *       threshold and the same state machine as a real shove, so a motion
 *       event that arrives at the server after `motion selftest` proves the
 *       whole chain including the sensor.
 */
int32_t motion_sensor_selftest(int32_t *delta_mg);

/**
 * @brief Assert the motion state by hand (`motion simulate`).
 * @note Exercises the transport only — it bypasses the sensor entirely. Use
 *       `motion selftest` when the sensor is meant to be part of the test.
 */
void motion_sensor_force(bool active);

/** @brief Fill in the status block behind `motion query`. */
void motion_sensor_status(t_motion_status *st);

#ifdef __cplusplus
}
#endif
#endif /* _MOTION_SENSOR_H_ */
