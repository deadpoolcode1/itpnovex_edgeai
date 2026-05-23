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
#define REGISTRY_VERSION        4U

/* Wifi */
#define WIFI_SSID_LEN           31U
#define WIFI_SSID_STR_LEN       (WIFI_SSID_LEN + 1U)
#define WIFI_PASS_LEN           63U
#define WIFI_PASS_STR_LEN       (WIFI_PASS_LEN + 1U)
#define WIFI_HOSTNAME_LEN       31U
#define WIFI_HOSTNAME_STR_LEN   (WIFI_HOSTNAME_LEN + 1U)
#define WIFI_SERVER_LEN         31U
#define WIFI_SERVER_STR_LEN     (WIFI_SERVER_LEN + 1U)

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
  /* V1 -------------------*/
  /* Demo */
  uint32_t  dummy;

  /* Camera */
  uint8_t   camera_aec_enable;
  int32_t   camera_aec_ev;
  uint8_t   camera_awb_enable;
  uint8_t   camera_awb_profile;
  uint32_t  camera_gain;
  uint32_t  camera_exposure;

  /* Wifi */
  uint8_t   wifi_ssid[WIFI_SSID_STR_LEN];
  uint8_t   wifi_pass[WIFI_PASS_STR_LEN];
  uint32_t  wifi_auth;
  uint8_t   wifi_static_enable;
  uint32_t  wifi_static_ip;
  uint32_t  wifi_static_gateway;
  uint32_t  wifi_static_netmask;
  uint8_t   wifi_mdns_enable;
  uint8_t   wifi_mdns_hostname[WIFI_HOSTNAME_STR_LEN];
  uint8_t   wifi_sntp_enable;
  uint8_t   wifi_sntp_server[WIFI_SERVER_STR_LEN];
  uint32_t  wifi_sntp_resync;

  /* V2 -------------------*/
  /* Camera */
  uint16_t  camera_brightness;
  uint8_t   camera_flip;

  /* Wifi */
  uint8_t   wifi_mode;

  /* V3 — Scopus SoW settings ----------*/
  /* Shell (§4.1) */
  uint8_t   shell_echo_enable;

  /* Object detection (§3.1, §4.2) */
  uint8_t   detect_enable;        /* 0=stopped, 1=running */
  uint8_t   detect_det_mask;      /* bit0=people, bit1=vehicles */
  uint8_t   detect_action_mask;   /* bit0=save SD, bit1=report cellular */

  /* Notifications (§3.1, §4.2, §6) */
  uint32_t  notify_enable_mask;   /* bit mask: 1=NetReg 2=MotStart 4=MotStop 8=Periodic 0x10=People 0x20=Vehicle */
  uint32_t  notify_period_s;
  uint32_t  notify_dest_ip;       /* set on modem via SDVR+NTFHOST */
  uint16_t  notify_dest_port;

  /* Photo / JPEG (§3.4, §4.4) */
  uint16_t  img_width;
  uint16_t  img_height;
  uint8_t   img_quality;          /* 1..100 */
  uint8_t   img_color;            /* 0=YCBCR, 1=RGB, 2=CMYK */
  uint8_t   img_chroma;           /* 0=4:4:4, 1=4:2:2 */

  /* Motion sensor (§3.5, §4.5) */
  uint8_t   motion_sensitivity;   /* 0..100 */
  uint32_t  motion_no_motion_timeout_s;

  /* V4 — safe-boot bootloop counter (flash-backed because TAMP + SRAM
   * are both wiped on this kit's NVIC_SystemReset path). The shell task
   * bumps it on boot, clears it after ~60 s of healthy uptime. If we
   * reach BOOTLOOP_THRESHOLD before that healthy point, the previous
   * boot(s) crashed before becoming healthy — typically nn_task hitting
   * a bad model — and the App switches to safe mode (skip auto-detect)
   * so the user can push a fix over CDC. */
  uint8_t   boot_count;

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
  .dummy                = 0xDEADBEEF,

  /* Camera */
  .camera_flip          = 0U,
  .camera_aec_enable    = 1U,
  .camera_aec_ev        = 0U,
  .camera_awb_enable    = 1U,
  .camera_awb_profile   = 0U,
  .camera_gain          = 0U,
  .camera_exposure      = 0U,
  .camera_brightness    = 55U,

  /* Scopus SoW defaults */
  .shell_echo_enable           = 1U,
  .detect_enable               = 0U,
  .detect_det_mask             = 1U,     /* people only by default */
  .detect_action_mask          = 0U,
  .notify_enable_mask          = 0U,
  .notify_period_s             = 0U,
  .notify_dest_ip              = 0U,
  .notify_dest_port            = 0U,
  .img_width                   = 800U,
  .img_height                  = 600U,
  .img_quality                 = 90U,
  .img_color                   = 0U,     /* YCBCR */
  .img_chroma                  = 0U,     /* 4:4:4 */
  .motion_sensitivity          = 50U,
  .motion_no_motion_timeout_s  = 30U,
  .boot_count                  = 0U,

  /* Wifi */
  .wifi_mode            = 0U,
  .wifi_ssid            = "",
  .wifi_pass            = "",
  .wifi_auth            = 0U,
  .wifi_static_enable   = 0U,
  .wifi_static_ip       = 0U,
  .wifi_static_gateway  = 0U,
  .wifi_static_netmask  = 0U,
  .wifi_mdns_enable     = 1U,
  .wifi_mdns_hostname   = "n6cam",
  .wifi_sntp_enable     = 1U,
  .wifi_sntp_server     = "time.nist.gov",
  .wifi_sntp_resync     = 86400000U
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
