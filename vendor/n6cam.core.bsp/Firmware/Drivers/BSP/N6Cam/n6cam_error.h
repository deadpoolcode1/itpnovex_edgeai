/**
 *******************************************************************************
 * @file    n6cam_error.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam error code definitions
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
#ifndef _N6CAM_ERROR_H_
#define _N6CAM_ERROR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup Error
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* Status codes */
#define BSP_OK                              (0)

/* Error codes (always negative) */
#define BSP_ERROR_NO_INIT                   (-1)
#define BSP_ERROR_PARAMETER                 (-2)
#define BSP_ERROR_COMPONENT                 (-3)
#define BSP_ERROR_PERIPHERAL                (-4)
#define BSP_ERROR_BUSY                      (-5)
#define BSP_ERROR_TIMEOUT                   (-6)
#define BSP_ERROR_NOT_SUPPORTED             (-7)
#define BSP_ERROR_UNKNOWN                   (-8)

/* XSPI errors */
#define BSP_ERROR_XSPI_SUSPENDED            (-20)
#define BSP_ERROR_XSPI_ASSIGN_FAILURE       (-21)
#define BSP_ERROR_XSPI_SETUP_FAILURE        (-22)
#define BSP_ERROR_XSPI_MMP_LOCK_FAILURE     (-23)
#define BSP_ERROR_XSPI_MMP_UNLOCK_FAILURE   (-24)

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
* @} <!-- End: BSP -->
* @} <!-- End: Error -->
*//*--------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif /* _N6CAM_ERROR_H_ */
