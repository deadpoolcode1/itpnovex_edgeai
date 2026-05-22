/**
 *******************************************************************************
 * @file    slib32_cobs.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements COBS (Consistent Overhead Byte Stuffing) codec
 * @note    Ref: https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing
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
#include "slib32_cobs.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup COBS
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

int32_t cobs_encode(const uint8_t *dec, size_t dec_size, uint8_t *enc, size_t enc_size, size_t *encoded)
{
  int32_t status;
  t_cobs  ctx;

  status = cobs_encode_start(&ctx, enc, enc_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  status = cobs_encode_increment(&ctx, dec, dec_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  return cobs_encode_end(&ctx, encoded);
}

int32_t cobs_encode_start(t_cobs *ctx, uint8_t *enc, size_t enc_size)
{
  /* Validate */
  if (!ctx || !enc || (enc_size < COBS_MAX_ENCODED_SIZE(0)))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Configure */
  ctx->start    = enc;
  ctx->end      = enc + enc_size;
  ctx->out      = enc;
  ctx->code     = 1U;
  ctx->code_ptr = ctx->out++;
  return SLIB32_OK;
}

int32_t cobs_encode_increment(t_cobs *ctx, const uint8_t *dec, size_t dec_size)
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
    if (ctx->out >= ctx->end)
    {
      return SLIB32_ERROR_OVERFLOW;
    }
    /* Continue */
    if (ctx->code != 0xFFU)
    {
      uint8_t byte = *dec++;
      if (byte != 0x00U)
      {
        *ctx->out++ = byte;
        ctx->code++;
        continue;
      }
    }
    *ctx->code_ptr = ctx->code;
    ctx->code_ptr  = ctx->out++;
    ctx->code      = 0x01U;
  }
  return SLIB32_OK;
}

int32_t cobs_encode_end(t_cobs *ctx, size_t *encoded)
{
  /* Validate */
  if (!ctx || !encoded)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Finish */
  *ctx->code_ptr++ = ctx->code;
  *ctx->out++      = 0x00U;
  if (ctx->out > ctx->end)
  {
    return SLIB32_ERROR_OVERFLOW;
  }
  *encoded = (size_t)(ctx->out - ctx->start);
  return SLIB32_OK;
}

int32_t cobs_decode(const uint8_t *enc, size_t enc_size, uint8_t *dec, size_t dec_size, size_t *decoded)
{
  int32_t status;
  t_cobs  ctx;

  status = cobs_decode_start(&ctx, dec, dec_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  status = cobs_decode_increment(&ctx, enc, enc_size);
  if (status != SLIB32_OK)
  {
    return status;
  }
  return cobs_decode_end(&ctx, decoded);
}

int32_t cobs_decode_start(t_cobs *ctx, uint8_t *dec, size_t dec_size)
{
  /* Validate */
  if (!ctx || !dec || (dec_size < COBS_MAX_ENCODED_SIZE(0)))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Configure */
  ctx->start = dec;
  ctx->end   = dec + dec_size;
  ctx->out   = dec;
  ctx->code  = 0xFFU;
  ctx->copy  = 0x00U;
  return SLIB32_OK;
}

int32_t cobs_decode_increment(t_cobs *ctx, const uint8_t *enc, size_t enc_size)
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
    if (ctx->copy > 0U)
    {
      if (*enc == 0x00U)
      {
        return SLIB32_ERROR_DECODE;
      }
      *ctx->out++ = *enc++;
    }
    else
    {
      if ((ctx->code != 0xFFU) || ((end - enc) == 1U))
      {
        *ctx->out++ = 0x00U;
      }
      ctx->copy = ctx->code = *enc++;
      if (ctx->code == 0x00U)
      {
        break;
      }
    }
    ctx->copy--;
  }
  return SLIB32_OK;
}

int32_t cobs_decode_end(const t_cobs *ctx, size_t *decoded)
{
  /* Validate */
  if (!ctx || !decoded)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Finish */
  if (ctx->copy != 0U)
  {
    return SLIB32_ERROR_DECODE;
  }
  *decoded = (ctx->out - ctx->start - 1U);
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
* @} <!-- End: COBS -->
*//*--------------------------------------------------------------------------*/
