/**
 ******************************************************************************
 * @file    main.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   Main N6Cam firmware.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT 2024 SIANA Systems</center></h2>
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
 ******************************************************************************
 */
#ifndef _MAIN_H_
#define _MAIN_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
  
/* Public Definitions --------------------------------------------------------*/

/* Public Macros -------------------------------------------------------------*/

#define IN_PSRAM          IN_SECTION(".psram_bss")
#define IN_SRAM_UNCACHED  IN_SECTION(".uncached_bss")

/* Public Types --------------------------------------------------------------*/

/* Public Data ---------------------------------------------------------------*/

/* Public API ----------------------------------------------------------------*/

#ifdef  __cplusplus
}
#endif
#endif /* _MAIN_H_ */
