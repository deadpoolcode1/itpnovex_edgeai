/**
 *******************************************************************************
 * @file    slib32_led_ctl.h
 * @author  SIANA Systems
 * @date    2017
 * @brief   Defines the API for a simple LED controller
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
#ifndef _SLIB32_LED_CTL_H_
#define _SLIB32_LED_CTL_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "slib32_gpio.h"

#ifndef SLIB32_HOST
#include <stdbool.h>

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup LED_CTL
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Defines num_of_blinks when the LED should keep blinking forever.
 * @note will blink forever or until the next pattern is set.
 */
#define LED_ALWAYS_BLINK 0xFFFFFFFF

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief define the state of an LED.
 */
typedef enum
{
  LED_ON,
  LED_OFF
} t_led_state;

/**
 * @brief represents an LED blinking pattern.
 */
typedef struct
{
  uint32_t blink_time_on;       /*!< turn-on time in msec       */
  uint32_t blink_time_off;      /*!< turn-off time in msec      */
  uint32_t num_of_blinks;       /*!< # of blinks                */
} t_led_pattern;

/**
 * @brief represents an LED.
 */
typedef struct
{
  const t_gpio *io;             /*!< associated GPIO            */
  t_led_pattern active_pattern; /*!< currently active pattern   */
  t_led_pattern prev_pattern;   /*!< previous pattern           */
  struct state
  {
    bool is_on;
    uint32_t ticks;
    uint32_t blinks;
  } ctl;                        /*!< LED control state          */
} t_led;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initializes the LED controller.
 *
 * @param LEDs          ptr. to array of LEDs.
 * @param maxLEDs       # of LEDs in array.
 * @param patterns      ptr. to array of LED patterns.
 * @param maxPatterns   # of patterns in array.
 * @retval true on SUCCESS, false otherwise.
 */
bool led_init_ctl(t_led *LEDs, uint8_t maxLEDs, const t_led_pattern *patterns, uint32_t maxPatterns);

/**
 * @brief  Runs the LED controller.
 *
 * @warning Needs to be called periodically!
 * @retval true if busy, false otherwise.
 */
bool led_yield(void);

/**
 * @brief Sets the specified LED to either solid On or Off.
 *
 * @param led_idx		Index of the target LED.
 * @param state			Desired LED state
 * @retval none
 */
void led_set(int32_t led_idx, t_led_state state);

/**
 * @brief Sets ALL LEDs to either solid On or Off.
 *
 * @param state			Desired LED state
 * @retval none
 */
void led_setAll(t_led_state state);

/**
 * @brief  Starts a new pattern for the specified LED.
 *
 * @param  led_idx      Index of the target LED.
 * @param  pattern      the requested LED pattern.
 * @retval none
 */
void led_set_pattern(int32_t led_idx, int32_t pattern_idx);

/**
 * @brief returns the active pattern for the specified LED.
 *
 * @note this function is intended for unit-testing.
 *
 * @param led_idx		Index of the target LED.
 * @param pattern		ptr. to the destination pattern.
 * @retval true on success, false otherwise.
 */
bool led_get_pattern(int32_t led_idx, t_led_pattern *pattern);

/**
 * @brief Returns whether the LED is busy processing a temporary pattern.
 *
 * You can use this function to check if the current pattern is still being
 * processed. For example, when you want to play another pattern but do not
 * want to interrupt the currently playing temporary pattern.
 *
 * @note An ALWAYS blinking pattern is not considered a temporary pattern!
 *
 * @param led_idx		Index of the target LED.
 * @retval true if LED is active, false otherwise.
 */
bool led_is_active(int32_t led_idx);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Controllers -->
* @} <!-- End: LED_CTL -->
*//*--------------------------------------------------------------------------*/
#endif /* SLIB32_HOST */
#ifdef __cplusplus
}
#endif
#endif  /* _SLIB32_LED_CTL_H_ */
