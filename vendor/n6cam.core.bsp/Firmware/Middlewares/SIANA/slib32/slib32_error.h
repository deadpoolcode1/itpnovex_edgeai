/**
 *******************************************************************************
 * @file    slib32_error.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines error codes
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
#ifndef _SLIB32_ERROR_H_
#define _SLIB32_ERROR_H_
#ifdef  __cplusplus
extern "C" {
#endif

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Error
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* Status codes */
#define SLIB32_OK                 (0)     /*!< No error */
#define SLIB32_RESET              (1)     /*!< */
#define SLIB32_UPDATED            (2)     /*!< */

/* Error codes (always negative) */
#define SLIB32_ERROR_INIT         (-1)    /*!< Generic error */
#define SLIB32_ERROR_PARAMETER    (-2)    /*!< Invalid parameter */
#define SLIB32_ERROR_EMPTY        (-3)    /*!< Is empty */
#define SLIB32_ERROR_FULL         (-4)    /*!< Is full */
#define SLIB32_ERROR_INDEX        (-5)    /*!< Index out of bounds */
#define SLIB32_ERROR_OVERFLOW     (-6)    /*!< Is overflowed */
#define SLIB32_ERROR_ENCODE       (-7)    /*!< Encoding failure */
#define SLIB32_ERROR_DECODE       (-8)    /*!< Decoding failure */
#define SLIB32_ERROR_LOAD         (-9)    /*!< Load error */
#define SLIB32_ERROR_SAVE         (-10)   /*!< Save error */

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
* @} <!-- End: Error -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_ERROR_H_ */
