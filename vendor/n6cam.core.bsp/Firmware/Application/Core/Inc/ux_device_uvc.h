/**
 ******************************************************************************
 * @file    ux_device_uvc.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   USBX UVC device application definition
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
#ifndef _UX_DEVICE_UVC_H_
#define _UX_DEVICE_UVC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ux_api.h"
#include "ux_device_class_video.h"
#include "ux_device_descriptors.h"

/*-->> Public Definitions <<--------------------------------------------------*/

#define UX_UVC_BUFFER_NUM    4U       /*!< Number of UVC TX buffers */

/*-->> Public Macros <<-------------------------------------------------------*/

/*-->> Public Types <<--------------------------------------------------------*/

/** UVC streaming events */
typedef enum
{
  UVC_STREAMING_INACTIVE = 0x00U,
  UVC_STREAMING_ACTIVE,
} t_ux_uvc_state;

/**
 * @brief UVC control structure
 * @note  UVC uses only 26 first bytes
 */
typedef struct
{
  uint16_t bmHint;
  uint8_t  bFormatIndex;
  uint8_t  bFrameIndex;
  uint32_t dwFrameInterval;
  uint16_t wKeyFrameRate;
  uint16_t wPFrameRate;
  uint16_t wCompQuality;
  uint16_t wCompWindowSize;
  uint16_t wDelay;
  uint32_t dwMaxVideoFrameSize;
  uint32_t dwMaxPayloadTransferSize;
  uint32_t dwClockFrequency;
  uint8_t  bmFramingInfo;
  uint8_t  bPreferedVersion;
  uint8_t  bMinVersion;
  uint8_t  bMaxVersion;
  uint8_t  bUsage;
  uint8_t  bBitDepthLuma;
  uint8_t  bmSettings;
  uint8_t  bMaxNumberOfRefFramesPlus1;
  uint16_t bmRateControlModes;
  uint64_t bmLayoutPerStream;
} __PACKED t_ux_uvc_ctrl;

/*-->> Public Data <<---------------------------------------------------------*/

/*-->> Public API <<----------------------------------------------------------*/

/* Streaming control */
int32_t ux_uvc_send_frame(uint8_t *frame, size_t size);
void    ux_uvc_on_frame_sent(uint8_t *buff);
void    ux_uvc_on_state_change(t_ux_uvc_state state);

/* Device class */
VOID ux_uvc_activate(VOID* instance);
VOID ux_uvc_deactivate(VOID* instance);
VOID ux_uvc_change(UX_DEVICE_CLASS_VIDEO_STREAM* stream, ULONG setting);
VOID ux_uvc_stream_done(UX_DEVICE_CLASS_VIDEO_STREAM* stream, ULONG length);
UINT ux_uvc_stream_request(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);

/* -------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif
#endif  /* _UX_DEVICE_UVC_H_ */
