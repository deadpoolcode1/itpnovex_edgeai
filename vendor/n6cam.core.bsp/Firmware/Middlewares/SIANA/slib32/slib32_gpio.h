/**
 *******************************************************************************
 * @file    slib32_gpio.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the GPIO helpers
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
#ifndef _SLIB32_GPIO_H_
#define _SLIB32_GPIO_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "slib32_core.h"

#ifndef SLIB32_HOST
#include "stm32_hal.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup GPIO
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Represents a single GPIO.
 */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} t_gpio;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Sets the output state to HIGH.
 *
 * @param io   ptr. to gpio_t instance.
 * @return none
 */
#define GPIO_nSET(io)     HAL_GPIO_WritePin(io.port, io.pin, GPIO_PIN_SET)
#define GPIO_SET(io)      HAL_GPIO_WritePin(io->port, io->pin, GPIO_PIN_SET)

/**
 * @brief Sets the output state to LOW.
 *
 * @param io   ptr. to gpio_t instance.
 * @return none
 */
#define GPIO_nRESET(io)   HAL_GPIO_WritePin(io.port, io.pin, GPIO_PIN_RESET)
#define GPIO_RESET(io)    HAL_GPIO_WritePin(io->port, io->pin, GPIO_PIN_RESET)

/**
 * @brief Toggles the output state.
 *
 * @param io   ptr. to gpio_t instance.
 * @return none
 */
#define GPIO_nTOGGLE(io)  HAL_GPIO_TogglePin(io.port, io.pin)
#define GPIO_TOGGLE(io)   HAL_GPIO_TogglePin(io->port, io->pin)

/**
 * @brief Reads the input state.
 *
 * @param io   ptr. to gpio_t instance.
 * @return HAL::GPIO_PinState
 */
#define GPIO_nREAD(io)    HAL_GPIO_ReadPin(io.port, io.pin)
#define GPIO_READ(io)     HAL_GPIO_ReadPin(io->port, io->pin)

/**
 * @brief locks the GPIO configuration.
 *
 * @param io   ptr. to gpio_t instance.
 * @return none
 */
#define GPIO_nLOCK(io)    HAL_GPIO_LockPin(io.port, io.pin)
#define GPIO_LOCK(io)     HAL_GPIO_LockPin(io->port, io->pin)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Controllers -->
* @} <!-- End: GPIO -->
*//*--------------------------------------------------------------------------*/
#endif /* SLIB32_HOST */
#ifdef __cplusplus
}
#endif
#endif /* _SLIB32_GPIO_H_ */
