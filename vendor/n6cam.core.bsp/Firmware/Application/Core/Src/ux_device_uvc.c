/**
 ******************************************************************************
 * @file    ux_device_uvc.c
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
#include <stdbool.h>

#include "camera_task.h"

#include "main.h"
#include "ux_device_uvc.h"
#include "ux_stm32_config.h"

#if USBD_UVC_CLASS_ACTIVATED == 1U
/*-->> Private Tunables <<----------------------------------------------------*/

/* H264 encoder buffer size */
#define H264_FRAME_MAX_SIZE  (192U * 1024U)

/*-->> Private Definitions <<-------------------------------------------------*/

#define UX_UVC_HEADER_SIZE    2U        /*!< UVC-H264 header size */
#define UX_UVC_BUFFER_SIZE    1024U     /*!< UVC TX buffer size */

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/** @brief UVC on-fly frame state */
typedef struct
{
  uint8_t     *cursor;
  uint8_t     *frame;
  int32_t     packet_num;
  int32_t     packet_idx;
  int32_t     last_packet_size;
} t_uvc_onfly;

/** @brief UVC context */
typedef struct
{
  t_ux_uvc_state state;
  uint8_t     starting;
  uint8_t     *frame;
  size_t      size;
  uint32_t    frame_start;
  uint32_t    frame_period;
  uint8_t     packet[UX_UVC_BUFFER_SIZE];
  t_uvc_onfly storage;
  t_uvc_onfly *current;
} t_uvc_ctx;

/*-->> Private Data <<--------------------------------------------------------*/

static t_uvc_ctx _uvc_ctx = { 0 };          /*!< UVC context */

/** @brief UVC video commit */
static t_ux_uvc_ctrl _uvc_ctrl_commit = {
  .bmHint                   = 0x00U,
  .bFormatIndex             = 0x01U,
  .bFrameIndex              = 0x01U,
  .dwFrameInterval          = UX_UVC_INTERVAL(30),
  .wKeyFrameRate            = 0x00U,
  .wPFrameRate              = 0x00U,
  .wCompQuality             = 0x00U,
  .wCompWindowSize          = 0x00U,
  .wDelay                   = 0x00U,
  .dwMaxVideoFrameSize      = 0x00U,
  .dwMaxPayloadTransferSize = 0x00U,
  .dwClockFrequency         = 0x00U,
  .bmFramingInfo            = 0x00U,
  .bPreferedVersion         = 0x00U,
  .bMinVersion              = 0x00U,
  .bMaxVersion              = 0x00U,
};

/** @brief UVC video probe */
static t_ux_uvc_ctrl _uvc_ctrl_probe = {
  .bmHint                   = 0x00U,
  .bFormatIndex             = 0x01U,
  .bFrameIndex              = 0x01U,
  .dwFrameInterval          = UX_UVC_INTERVAL(30),
  .wKeyFrameRate            = 0x00U,
  .wPFrameRate              = 0x00U,
  .wCompQuality             = 0x00U,
  .wCompWindowSize          = 0x00U,
  .wDelay                   = 0x00U,
  .dwMaxVideoFrameSize      = 0x00U,
  .dwMaxPayloadTransferSize = 0x00U,
  .dwClockFrequency         = 0x00U,
  .bmFramingInfo            = 0x00U,
  .bPreferedVersion         = 0x00U,
  .bMinVersion              = 0x00U,
  .bMaxVersion              = 0x00U,
};

static uint8_t              _h264_enc_buff[H264_FRAME_MAX_SIZE] DMA_ALIGN IN_PSRAM;
static size_t               _h264_enc_size = 0;

/*-->> Private Functions <<---------------------------------------------------*/

static UINT _uvc_is_hs(VOID);
static UINT _uvc_check_fps();

static VOID _uvc_stream(UX_DEVICE_CLASS_VIDEO_STREAM* stream);
static VOID _uvc_send_packet(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UINT size);
static VOID _uvc_update_onfly(UINT size);
static t_uvc_onfly *_uvc_start_new_transmission(UINT size);

static VOID _uvc_stream_start(UX_DEVICE_CLASS_VIDEO_STREAM* stream);
static VOID _uvc_stream_stop(UX_DEVICE_CLASS_VIDEO_STREAM* stream);

static UINT _uvc_handle_itf_setup(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);
static UINT _uvc_handle_itf_class_setup(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);

static UINT _uvc_handle_probe_commit(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);
static UINT _uvc_handle_probe_commit_get(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);
static UINT _uvc_handle_probe_commit_set(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);

static UINT _uvc_handle_probe_control(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);
static UINT _uvc_handle_probe_control_get(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);
static UINT _uvc_handle_probe_control_set(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer);

/*-->> Public API <<----------------------------------------------------------*/

int32_t ux_uvc_send_frame(uint8_t *frame, size_t size)
{
  /* sanity check... */
  if (size > H264_FRAME_MAX_SIZE)
  {
	return 1;
 }

  /* Validate: Parameters invalid */
  if ((_uvc_ctx.state != UVC_STREAMING_ACTIVE) || !frame || (size == 0))
  {
    return 2;
  }
  /* Validate: Already sending frame */
  if (_uvc_ctx.frame)
  {
    return 3;
  }

  /* cache frame locally */
  memcpy(_h264_enc_buff, frame, size);

  /* Prepare */
  _uvc_ctx.size = _h264_enc_size = size;
  __DMB();
  _uvc_ctx.frame = _h264_enc_buff;

  /* all good! */
  return 0;
}

WEAK void ux_uvc_on_frame_sent(uint8_t *buff)
{
  /* Meant to be implemented by user */
  UNUSED(buff);
}

WEAK void ux_uvc_on_state_change(t_ux_uvc_state state)
{
  /* Meant to be implemented by user */
  UNUSED(state);
}

VOID ux_uvc_activate(VOID* instance)
{
  UX_PARAMETER_NOT_USED(instance);
  _uvc_ctx.state = UVC_STREAMING_INACTIVE;
}

VOID ux_uvc_deactivate(VOID* instance)
{
  UX_PARAMETER_NOT_USED(instance);
}

VOID ux_uvc_change(UX_DEVICE_CLASS_VIDEO_STREAM* stream, ULONG setting)
{
  ULONG status;
  /* Stop if stream is closed */
  if (setting == 0)
  {
    _uvc_stream_stop(stream);
    return;
  }
  /* Start */
  _uvc_stream_start(stream);
  status = ux_device_class_video_transmission_start(stream);
  if (status != UX_SUCCESS)
  {
    Error_Handler();
  }
}

VOID ux_uvc_stream_done(UX_DEVICE_CLASS_VIDEO_STREAM* stream, ULONG length)
{
  _uvc_stream(stream);
}

UINT ux_uvc_stream_request(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  UINT  status  = UX_SUCCESS;
  UCHAR request = transfer->ux_slave_transfer_request_setup[UX_SETUP_REQUEST_TYPE];

  /* Handle by target */
  switch (request & UX_REQUEST_TARGET)
  {
    case UX_REQUEST_TARGET_INTERFACE:
      status = _uvc_handle_itf_setup(stream, transfer);
      break;

    default:
      status = UX_ERROR;
      break;
  }
  return status;
}

/*-->> Private Functions <<---------------------------------------------------*/

static UINT _uvc_is_hs(VOID)
{
  return _ux_system_slave->ux_system_slave_speed == UX_HIGH_SPEED_DEVICE;
}

static UINT _uvc_check_fps()
{
  if ((HAL_GetTick() - _uvc_ctx.frame_start) >= _uvc_ctx.frame_period)
  {
    return 1U;
  }
  return 0U;
}

static VOID _uvc_stream(UX_DEVICE_CLASS_VIDEO_STREAM* stream)
{
  t_uvc_onfly  *ctx;
  uint32_t size;
  uint32_t packet_size = _uvc_is_hs()? USBD_UVC_EPIN_HS_MPS : USBD_UVC_EPIN_FS_MPS;
  /* Validate */
  if (_uvc_ctx.state != UVC_STREAMING_ACTIVE)
  {
    return;
  }
  /* Check if we need a new frame */
  if (!_uvc_ctx.current)
  {
    _uvc_ctx.current = _uvc_start_new_transmission(packet_size);
  }
  if (!_uvc_ctx.current)
  {
    _uvc_send_packet(stream, UX_UVC_HEADER_SIZE);
    return;
  }
  /* Send packet */
  ctx  = _uvc_ctx.current;
  size = (ctx->packet_idx == (ctx->packet_num - 1U)) ? (ctx->last_packet_size + UX_UVC_HEADER_SIZE) : packet_size;
  memcpy(&_uvc_ctx.packet[2], ctx->cursor, (size - UX_UVC_HEADER_SIZE));
  _uvc_send_packet(stream, size);
  _uvc_update_onfly(size);
}

static VOID _uvc_send_packet(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UINT size)
{
  UCHAR *buff;
  ULONG bsize;
  UINT  status;
  /* Get transmission buffer */
  status = ux_device_class_video_write_payload_get(stream, &buff, &bsize);
  if (status != UX_SUCCESS)
  {
    Error_Handler();
  }
  /* Prepare and send */
  memcpy(buff, _uvc_ctx.packet, size);
  ux_device_class_video_write_payload_commit(stream, size);
}

static VOID _uvc_update_onfly(UINT size)
{
  /* Validate we have a context */
  t_uvc_onfly *ctx = _uvc_ctx.current;
  if (!ctx)
  {
    Error_Handler();
  }
  /* Update */
  ctx->packet_idx = (ctx->packet_idx + 1U) % ctx->packet_num;
  ctx->cursor    += (size - UX_UVC_HEADER_SIZE);
  /* Check if ready */
  if (ctx->packet_idx == 0U)
  {
    /* Once displayed, free frame */
    ux_uvc_on_frame_sent(ctx->frame);
    _uvc_ctx.current = NULL;
  }
}

static t_uvc_onfly *_uvc_start_new_transmission(UINT size)
{
  t_uvc_onfly *ctx;
  /* Validate */
  if ((_uvc_ctx.starting == 0x00U) && !_uvc_check_fps())
  {
    return NULL;
  }
  if (!_uvc_ctx.frame)
  {
    return NULL;
  }
  /* Prepare new frame */
  ctx = &_uvc_ctx.storage;
  ctx->cursor          = _uvc_ctx.frame;
  ctx->packet_idx      = 0x00U;
  ctx->packet_num      = (_uvc_ctx.size + size - 1U) / (size - UX_UVC_HEADER_SIZE);
  ctx->last_packet_size= _uvc_ctx.size % (size - UX_UVC_HEADER_SIZE);
  if (!ctx->last_packet_size)
  {
    ctx->packet_num--;
    ctx->last_packet_size = size - UX_UVC_HEADER_SIZE;
  }
  /* Reset context */
  _uvc_ctx.frame_start  = HAL_GetTick();
  _uvc_ctx.starting     = 0x00U;
  _uvc_ctx.packet[1]   ^= 0x01U;
  __DMB();
  _uvc_ctx.frame        = NULL;
  return ctx;
}

static VOID _uvc_stream_start(UX_DEVICE_CLASS_VIDEO_STREAM* stream)
{
  /* Notify */
  ux_uvc_on_state_change(UVC_STREAMING_ACTIVE);
  /* Start stream */
  _uvc_ctx.starting    = 0x01U;
  _uvc_ctx.packet[0]   = 0x02U;
  _uvc_ctx.packet[1]   = 0x00U;
  _uvc_ctx.frame_start = HAL_GetTick() - _uvc_ctx.frame_period;
  _uvc_ctx.state       = UVC_STREAMING_ACTIVE;
  for (uint8_t idx = 0; idx < UX_UVC_BUFFER_NUM; idx++)
  {
    _uvc_stream(stream);
  }
}

static VOID _uvc_stream_stop(UX_DEVICE_CLASS_VIDEO_STREAM* stream)
{
  /* Notify */
  ux_uvc_on_state_change(UVC_STREAMING_INACTIVE);
  _uvc_ctx.state = UVC_STREAMING_INACTIVE;
}

static UINT _uvc_handle_itf_setup(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  UINT  status  = UX_SUCCESS;
  UCHAR request = transfer->ux_slave_transfer_request_setup[UX_SETUP_REQUEST_TYPE];

  /* Handle by type */
  switch (request & UX_REQUEST_TYPE)
  {
    case UX_REQUEST_TYPE_CLASS:
      status = _uvc_handle_itf_class_setup(stream, transfer);
      break;

    default:
      status = UX_ERROR;
      break;
  }
  return status;
}

static UINT _uvc_handle_itf_class_setup(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  UINT  status  = UX_SUCCESS;
  UCHAR class   = transfer->ux_slave_transfer_request_setup[UX_SETUP_VALUE + 1];
  UCHAR itf_idx = transfer->ux_slave_transfer_request_setup[UX_SETUP_INDEX];
  /* Validate: No control */
  if (!itf_idx)
  {
    status = UX_ERROR;
    return status;
  }
  /* Handle by class */
  switch (class)
  {
    case UX_DEVICE_CLASS_VIDEO_VS_COMMIT_CONTROL:
      status = _uvc_handle_probe_commit(stream, transfer);
      break;

    case UX_DEVICE_CLASS_VIDEO_VS_PROBE_CONTROL:
      status = _uvc_handle_probe_control(stream, transfer);
      break;

    default:
      status = UX_ERROR;
      break;
  }
  return status;
}

static UINT _uvc_handle_probe_commit(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  UINT  status  = UX_SUCCESS;
  UCHAR request = transfer->ux_slave_transfer_request_setup[UX_SETUP_REQUEST];
  switch (request)
  {
    case UX_DEVICE_CLASS_VIDEO_GET_CUR:
      status = _uvc_handle_probe_commit_get(stream, transfer);
      break;

    case UX_DEVICE_CLASS_VIDEO_SET_CUR:
      status = _uvc_handle_probe_commit_set(stream, transfer);
      break;

    default:
      status = UX_ERROR;
      break;
  }
  return status;
}

static UINT _uvc_handle_probe_commit_get(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  UINT    status;
  UINT    size    = ux_utility_short_get(transfer->ux_slave_transfer_request_setup + UX_SETUP_LENGTH);
  uint8_t *data   = transfer->ux_slave_transfer_request_data_pointer;
  /* Update probe */
  memcpy(data, &_uvc_ctrl_commit, sizeof(t_ux_uvc_ctrl));
  status = ux_device_stack_transfer_request(
    transfer,
    UX_MIN(size, sizeof(t_ux_uvc_ctrl)),
    UX_MIN(size, sizeof(t_ux_uvc_ctrl))
  );
  return status;
}

static UINT _uvc_handle_probe_commit_set(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  /* Validate transfer size */
  if (transfer->ux_slave_transfer_request_actual_length != sizeof(t_ux_uvc_ctrl))
  {
    return UX_ERROR;
  }
  /* Update */
  memcpy(&_uvc_ctrl_commit, transfer->ux_slave_transfer_request_data_pointer, sizeof(t_ux_uvc_ctrl));
  if (_uvc_ctrl_commit.bmHint != 0)
  {
    Error_Handler();
  }
  return UX_SUCCESS;
}

static UINT _uvc_handle_probe_control(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  UINT  status  = UX_SUCCESS;
  UCHAR request = transfer->ux_slave_transfer_request_setup[UX_SETUP_REQUEST];
  switch (request)
  {
    case UX_DEVICE_CLASS_VIDEO_GET_DEF:
    case UX_DEVICE_CLASS_VIDEO_GET_CUR:
    case UX_DEVICE_CLASS_VIDEO_GET_MIN:
    case UX_DEVICE_CLASS_VIDEO_GET_MAX:
      status = _uvc_handle_probe_control_get(stream, transfer);
      break;

    case UX_DEVICE_CLASS_VIDEO_SET_CUR:
      status = _uvc_handle_probe_control_set(stream, transfer);
      break;

    default:
      status = UX_ERROR;
      break;
  }
  return status;
}

static UINT _uvc_handle_probe_control_get(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  UINT    status;
  UINT    size     = ux_utility_short_get(transfer->ux_slave_transfer_request_setup + UX_SETUP_LENGTH);
  UINT    size_max = _uvc_is_hs()? USBD_UVC_EPIN_HS_MPS : USBD_UVC_EPIN_FS_MPS;
  uint8_t *data    = transfer->ux_slave_transfer_request_data_pointer;
  /* Update probe */
  _uvc_ctrl_probe.dwFrameInterval          = UX_UVC_INTERVAL(camera.sensor.fps);
  _uvc_ctrl_probe.dwMaxVideoFrameSize      = camera.main.width * camera.main.height * 2U;
  _uvc_ctrl_probe.dwMaxPayloadTransferSize = size_max;
  _uvc_ctrl_probe.dwClockFrequency         = HSE_VALUE;
  /* Should be not zero, but not clear for uncompressed */
  _uvc_ctrl_probe.bPreferedVersion         = 0x00U;
  _uvc_ctrl_probe.bMinVersion              = 0x00U;
  _uvc_ctrl_probe.bMaxVersion              = 0x00U;
  /* Send */
  memcpy(data, &_uvc_ctrl_probe, sizeof(t_ux_uvc_ctrl));
  status = ux_device_stack_transfer_request(
    transfer,
    UX_MIN(size, sizeof(t_ux_uvc_ctrl)),
    UX_MIN(size, sizeof(t_ux_uvc_ctrl))
  );
  return status;
}

static UINT _uvc_handle_probe_control_set(UX_DEVICE_CLASS_VIDEO_STREAM* stream, UX_SLAVE_TRANSFER* transfer)
{
  /* Validate transfer size */
  if (transfer->ux_slave_transfer_request_actual_length != sizeof(t_ux_uvc_ctrl))
  {
    return UX_ERROR;
  }
  /* Update */
  memcpy(&_uvc_ctrl_probe, transfer->ux_slave_transfer_request_data_pointer, sizeof(t_ux_uvc_ctrl));
  _uvc_ctrl_probe.bmHint = 0;
  return UX_SUCCESS;
}

/* -------------------------------------------------------------------------- */
#endif /* USBD_UVC_CLASS_ACTIVATED */
