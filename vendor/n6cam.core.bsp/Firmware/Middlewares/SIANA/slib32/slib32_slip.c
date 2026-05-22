/**
 *******************************************************************************
 * @file    slib32_slip.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements SLIP (Serial Line IP) codec
 * @note    Ref: https://datatracker.ietf.org/doc/html/rfc1055.html
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
#include "slib32_slip.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup SLIP
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
*/ /*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*/ /*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*/ /*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*/ /*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*/ /*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*/ /*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*/ /*--------------------------------------------------------------------------*/

int32_t slip_encode(const uint8_t *dec, size_t dec_size, uint8_t *enc, size_t enc_size, size_t *encoded)
{
  int32_t status;
  t_slip  ctx;

  status = slip_encode_start(&ctx, enc, enc_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  status = slip_encode_increment(&ctx, dec, dec_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  return slip_encode_end(&ctx, encoded);
}

int32_t slip_encode_start(t_slip *ctx, uint8_t *enc, size_t enc_size)
{
  /* Validate */
  if (!ctx || !enc || (enc_size < SLIP_MAX_ENCODED_SIZE(0)))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Configure */
  ctx->start  = enc;
  ctx->end    = enc + enc_size;
  ctx->out    = enc;
  ctx->is_esc = false;

  /* Set first byte */
  *ctx->out++ = SLIP_END;

  return SLIB32_OK;
}

int32_t slip_encode_increment(t_slip *ctx, const uint8_t *dec, size_t dec_size)
{
  /* Validate */
  if (!ctx || !dec)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (dec_size == 0U)
  {
    return SLIB32_OK;
  }

  /* Encode */
  const uint8_t *end = dec + dec_size;
  while (dec < end)
  {
    /* Validate output */
    if (ctx->out >= (ctx->end - 1U))
    {
      return SLIB32_ERROR_OVERFLOW;
    }
    /* Continue */
    uint8_t byte = *dec++;
    switch (byte)
    {
      case SLIP_ESC:
        *ctx->out++ = SLIP_ESC;
        *ctx->out++ = SLIP_ESC_ESC;
        break;

      case SLIP_END:
        *ctx->out++ = SLIP_ESC;
        *ctx->out++ = SLIP_ESC_END;
        break;

      default:
        *ctx->out++ = byte;
        break;
    }
  }
  return SLIB32_OK;
}

int32_t slip_encode_end(t_slip *ctx, size_t *encoded)
{
  /* Validate */
  if (!ctx || !encoded)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (ctx->out >= ctx->end)
  {
    return SLIB32_ERROR_OVERFLOW;
  }

  /* Finish */
  *ctx->out++ = SLIP_END;
  *encoded = (size_t)(ctx->out - ctx->start);
  return SLIB32_OK;
}

int32_t slip_decode(const uint8_t *enc, size_t enc_size, uint8_t *dec, size_t dec_size, size_t *decoded)
{
  int32_t status;
  t_slip  ctx;

  status = slip_decode_start(&ctx, dec, dec_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  status = slip_decode_increment(&ctx, enc, enc_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  return slip_decode_end(&ctx, decoded);
}

int32_t slip_decode_start(t_slip *ctx, uint8_t *dec, size_t dec_size)
{
  /* Validate */
  if (!ctx || !dec || (dec_size < SLIP_MAX_ENCODED_SIZE(0)))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Configure */
  ctx->start  = dec;
  ctx->end    = dec + dec_size;
  ctx->out    = dec;
  ctx->is_esc = false;
  return SLIB32_OK;
}

int32_t slip_decode_increment(t_slip *ctx, const uint8_t *enc, size_t enc_size)
{
  /* Validate */
  if (!ctx || !enc || (enc_size == 0U))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Decode */
  const uint8_t *end = enc + enc_size;
  while (enc < end)
  {
    /* Validate output */
    if (ctx->out >= ctx->end)
    {
      return SLIB32_ERROR_OVERFLOW;
    }
    /* Continue */
    uint8_t byte = *enc++;
    if (ctx->is_esc)
    {
      ctx->is_esc = false;
      switch (byte)
      {
        case SLIP_ESC_ESC:
          *ctx->out++ = SLIP_ESC;
          break;

        case SLIP_ESC_END:
          *ctx->out++ = SLIP_END;
          break;

        default:
          return SLIB32_ERROR_DECODE;
      }
    }
    else if (byte == SLIP_END)
    {
      if (ctx->out > ctx->start)
      {
        break;
      }
    }
    else if (byte == SLIP_ESC)
    {
      ctx->is_esc = true;
    }
    else
    {
      *ctx->out++ = byte;
    }
  }
  return SLIB32_OK;
}

int32_t slip_decode_end(const t_slip *ctx, size_t *decoded)
{
  /* Validate */
  if (!ctx || !decoded)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Finish */
  *decoded = (size_t)(ctx->out - ctx->start);
  return SLIB32_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*/ /*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: SLIP -->
*//*--------------------------------------------------------------------------*/
