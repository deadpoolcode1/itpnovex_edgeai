/**
 *******************************************************************************
 * @file    slib32_memory.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implement fast and alignment-aware memory utilities
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

#include "slib32_core.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Memory
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

/** MEMORY Optimize for size */
#ifndef SLIB32_MEMORY_OPTIMIZE_FOR_SIZE
  #define SLIB32_MEMORY_OPTIMIZE_FOR_SIZE 1U
#endif /* SLIB32_MEMORY_OPTIMIZE_FOR_SIZE */

/** MEMORY Enable memcpy */
#ifndef SLIB32_MEMCPY_ENABLE
  #define SLIB32_MEMCPY_ENABLE            1U
#endif /* SLIB32_MEMCPY_ENABLE */

/** MEMORY Enable memset */
#ifndef SLIB32_MEMSET_ENABLE
  #define SLIB32_MEMSET_ENABLE            1U
#endif /* SLIB32_MEMSET_ENABLE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Number of bytes copied per 4x unrolled iteration */
#define MEMORY_BLOCK_BIG      (16U)

/** Number of bytes copied per iteration */
#define MEMORY_BLOCK_SMALL    (4U)

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

#if SLIB32_MEMCPY_ENABLE == 1U
void *slib32_memcpy(void *dst, const void *src, size_t size);
#endif /* SLIB32_MEMCPY_ENABLE */

#if SLIB32_MEMSET_ENABLE == 1U
void *slib32_memset(void *dst, int value, size_t size);
#endif /* SLIB32_MEMSET_ENABLE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

#if SLIB32_MEMCPY_ENABLE == 1U
void *memcpy(void *dst, const void *src, size_t size)
{
  return slib32_memcpy(dst, src, size);
}
#endif /* SLIB32_MEMCPY_ENABLE */

#if SLIB32_MEMSET_ENABLE == 1U
void *memset(void *dst, int value, size_t size)
{
  return slib32_memset(dst, value, size);
}
#endif /* SLIB32_MEMSET_ENABLE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

#if SLIB32_MEMCPY_ENABLE == 1U
void OPTIMIZE("-fno-tree-loop-distribute-patterns") *slib32_memcpy (void *dst, const void *src, size_t size)
{
  uint8_t       *dst_u = dst;
  const uint8_t *src_u = src;

#if SLIB32_MEMORY_OPTIMIZE_FOR_SIZE == 1U
  /* Perform efficient copy */
  if (
    (size >= MEMORY_BLOCK_BIG)  &&  /* Sufficient data... */
    U32_ALIGNED(dst)            &&  /* and aligned pointers */
    U32_ALIGNED(src)
  )
  {
    uint32_t        *dst_a = (uint32_t*)dst;
    const uint32_t  *src_a = (const uint32_t*)src;

    /* Unrolled iteration */
    while (size >= MEMORY_BLOCK_BIG)
    {
      *dst_a++ = *src_a++;
      *dst_a++ = *src_a++;
      *dst_a++ = *src_a++;
      *dst_a++ = *src_a++;
      size    -= MEMORY_BLOCK_BIG;
    }

    /* Simple iteration */
    while (size >= MEMORY_BLOCK_SMALL)
    {
      *dst_a++ = *src_a++;
      size    -= MEMORY_BLOCK_SMALL;
    }

    /* For missing bytes, use byte copy */
    dst_u = (uint8_t*)dst_a;
    src_u = (const uint8_t*)src_a;
  }
#endif /* SLIB32_MEMORY_OPTIMIZE_FOR_SIZE */

  /* Perform byte copy */
  while (size--)
  {
    *dst_u++ = *src_u++;
  }
  return dst;
}
#endif /* SLIB32_MEMCPY_ENABLE */

#if SLIB32_MEMSET_ENABLE == 1U
void OPTIMIZE("-fno-tree-loop-distribute-patterns") *slib32_memset (void *dst, int value, size_t size)
{
  uint8_t *dst_u = dst;
  uint8_t val8   = (uint8_t)value;

#if SLIB32_MEMORY_OPTIMIZE_FOR_SIZE == 1U
  /* Handle unaligned start */
  while (size && !U32_ALIGNED(dst_u))
  {
    *dst_u++ = val8;
    size--;
  }

  /* Perform efficient copy */
  if (size >= MEMORY_BLOCK_BIG)
  {
    uint32_t *dst_a = (uint32_t*)dst_u;
    uint32_t val32  = (uint32_t)((val8 << 24U) | (val8 << 16U) | (val8 << 8U) | val8);

    /* Unrolled iteration */
    while (size >= MEMORY_BLOCK_BIG)
    {
      *dst_a++ = val32;
      *dst_a++ = val32;
      *dst_a++ = val32;
      *dst_a++ = val32;
      size    -= MEMORY_BLOCK_BIG;
    }

    /* Simple iteration */
    while (size >= MEMORY_BLOCK_SMALL)
    {
      *dst_a++ = val32;
      size    -= MEMORY_BLOCK_SMALL;
    }

    /* For missing bytes, use byte copy */
    dst_u = (uint8_t*)dst_a;
  }
#endif /* SLIB32_MEMORY_OPTIMIZE_FOR_SIZE */

  /* Perform byte copy */
  while (size--)
  {
    *dst_u++ = val8;
  }
  return dst;
}
#endif /* SLIB32_MEMSET_ENABLE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Memory -->
*//*--------------------------------------------------------------------------*/
