/**
 *******************************************************************************
 * @file    registry.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Registry data structure definitions
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
#ifndef _REGISTRY_H_
#define _REGISTRY_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Registry
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Registry version
 *
 * IMPORTANT: When adding new entries to the registry, increment this version.
 */
#define REGISTRY_VERSION     0x00000001U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Registry data entries
 *
 * When adding new entries:
 *   1. Define the new entry below the existing (with native type).
 *   2. Add the default value to the registry_defaults structure.
 *   3. Repeat for all entries.
 *
 * IMPORTANT: When done, increment REGISTRY_VERSION.
 *
 * IMPORTANT: The order of the entries must not be changed, as it will break
 * compatibility with the previous versions of the registry.
 */
typedef struct
{
  /* Demo */
  uint32_t dummy;
} t_registry_data;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

#ifdef SLIB32_REGISTRY_DECLARATION
/** Default registry data values */
const t_registry_data registry_defaults =
{
  /* Demo */
  .dummy = 0xDEADBEEF,
};

#endif /* SLIB32_REGISTRY_DECLARATION */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Registry -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _REGISTRY_H_ */
