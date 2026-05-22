/**
 *******************************************************************************
 * @file    slib32_slip.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for SLIP (Serial Line IP) codec
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
#ifndef _SLIB32_SLIP_H_
#define _SLIB32_SLIP_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "slib32_core.h"
#include "slib32_error.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup SLIP
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* SLIP special characters */
#define SLIP_END      0xC0     /*!< Start/End of packet */
#define SLIP_ESC      0xDB     /*!< Escape character */
#define SLIP_ESC_END  0xDC     /*!< Escaped END */
#define SLIP_ESC_ESC  0xDD     /*!< Escaped ESC */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Maximum size of the encoded buffer */
#define SLIP_MAX_ENCODED_SIZE(size) \
  (2U + (2U * (size)))

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/** SLIP context instance */
typedef struct
{
  const uint8_t *start;
  const uint8_t *end;
  uint8_t       *out;
  bool          is_esc;
} t_slip;

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
 * @brief SLIP encode
 * @param dec Decoded buffer (source)
 * @param dec_size Decoded buffer length
 * @param enc Encoded buffer (destination)
 * @param enc_size Encoded buffer length
 * @param encoded Number of bytes encoded
 * @return Error code
 */
int32_t slip_encode(const uint8_t *dec, size_t dec_size, uint8_t *enc, size_t enc_size, size_t *encoded);

/**
 * @brief SLIP encode start: Prepare context for encoding
 * @param ctx SLIP context
 * @param enc Encoded buffer
 * @param enc_size Encoded buffer length (max)
 * @return Error code
 */
int32_t slip_encode_start(t_slip *ctx, uint8_t *enc, size_t enc_size);

/**
 * @brief SLIP encode increment: Add new decoded data to the encoding process
 * @param ctx SLIP context
 * @param dec Decoded buffer
 * @param dec_size Decoded buffer length
 * @return Error code
 */
int32_t slip_encode_increment(t_slip *ctx, const uint8_t *dec, size_t dec_size);

/**
 * @brief SLIP encode end: Finish the encoding process
 * @param ctx SLIP context
 * @param encoded Number of bytes encoded
 * @return Error code
 */
int32_t slip_encode_end(t_slip *ctx, size_t *encoded);

/**
 * @brief SLIP decode
 * @param enc Encoded buffer (source)
 * @param enc_size Encoded buffer length
 * @param dec Decoded buffer (destination)
 * @param dec_size Decoded buffer length
 * @param decoded Number of bytes decoded
 * @return Error code
 */
int32_t slip_decode(const uint8_t *enc, size_t enc_size, uint8_t *dec, size_t dec_size, size_t *decoded);

/**
 * @brief SLIP decode start: Prepare context for decoding
 * @param ctx SLIP context
 * @param dec Decoded buffer
 * @param dec_size Decoded buffer length (max)
 * @return Error code
 */
int32_t slip_decode_start(t_slip *ctx, uint8_t *dec, size_t dec_size);

/**
 * @brief SLIP decode increment: Add new encoded data to the decoding process
 * @param ctx SLIP context
 * @param enc Encoded buffer
 * @param enc_size Encoded buffer length
 * @return Error code
 */
int32_t slip_decode_increment(t_slip *ctx, const uint8_t *enc, size_t enc_size);

/**
 * @brief SLIP decode end: Finish the decoding process
 * @param ctx SLIP context
 * @param decoded Number of bytes decoded
 * @return Error code
 */
int32_t slip_decode_end(const t_slip *ctx, size_t *decoded);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: SLIP -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_SLIP_H_ */
