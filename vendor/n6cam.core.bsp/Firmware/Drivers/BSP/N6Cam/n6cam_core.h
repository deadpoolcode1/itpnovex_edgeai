/**
 *******************************************************************************
 * @file    n6cam_core.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam CORE API
 *******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
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
#ifndef _N6CAM_CORE_H_
#define _N6CAM_CORE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_error.h"
#include "n6cam_io.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup CORE
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

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

/** Available IOs */
typedef enum
{
  IO_START = 0U,
  IO_BTN_USER1 = IO_START,
  IO_LED_USER1,
  IO_LED_USER2,
  #if defined(N6CAM)
  IO_LED_USER3,
  IO_LED_CAMERA,
  IO_LED_POWER,
  IO_HW_ID1,
  IO_HW_ID2,
  IO_HW_ID3,
  #endif /* BOARD */
  IO_PWR_CAMERA_EN,
  #if defined(STM32N6DK_C02)
  IO_PWR_CAMERA_RST,
  #endif /* BOARD */
  IO_PWR_SD_EN,
  IO_PWR_SD_SEL,
  IO_PWR_USB1_EN,
  IO_SD_DETECT,
  #if defined(N6CAM)
  IO_SPI3_CS1,
  IO_SPI4_CS1,
  IO_SPI5_CS1,
  IO_SPI5_CS2,
  IO_SPI5_CS3,
  IO_SPI5_CS4,
  #endif /* BOARD */
  IO_NUM,
} t_io_id;

/** Available Buttons */
typedef enum
{
  BTN_START = 0U,
  BTN_USER1 = BTN_START,
  BTN_NUM,
} t_btn_id;

/** Available LEDs */
typedef enum
{
  LED_START = 0U,
  LED_USER1 = LED_START,
  LED_USER2,
  #if defined(N6CAM)
  LED_USER3,
  LED_CAMERA,
  LED_POWER,
  #endif /* BOARD */
  LED_NUM,
} t_led_id;

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
 * @brief Initializes IO
 * @param id IO ID
 * @return Error code
 */
int32_t bsp_io_init(t_io_id id);

/**
 * @brief Deinitializes IO
 * @param id IO ID
 * @return Error code
 */
int32_t bsp_io_deinit(t_io_id id);

/**
 * @brief Toggles IO state
 * @param id IO ID
 * @return Error code
 */
int32_t bsp_io_toggle_state(t_io_id id);

/**
 * @brief Sets IO state
 * @param id    IO ID
 * @param state Desired state
 * @return Error code
 */
int32_t bsp_io_set_state(t_io_id id, bool state);

/**
 * @brief Gets IO state
 * @param id IO ID
 * @return State
 */
bool bsp_io_get_state(t_io_id id);

/**
 * @brief IO IRQ callback
 *
 * @note  To be implemented by user application
 *
 * @param id      IO ID
 * @param rising  true if rising edge, false if falling edge
 */
void bsp_io_irq_handler(t_io_id id, bool rising);

/**
 * @brief Unknown IO IRQ callback
 *
 * @note  To be implemented by user application
 *
 * @param pin     GPIO pin number
 * @param rising  true if rising edge, false if falling edge
 */
void bsp_io_unknown_irq_handler(uint16_t pin, bool rising);

/**
 * @brief Initializes Button
 * @param id Button ID
 * @return Error code
 */
int32_t bsp_btn_init(t_btn_id id);

/**
 * @brief Deinitializes Button
 * @param id Button ID
 * @return Error code
 */
int32_t bsp_btn_deinit(t_btn_id id);

/**
 * @brief Gets Button state
 * @param id Button ID
 * @return State
 */
bool bsp_btn_get_state(t_btn_id id);

/**
 * @brief Button IRQ callback
 * @param id      Button ID
 * @param rising  True if rising edge, false if falling edge
 */
void bsp_btn_irq_handler(t_btn_id id, bool rising);

/**
 * @brief Initializes Hardware ID
 */
void bsp_hw_id_init(void);

/**
 * @brief Gets Hardware ID
 * @return Hardware ID
 */
uint8_t bsp_hw_id_get(void);

/**
 * @brief Gets Hardware ID string
 * @return Hardware ID string
 */
const char *bsp_hw_id_get_string(void);

/**
 * @brief Initializes LED
 * @param id LED ID
 * @return Error code
 */
int32_t bsp_led_init(t_led_id id);

/**
 * @brief Deinitializes LED
 * @param id LED ID
 * @return Error code
 */
int32_t bsp_led_deinit(t_led_id id);

/**
 * @brief Turns LED on
 * @param id LED ID
 * @return Error code
 */
int32_t bsp_led_on(t_led_id id);

/**
 * @brief Turns LED off
 * @param id LED ID
 * @return Error code
 */
int32_t bsp_led_off(t_led_id id);

/**
 * @brief Toggles LED state
 * @param id LED ID
 * @return Error code
 */
int32_t bsp_led_toggle(t_led_id id);

/**
 * @brief Sets LED state
 * @param id    LED ID
 * @param state Desired state
 * @return Error code
 */
int32_t bsp_led_set_state(t_led_id id, bool state);

/**
 * @brief Gets LED state
 * @param id LED ID
 * @return State
 */
bool bsp_led_get_state(t_led_id id);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: CORE -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_CORE_H_ */
