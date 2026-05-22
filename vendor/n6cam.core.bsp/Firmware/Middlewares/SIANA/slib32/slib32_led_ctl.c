/**
 *******************************************************************************
 * @file    slib32_led_ctl.h
 * @author  SIANA Systems
 * @date    2017
 * @brief   Implements a simple LED controller
 * @note    Depends on STM-HAL
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 SIANA Systems. All rights reserved.
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
 *******************************************************************************
 */
#include <string.h>

#include "slib32_led_ctl.h"
#include "slib32_tick.h"

#ifndef SLIB32_HOST
/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup LED_CTL
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

#define DEBUG_LED_CONTROLLER  1U

#if DEBUG_LED_CONTROLLER
  #include "slib32_trace.h"
  #define TRACE_LED(...) trace(TRACE_LEVEL_DEBUG, 0U, "LED", NULL, 0U, __VA_ARGS__)
#else
  #define TRACE_LED(...)
#endif

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Defines the blink-on value when the LED should remain solid on. */
#define LED_ALWAYS_ON  0xFFFFFFFF

/** Defines the blink-on value when the LED should remain off. */
#define LED_ALWAYS_OFF 0

/** defines the pattern index for the built-in On/Off pattern */
#define LP_LED_OFF_IDX	(-1)
#define LP_LED_ON_IDX   (-2)

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/** Built-in solid on/off patterns */
static const t_led_pattern _led_off = {0, LED_ALWAYS_OFF, 0};
static const t_led_pattern _led_on = {LED_ALWAYS_ON, 0, 0};

/** Holds the supported system LEDs */
static t_led *_leds;
static uint8_t _max_leds;

/** Holds the user-defined LED patterns */
static const t_led_pattern *_patterns;
static uint32_t _max_patterns;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _restore_previous_pattern(uint32_t which_led);
static void _process_blinking_pattern(int idx);
static bool _is_pattern_continuous(const t_led_pattern *pattern);
static bool _is_pattern_off(const t_led_pattern *pattern);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

bool led_init_ctl(t_led *LEDs, uint8_t maxLEDs, const t_led_pattern *patterns, uint32_t maxPatterns)
{
  // set the list of LEDs
  _leds = (t_led*)LEDs;
  _max_leds = maxLEDs;

  // set the list of user-defined patterns
  _patterns = patterns;
  _max_patterns = maxPatterns;

  /* Initialize all LEDs in OFF pattern... */
  for (int i = 0; i < _max_leds; i++)
  {
    memcpy(&_leds[i].active_pattern, &_led_off, sizeof(t_led_pattern));
  }

  return true;
}

bool led_yield(void)
{
  bool is_busy = false;

  /* for each LED... */
  for (int idx = 0; idx < _max_leds; idx++)
  {
    /* process if blinking... */
    if (_leds[idx].active_pattern.num_of_blinks != 0)
    {
      is_busy = true;
      _process_blinking_pattern(idx);
    }
  }

  return is_busy;
}

void led_set(int32_t led_idx, t_led_state state)
{
  if (state == LED_ON)
  {
    led_set_pattern(led_idx, LP_LED_ON_IDX);
  }
  else
  {
    led_set_pattern(led_idx, LP_LED_OFF_IDX);
  }
}

void led_setAll(t_led_state state)
{
  /* set each LED... */
  for (int idx = 0; idx < _max_leds; idx++)
  {
    led_set(idx, state);
  }
}

void led_set_pattern(int32_t led_idx, int32_t pattern_idx)
{
  const t_led_pattern *pattern;

  // select the target pattern...
  if (pattern_idx == LP_LED_OFF_IDX)
  {
    pattern = &_led_off;
  }
  else if (pattern_idx == LP_LED_ON_IDX)
  {
    pattern = &_led_on;
  }
  else if (pattern_idx < _max_patterns)
  {
    pattern = &_patterns[pattern_idx];
  }
  else
  {
    /* ERROR => invalid pattern! */
    TRACE_LED("ERROR: invalid pattern %u", pattern_idx);
    return;
  }

  // process the target LED...
  if (led_idx < _max_leds)
  {
    /* target LED */
    t_led *led = &_leds[led_idx];

    TRACE_LED("Set LED[%u] -> pattern[%u]", led_idx, pattern_idx);

    /* is current pattern continuous? (runs forever) */
    if (_is_pattern_continuous(&led->active_pattern) == true)
    {
      /* yes -> back it up to restore once new pattern is completed */
      memcpy(&led->prev_pattern, &led->active_pattern, sizeof(t_led_pattern));
    }

    /** Activate new pattern **/

    memcpy(&led->active_pattern, pattern, sizeof(t_led_pattern));

    /* start in On-State */
    led->ctl.is_on = true;
    led->ctl.ticks = HAL_GetTick();
    /* reset blinking logic */
    led->ctl.blinks = 0;

    /* is On-State actually On? */
    if (_is_pattern_off(&led->active_pattern) == false)
    {
      /* LED ON */
      GPIO_SET(led->io);
    }
    else
    {
      /* LED OFF */
      GPIO_RESET(led->io);
    }
  }
}

bool led_get_pattern(int32_t led_idx, t_led_pattern *pattern)
{
  /* sanity check... */
  if (led_idx >= _max_leds)
  {
    return false;
  }

  /* copy the active pattern... */
  *pattern = _leds[led_idx].active_pattern;

  return true;
}

bool led_is_active(int32_t led_idx)
{
  return (_is_pattern_continuous(&_leds[led_idx].active_pattern) == false);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief processes an active blinking pattern.
 * @param idx   LED.index of the active pattern to process.
 * @retval none
 */
static void _process_blinking_pattern(int idx)
{
  /* target LED instance */
  t_led *led = &_leds[idx];

  /* is LED OFF? */
  if (led->ctl.is_on == false)
  {
    /* reached end of current blink.OFF period? */
    if (tick_is_expired(led->ctl.ticks, led->active_pattern.blink_time_off))
    {
      /* yes -> process next period, if any... */

      /* is next period valid?
       *  - either always blinking or...
       *  - not blinked enough
       */
      if ((led->active_pattern.num_of_blinks == LED_ALWAYS_BLINK) ||
        (++led->ctl.blinks < led->active_pattern.num_of_blinks))
      {
        /* start next blink.ON period */
        led->ctl.is_on = true;
        led->ctl.ticks = HAL_GetTick();

        /* is On-State actually On? */
        if (_is_pattern_off(&led->active_pattern) == false)
        {
          GPIO_SET(led->io);
        }
      }
      /* end of blinking.pattern */
      else
      {
        /* was previous pattern continuous?  */
        if (_is_pattern_continuous(&led->prev_pattern) == true)
        {
          /* yes... */
          _restore_previous_pattern(idx);
        }
        else
        {
          /* no -> then LED remains OFF */
          led_set_pattern(idx, LP_LED_OFF_IDX);
        }
      }
    }
  }
  /* LED is on */
  else
  {
    /* reached end of current blink.ON period? */
    if (tick_is_expired(led->ctl.ticks, led->active_pattern.blink_time_on))
    {
      /* yes -> start next blink.OFF period */
      GPIO_RESET(led->io);
      led->ctl.is_on = false;
      led->ctl.ticks = HAL_GetTick();
    }
  }
}

/**
 * @brief restores the previous pattern.
 *
 * This function is used when blinking active pattern is completed and the
 * previous pattern needs to be resumed.
 * @param idx   LED.index of the previous patter to restore.
 * @retval none
 */
static void _restore_previous_pattern(uint32_t idx)
{
  if (idx < _max_leds)
  {
    /* target LED */
    t_led *led = &_leds[idx];

    /* restore previous pattern if continuous... */
    if (_is_pattern_continuous(&led->prev_pattern) == true)
    {
      memcpy(&led->active_pattern, &led->prev_pattern, sizeof(t_led_pattern));
    }

    /* reset in On-State */
    led->ctl.is_on = true;
    led->ctl.ticks = HAL_GetTick();
    led->ctl.blinks = 0;

    /* is On-State actual On? */
    if (_is_pattern_off(&led->active_pattern) == true)
    {
      /* LED OFF */
      GPIO_RESET(led->io);
    }
    else /* all other patterns start with the LED on */
    {
      /* LED ON */
      GPIO_SET(led->io);
    }
  }
}

/**
 * @brief Determines whether a pattern is continuous, i.e. runs forever.
 *
 * @param pattern	ptr. to pattern.
 * @retval true if continuous, false if temporary.
 */
bool _is_pattern_continuous(const t_led_pattern *pattern)
{
  return (
    (pattern->num_of_blinks == LED_ALWAYS_BLINK) ||
    (pattern->blink_time_on == LED_ALWAYS_ON) ||
    (pattern->blink_time_off == LED_ALWAYS_OFF)
  );
}

/** @brief Determines if a pattern is the OFF pattern.
 *
 * @param pattern	ptr. to pattern.
 * @retval true if continuous, false if temporary.
 */
bool _is_pattern_off(const t_led_pattern *pattern)
{
  bool always_off = (
    (pattern->num_of_blinks == _led_off.num_of_blinks) &&
    (pattern->blink_time_on == _led_off.blink_time_on) &&
    (pattern->blink_time_off == _led_off.blink_time_off)
  );
  bool on_is_zeroed = (pattern->blink_time_on == 0);

  return (always_off || on_is_zeroed);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Controllers -->
* @} <!-- End: LED_CTL -->
*//*--------------------------------------------------------------------------*/
#endif /* SLIB32_HOST */
