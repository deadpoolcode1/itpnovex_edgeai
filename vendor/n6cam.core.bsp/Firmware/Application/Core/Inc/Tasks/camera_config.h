/**
 ******************************************************************************
 * @file    camera_config.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   Camera task configuration.
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2024 SIANA Systems</center></h2>
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
#ifndef _CAMERA_CONFIG_H_
#define _CAMERA_CONFIG_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"
#include "imx335/imx335.h"
#include "vd55g1/vd55g1.h"
#include "vd6g/vd6g.h"
#include "vd1943/vd1943.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Camera
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define USE_IMX335_SENSOR
#define USE_VD55G1_SENSOR
#define USE_VD66GY_SENSOR
#define USE_VD1943_SENSOR

/*-->> Sensor Settings <<------------*/
#define CAMERA_SENSOR_FPS               30U

#define CAMERA_SENSOR_IMX335_ADDRESS    0x34U
#define CAMERA_SENSOR_IMX335_WIDTH      IMX335_WIDTH
#define CAMERA_SENSOR_IMX335_HEIGHT     IMX335_HEIGHT

#define CAMERA_SENSOR_VD55G1_ADDRESS    0x20U
#define CAMERA_SENSOR_VD55G1_WIDTH      VD55G1_MAX_WIDTH
#define CAMERA_SENSOR_VD55G1_HEIGHT     VD55G1_MAX_HEIGHT

#define CAMERA_SENSOR_VD66GY_ADDRESS    0x20U
#define CAMERA_SENSOR_VD66GY_WIDTH      VD6G_MAX_WIDTH
#define CAMERA_SENSOR_VD66GY_HEIGHT     VD6G_MAX_HEIGHT

#define CAMERA_SENSOR_VD1943_ADDRESS    0x20U
#define CAMERA_SENSOR_VD1943_WIDTH      VD1943_MAX_WIDTH
#define CAMERA_SENSOR_VD1943_HEIGHT     VD1943_MAX_HEIGHT

/*-->> PIPE Settings <<--------------*/
/* PIPE 0: Main */
#define CAMERA_MAIN_GAMMA               -1
#define CAMERA_MAIN_BPP                 2U
#define CAMERA_MAIN_PACKER              DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
  /* Small preview to support all cameras while allowing ancillary dumps */
  #define CAMERA_MAIN_RBSWAP            0
  #define CAMERA_MAIN_WIDTH             640U
  #define CAMERA_MAIN_HEIGHT            640U
  #define CAMERA_MAIN_BUFFER_NUM        2U
#else
  /* User defined: preview size */
  #define CAMERA_MAIN_RBSWAP            0
  #define CAMERA_MAIN_WIDTH             800U
  #define CAMERA_MAIN_HEIGHT            600U
  #define CAMERA_MAIN_BUFFER_NUM        2U
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */
#define CAMERA_MAIN_BUFFER_SIZE         (CAMERA_MAIN_WIDTH * CAMERA_MAIN_HEIGHT * CAMERA_MAIN_BPP)

/* PIPE 1: Ancillary */
#define CAMERA_ANCILLARY_GAMMA          -1
#define CAMERA_ANCILLARY_BPP            3U
#define CAMERA_ANCILLARY_PACKER         DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
  /* Big buffer to support 5M buffers */
  #define CAMERA_ANCILLARY_RBSWAP       0
  #define CAMERA_ANCILLARY_WIDTH        3000U
  #define CAMERA_ANCILLARY_HEIGHT       2000U
  #define CAMERA_ANCILLARY_BUFFER_NUM   1U
#else
  /* User defined: NPU input size. yolov8n (full-COCO) takes 256x256x3. */
  #define CAMERA_ANCILLARY_RBSWAP       1
  #define CAMERA_ANCILLARY_WIDTH        256U
  #define CAMERA_ANCILLARY_HEIGHT       256U
  #define CAMERA_ANCILLARY_BUFFER_NUM   2U
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */
#define CAMERA_ANCILLARY_BUFFER_SIZE    (CAMERA_ANCILLARY_WIDTH * CAMERA_ANCILLARY_HEIGHT * CAMERA_ANCILLARY_BPP)

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
* @} <!-- End: Tasks -->
* @} <!-- End: Camera -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _CAMERA_CONFIG_H_ */
