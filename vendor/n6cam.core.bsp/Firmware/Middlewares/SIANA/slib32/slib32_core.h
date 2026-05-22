/**
 *******************************************************************************
 * @file    slib32_core.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines core macros and utilities
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
#ifndef _SLIB32_CORE_H_
#define _SLIB32_CORE_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Core
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Host environment */
#if defined(HOST) || defined(SIMULATOR)
  #define SLIB32_HOST
#endif

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Align generic */
#ifndef ALIGN
  #define ALIGN(x)            __attribute__ ((aligned(x)))
  #define IS_ALIGNED(x, ptr)  (((uint32_t)(ptr) & ((x) - 1U)) == 0U)
#endif

/** Align 32byte
 * Use on DMA buffers */
#ifndef DMA_ALIGN
  #define DMA_ALIGN           ALIGN(32U)
  #define DMA_ALIGNED(x)      IS_ALIGNED(32U, x)
#endif

/** Align 16bit */
#ifndef U16_ALIGN
  #define U16_ALIGN           ALIGN(2U)
  #define U16_ALIGNED(x)      IS_ALIGNED(2U, x)
#endif

/** Align 32bit */
#ifndef U32_ALIGN
  #define U32_ALIGN           ALIGN(4U)
  #define U32_ALIGNED(x)      IS_ALIGNED(4U, x)
#endif

/** In section */
#ifndef IN_SECTION
  #define IN_SECTION(x)       __attribute__ ((section(x)))
#endif

/** Keep */
#ifndef KEEP
  #define KEEP                __attribute__ ((used))
#endif

/** Optimize (define flags) */
#ifndef OPTIMIZE
  #define OPTIMIZE(x)         __attribute__ ((optimize(x)))
#endif

/** No optimize */
#ifndef NO_OPTIMIZE
  #define NO_OPTIMIZE         OPTIMIZE("O0")
#endif

/** Packed implementation */
#ifndef PACKED
  #define PACKED              __attribute__ ((packed))
#endif

/** Weak implementation
 * Note: This could be not supported by all compilers.
 */
#ifndef WEAK
  #ifdef _WIN32
    /* This is not supported for now */
    #define WEAK
  #elif defined(__weak)
    #define WEAK              __weak
  #else
    #define WEAK              __attribute__ ((weak))
  #endif
#endif

/** Explicitly indicates a parameter as unused */
#ifndef UNUSED
  #define UNUSED(x)           ((void)(x))
#endif

/** Get number of elements in an array */
#ifndef ARRAY_SIZE
  #define ARRAY_SIZE(a)       (sizeof(a) / sizeof(a[0]))
#endif

/** Get sign of a value */
#ifndef SIGN
  #define SIGN(a)             (((a) > 0) - ((a) < 0))
#endif

/** Get absolute value */
#ifndef ABS
  #define ABS(a)              (SIGN(a) * (a))
#endif

/** Get maximum value between a and b */
#ifndef MAX
  #define MAX(a, b)           (((a) > (b)) ? (a) : (b))
#endif

/** Get minimum value between a and b */
#ifndef MIN
  #define MIN(a, b)           (((a) < (b)) ? (a) : (b))
#endif

/** Concatenate */
#ifndef CONCAT
  #define CONCAT_NX(a, b)     a ## b
  #define CONCAT(a, b)        CONCAT_NX(a, b)
#endif

/** Stringify */
#ifndef STRINGIFY
  #define STRINGIFY_NX(x)     #x
  #define STRINGIFY(x)        STRINGIFY_NX(x)
#endif

/** Get container struct for a given member */
#ifndef GET_CONTAINER_OF
  #define GET_CONTAINER_OF(ptr, type, member) \
    (type*)((uint8_t*)ptr - offsetof(type, member))
#endif

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Core -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_CORE_H_ */
