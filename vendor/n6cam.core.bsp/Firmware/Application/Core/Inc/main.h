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

#include "slib32_core.h"
#include "stm32n6xx_hal.h"
  
/* Public Definitions --------------------------------------------------------*/

/* Public Macros -------------------------------------------------------------*/

/* PSRAM is 32 MB but not all of it is ours: the generated model owns
 * 0x91a00000..0x91a80000 (see the MEMORY block in STM32N657xx.ld). IN_PSRAM is
 * the 26 MB below that pool; IN_PSRAM_HI is the 5.5 MB above it. Both are the
 * same memory, so the choice is only about which side of the pool a buffer
 * lands on, and the linker now refuses either that does not fit. */
#define IN_PSRAM          IN_SECTION(".psram_bss")
#define IN_PSRAM_HI       IN_SECTION(".psram_hi_bss")
#define IN_SRAM_UNCACHED  IN_SECTION(".uncached_bss")

/* Public Types --------------------------------------------------------------*/

/* Public Data ---------------------------------------------------------------*/

/* Public API ----------------------------------------------------------------*/

#ifdef  __cplusplus
}
#endif
#endif /* _MAIN_H_ */
