/**
 ******************************************************************************
 * @file    nx_app.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   NetX application definition
 ******************************************************************************
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
 ******************************************************************************
 */
#ifndef __NX_APP_H__
#define __NX_APP_H__
#ifdef __cplusplus
extern "C" {
#endif
#if defined(N6CAM_WIFI_MURATA)

#include "cy_wcm.h"
#include "nx_api.h"
#include "tx_app.h"

/*-->> Public Definitions <<--------------------------------------------------*/

#define NX_APP_RTSP_SERVER  1U
#define NX_APP_TCP_STREAM   2U
#define NX_APP_ACTIVE       NX_APP_RTSP_SERVER

/*-->> Public Macros <<-------------------------------------------------------*/

/*-->> Public Types <<--------------------------------------------------------*/

/** Wifi modes */
typedef enum
{
  NX_MODE_OFF = 0x00U,
  NX_MODE_STA,
  NX_MODE_UNKNOWN
} t_nx_mode;

/** Streaming events. */
typedef enum
{
  NX_STREAMING_INACTIVE = 0x00U,
  NX_STREAMING_ACTIVE,
} t_nx_stream;

/*-->> Public Data <<---------------------------------------------------------*/

/*-->> Public API <<----------------------------------------------------------*/

/**
 * @brief NetX application initialization
 */
void nx_app_init(void);

/**
 * @brief Get NetX mode
 * @return Mode
 */
t_nx_mode nx_mode_get(void);

/**
 * @brief Get NetX stored mode
 * @return Mode
 */
t_nx_mode nx_mode_get_stored(void);

/**
 * @brief Set NetX mode
 * @param mode Operation mode
 * @return 0 updated, -1 invalid parameters, -2 update not required
 */
int32_t nx_mode_set(t_nx_mode mode);

/**
 * @brief Get NetX static status
 * @return True if enabled, false otherwise
 */
bool nx_static_is_enabled(void);

/**
 * @brief Get NetX static settings
 * @param settings Static settings
 */
void nx_static_get(cy_wcm_ip_setting_t *settings);

/**
 * @brief Update NetX static settings
 * @param enabled  Static IP status
 * @param settings Static settings
 * @return 0 updated, -1 invalid parameters, -2 update not required
 */
int32_t nx_static_update(uint8_t enabled, cy_wcm_ip_setting_t *settings);

/**
 * @brief Get NetX connection parameters
 * @param credentials AP credentials
 */
void nx_credentials_get(cy_wcm_ap_credentials_t *credentials);

/**
 * @brief Update NetX connection parameters
 * @param credentials AP credentials
 * @return 0 updated, -1 invalid parameters, -2 update not required
 */
int32_t nx_credentials_update(const cy_wcm_ap_credentials_t *credentials);

/**
 * @brief Send frame through NX application
 * @param frame Frame buffer
 * @param size  Frame size
 * @return 0 if successful, 1 otherwise
 */
int32_t nx_stream_send_frame(uint8_t *frame, size_t size);

/**
 * @brief Stream frame sent callback
 *
 * @note  This function is meant to be implemented by user
 *
 * @param frame Frame buffer
 */
void nx_stream_on_frame_sent(uint8_t *frame);

/**
 * @brief Stream state change callback
 *
 * @note  This function is meant to be implemented by user
 *
 * @param state New state
 */
void nx_stream_on_state_change(t_nx_stream state);

/* -------------------------------------------------------------------------- */
#endif /* N6CAM_WIFI_MURATA */
#ifdef __cplusplus
}
#endif
#endif /* __NX_APP_H__ */
