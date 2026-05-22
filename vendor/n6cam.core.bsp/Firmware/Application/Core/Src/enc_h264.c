/**
 ******************************************************************************
 * @file    enc_h264.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   H264 encoding utilities for the N6Cam firmware.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT 2025 SIANA Systems</center></h2>
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
#include <stdlib.h>

#include "common.h"
#include "enc_h264.h"
#include "ewl.h"
#include "main.h"
#include "stm32n6xx_hal.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Utilities
* @{
* @addtogroup Enc
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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define ENC_ALLOC_SIZE      (4U * 1024U * 1024U)

/** Inital QP (Quantization Parameter)
 *  -1: encoder automatically selects the best QP for the first frame.
 *  [0..51]: QP value for the first frame. Lower values produce better quality.
 */
#define ENC_RATE_CTRL_QP    25U
//#define ENC_RATE_CTRL_QP      -1		/* note => fail to encode! */

/** Target bitrate in bits/second */
#define ENC_BITRATE          3000000	/* decent over RTP */

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

enum
{
  ENC_RATE_CTRL_QP_CONSTANT = 0x00U,
  ENC_RATE_CTRL_VBR,
} t_enc_rate_ctrl;

typedef struct
{
  H264EncInst hdl;
  uint64_t    pic_cnt;
  int         is_sps_pps_done;
  int         gop_len;
} t_enc_ctx;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static uint8_t    _enc_alloc_buff[ENC_ALLOC_SIZE] DMA_ALIGN IN_PSRAM;
static t_enc_ctx  _enc_ctx;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _enc_setup_cont_qp(H264EncRateCtrl *rate, int qp);
static void _enc_setup_vbr(H264EncRateCtrl *rate, int bitrate, int gopLen, int qp);
static int  _enc_append_padding(t_enc_ctx *ctx, uint8_t *out, size_t out_size, size_t *p_out_size);

static int  _enc_process(uint8_t *in, uint8_t *out, size_t out_size, size_t *p_out_size, int is_intra_force);
static int  _enc_start(t_enc_ctx *ctx, uint8_t *out, size_t out_size, size_t *p_out_size);
static int  _enc_internal(t_enc_ctx *ctx, uint8_t *in, uint8_t *out, size_t out_size, size_t *p_out_size, int is_intra_force);

extern void *EWLmalloc(u32 n);
extern void *EWLcalloc(u32 n, u32 s);
extern void EWLfree(void *p);

extern void EWLPoolChoiceCb(uint8_t **pool_ptr, size_t *size);
extern void EWLPoolReleaseCb(uint8_t **pool_ptr);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

void enc_h264_init(t_enc_h264 *config)
{
  const int               mode = ENC_RATE_CTRL_VBR;
  int32_t                 ret;
  int32_t                 bitrate;
  t_enc_ctx               *ctx = &_enc_ctx;
  H264EncConfig           h264_config = { 0 };
  H264EncPreProcessingCfg h264_pp;
  H264EncCodingCtrl       h264_ctrl;
  H264EncRateCtrl         h264_rate;

  ctx->gop_len = config->fps - 1;
  /* init encoder */
  h264_config.streamType     = H264ENC_BYTE_STREAM;
  h264_config.viewMode       = H264ENC_BASE_VIEW_SINGLE_BUFFER;
///TODO: sylvain -> experiment with profile level to reduce bitrate
  h264_config.level          = H264ENC_LEVEL_3_2;
//  h264_config.level          = H264ENC_LEVEL_5_1;
  h264_config.width          = config->width;
  h264_config.height         = config->height;
  h264_config.frameRateNum   = config->fps;
  h264_config.frameRateDenom = 1;
  h264_config.refFrameAmount = 1;
  ret = H264EncInit(&h264_config, &ctx->hdl);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }

  /* setup source format */
  ret = H264EncGetPreProcessing(ctx->hdl, &h264_pp);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }

  h264_pp.inputType = config->encoding;
  ret = H264EncSetPreProcessing(ctx->hdl, &h264_pp);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }

  /* setup coding ctrl */
  ret = H264EncGetCodingCtrl(ctx->hdl, &h264_ctrl);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }

  h264_ctrl.idrHeader = 1;
  ret = H264EncSetCodingCtrl(ctx->hdl, &h264_ctrl);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }

  /* setup rate ctrl */
  ret = H264EncGetRateCtrl(ctx->hdl, &h264_rate);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }

  // generate super safe bitrate but too much for RTP!
  //bitrate = ((config->width * config->height * 12) * config->fps) / 30;

  bitrate = ENC_BITRATE;

  if (mode == ENC_RATE_CTRL_QP_CONSTANT)
  {
    _enc_setup_cont_qp(&h264_rate, ENC_RATE_CTRL_QP);
  }
  else if (mode == ENC_RATE_CTRL_VBR)
  {
    _enc_setup_vbr(&h264_rate, bitrate, config->fps, ENC_RATE_CTRL_QP);
  }
  else
  {
    Error_Handler();
  }
  ret = H264EncSetRateCtrl(ctx->hdl, &h264_rate);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }
}

void enc_h264_deinit()
{
  t_enc_ctx *ctx = &_enc_ctx;
  int32_t   ret;
  ret = H264EncRelease(ctx->hdl);
  if (ret != H264ENC_OK)
  {
    Error_Handler();
  }
}

int32_t enc_h264_process(uint8_t *in, uint8_t *out, size_t out_size, int is_intra_force)
{
  size_t  out_compressed_frame_len;
  int32_t ret;
  ret = _enc_process(in, out, out_size, &out_compressed_frame_len, is_intra_force);
  return ret ? -1 : out_compressed_frame_len;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _enc_setup_cont_qp(H264EncRateCtrl *rate, int qp)
{
  rate->pictureRc   = 0;
  rate->mbRc        = 0;
  rate->pictureSkip = 0;
  rate->hrd         = 0;
  rate->qpHdr       = qp;
  rate->qpMin       = qp;
  rate->qpMax       = qp;
}

static void _enc_setup_vbr(H264EncRateCtrl *rate, int bitrate, int gopLen, int qp)
{
  rate->pictureRc   = 1;
  rate->mbRc        = 1;
  rate->pictureSkip = 0;
  rate->hrd         = 0;
  rate->qpHdr       = qp;
  rate->qpMin       = 10;
  rate->qpMax       = 51;
  rate->gopLen      = gopLen;
  rate->bitPerSecond= bitrate;
  rate->intraQpDelta= 0;
}

static int _enc_append_padding(t_enc_ctx *ctx, uint8_t *out, size_t out_size, size_t *p_out_size)
{
  uint32_t out_addr = (uint32_t) out;
  int      pad_size = 8 - (out_addr % 8);
  int      pad_len  = 0;

  *p_out_size = 0;

  /* No need of padding */
  if (out_addr % 8 == 0)
  {
    return 0;
  }

  /* adjust pad size */
  if (pad_size < 6)
  {
    pad_size += 8;
  }

  /* Do we add enough space for padding ? */
  if (pad_size > out_size)
  {
    return -1;
  }

  /* Ok now we add nal pad */
  out[pad_len++] = 0x00;
  out[pad_len++] = 0x00;
  out[pad_len++] = 0x00;
  out[pad_len++] = 0x01;
  out[pad_len++] = 0x2c; /* FIXME : adapt for nal_ref_idc ? */
  pad_size -= 5;
  while (pad_size--)
  {
    out[pad_len++] = 0xff;
  }
  *p_out_size = pad_len;
  return 0;
}

static int _enc_process(uint8_t *in, uint8_t *out, size_t out_size, size_t *p_out_size, int is_intra_force)
{
  int       ret;
  t_enc_ctx *p_ctx   = &_enc_ctx;
  size_t    start_len= 0;
  size_t    frame_len;

  if (!p_ctx->is_sps_pps_done)
  {
    ret = _enc_start(p_ctx, out, out_size, &start_len);
    if (ret)
    {
      return ret;
    }
    p_ctx->is_sps_pps_done = 1;
  }

  ret = _enc_internal(p_ctx, in, &out[start_len], out_size - start_len, &frame_len, is_intra_force);
  if (ret)
  {
    return ret;
  }
  *p_out_size = start_len + frame_len;
  return 0;
}

static int _enc_start(t_enc_ctx *ctx, uint8_t *out, size_t out_size, size_t *p_out_size)
{
  int        ret;
  H264EncOut enc_out;
  H264EncIn  enc_in;
  size_t     start_len;
  size_t     pad_len;

  enc_in.pOutBuf = (u32 *) out;
  enc_in.busOutBuf = (ptr_t) out;
  enc_in.outBufSize = out_size;
  ret = H264EncStrmStart(ctx->hdl, &enc_in, &enc_out);
  if (ret)
  {
    return ret;
  }
  start_len = enc_out.streamSize;

  ret = _enc_append_padding(ctx, &out[start_len], out_size - start_len, &pad_len);
  if (ret)
  {
    return ret;
  }
  *p_out_size = start_len + pad_len;
  return 0;
}

static int _enc_internal(t_enc_ctx *ctx, uint8_t *in, uint8_t *out, size_t out_size, size_t *p_out_size, int is_intra_force)
{
  int        ret;
  H264EncOut enc_out;
  H264EncIn  enc_in;

  /* In both N6_VENC_INPUT_YUV2 and N6_VENC_INPUT_RGB565 only busLuma is used */
  enc_in.busLuma      = (ptr_t) in;
  enc_in.busChromaU   = 0;
  enc_in.busChromaV   = 0;
  enc_in.pOutBuf      = (u32 *)out;
  enc_in.busOutBuf    = (ptr_t)out;
  enc_in.outBufSize   = out_size;
  enc_in.codingType   = ((ctx->pic_cnt % (ctx->gop_len + 1)) == 0) ? H264ENC_INTRA_FRAME : H264ENC_PREDICTED_FRAME;
  enc_in.codingType   = is_intra_force ? H264ENC_INTRA_FRAME : enc_in.codingType;
  enc_in.timeIncrement= 1;
  enc_in.ipf          = H264ENC_REFERENCE_AND_REFRESH; /* FIXME : can be H264ENC_NO_REFERENCE_NO_REFRESH in I only mode */
  enc_in.ltrf         = H264ENC_NO_REFERENCE_NO_REFRESH;
  enc_in.lineBufWrCnt = 0;
  enc_in.sendAUD      = 0;

  ret = H264EncStrmEncode(ctx->hdl, &enc_in, &enc_out, NULL, NULL, NULL);
  if (ret != H264ENC_FRAME_READY)
  {
    return -1;
  }
  ctx->pic_cnt++;
  *p_out_size = enc_out.streamSize;
  return 0;
}

void *EWLmalloc(u32 n)
{
  void *res = malloc(n);
  if (!res)
  {
    Error_Handler();
  }
  return res;
}

void *EWLcalloc(u32 n, u32 s)
{
  void *res = calloc(n, s);
  if (!res)
  {
    Error_Handler();
  }
  return res;
}

void EWLfree(void *p)
{
  free(p);
}

void EWLPoolChoiceCb(uint8_t **pool_ptr, size_t *size)
{
  *pool_ptr = _enc_alloc_buff;
  *size     = ENC_ALLOC_SIZE;
}

void EWLPoolReleaseCb(uint8_t **pool_ptr)
{
  UNUSED(pool_ptr);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Utilities -->
* @} <!-- End: Enc -->
*//*--------------------------------------------------------------------------*/
