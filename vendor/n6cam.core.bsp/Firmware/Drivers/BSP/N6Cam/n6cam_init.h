/**
 *******************************************************************************
 * @file    n6cam_init.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam initialization utilities API
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
#ifndef _N6CAM_INIT_H_
#define _N6CAM_INIT_H_
#ifdef  __cplusplus
extern "C" {
#endif

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup INIT
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
 * @brief IAC configuration to trap illegal access events
 *
 * Run this after HAL_Init
 * IAC_IRQHandler needs to be defined
 */
void bsp_config_iac(void);

/**
 * @brief OTP fuses configuration
 *
 * Run this after clocks are configured
 */
void bsp_config_fuse(void);

/**
 * @brief Power configuration
 *
 * Run this before clocks are configured
 */
void bsp_config_pwr(void);

/**
 * @brief Security configuration
 *
 * Run this after HAL_Init
 */
void bsp_config_risaf(void);

/**
 * @brief System fixes
 *
 * Run this before HAL_Init
 *
 * Based on reference projects (Simple, UVC)
 */
void bsp_system_fixes(void);

/**
 * @brief USB fixes
 *
 * Run this before HAL_Init
 *
 * Based on reference projects (UVC), USB1:
 * - Peripheral/IT reset
 * - Clock disabled
 */
void bsp_usb_fixes(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Controllers -->
* @} <!-- End: INIT -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_INIT_H_ */
