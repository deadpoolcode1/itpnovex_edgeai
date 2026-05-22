/**
 *******************************************************************************
 * @file    stm32_hal.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the STM32 HAL API for this project
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
#ifndef _STM32_HAL_H_
#define _STM32_HAL_H_
#ifdef  __cplusplus
extern "C" {
#endif
/******************************************************************************/

/* Add STM32 HAL includes for your specific MCU */
#include "stm32n6xx.h"
#include "stm32n6xx_hal.h"

/* BOARD DEFINITIONS ----------------*/
#if defined(N6CAM_WIFI_MURATA)
  /* All variants share core definitions
   * TODO: Add all variants */
  #define N6CAM
#endif /* N6CAM */

/******************************************************************************/
#ifdef  __cplusplus
}
#endif
#endif /* _STM32_HAL_H_ */
