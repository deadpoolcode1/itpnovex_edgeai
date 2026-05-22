/**
 *******************************************************************************
 * @file    slib32_cobs.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for COBS (Consistent Overhead Byte Stuffing) codec
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
#ifndef _SLIB32_COBS_H_
#define _SLIB32_COBS_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "slib32_core.h"
#include "slib32_error.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup COBS
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Maximum block size */
#define COBS_MAX_BLOCK_SIZE   254U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Maximum size of the encoded buffer */
#define COBS_MAX_ENCODED_SIZE(size) \
  (((size) == 0U ? 0U : (size) + ((size) / COBS_MAX_BLOCK_SIZE)) + 2U)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/** COBS context instance */
typedef struct
{
  const uint8_t *start;
  const uint8_t *end;
  uint8_t       *out;
  uint8_t       *code_ptr;
  uint8_t       code;
  uint8_t       copy;
} t_cobs;

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
 * @brief COBS encode
 * @param dec Decoded buffer (source)
 * @param dec_size Decoded buffer length
 * @param enc Encoded buffer (destination)
 * @param enc_size Encoded buffer length
 * @param encoded Number of bytes encoded
 * @return Error code
 */
int32_t cobs_encode(const uint8_t *dec, size_t dec_size, uint8_t *enc, size_t enc_size, size_t *encoded);

/**
 * @brief COBS encode start: Prepare context for encoding
 * @param ctx COBS context
 * @param enc Encoded buffer
 * @param enc_size Encoded buffer length (max)
 * @return Error code
 */
int32_t cobs_encode_start(t_cobs *ctx, uint8_t *enc, size_t enc_size);

/**
 * @brief COBS encode increment: Add new decoded data to the encoding process
 * @param ctx COBS context
 * @param dec Decoded buffer
 * @param dec_size Decoded buffer length
 * @return Error code
 */
int32_t cobs_encode_increment(t_cobs *ctx, const uint8_t *dec, size_t dec_size);

/**
 * @brief COBS encode end: Finish the encoding process
 * @param ctx COBS context
 * @param encoded Number of bytes encoded
 * @return Error code
 */
int32_t cobs_encode_end(t_cobs *ctx, size_t *encoded);

/**
 * @brief COBS decode
 * @param enc Encoded buffer (source)
 * @param enc_size Encoded buffer length
 * @param dec Decoded buffer (destination)
 * @param dec_size Decoded buffer length
 * @param decoded Number of bytes decoded
 * @return Error code
 */
int32_t cobs_decode(const uint8_t *enc, size_t enc_size, uint8_t *dec, size_t dec_size, size_t *decoded);

/**
 * @brief COBS decode start: Prepare context for decoding
 * @param ctx COBS context
 * @param dec Decoded buffer
 * @param dec_size Decoded buffer length (max)
 * @return Error code
 */
int32_t cobs_decode_start(t_cobs *ctx, uint8_t *dec, size_t dec_size);

/**
 * @brief COBS decode increment: Add new encoded data to the decoding process
 * @param ctx COBS context
 * @param enc Encoded buffer
 * @param enc_size Encoded buffer length
 * @return Error code
 */
int32_t cobs_decode_increment(t_cobs *ctx, const uint8_t *enc, size_t enc_size);

/**
 * @brief COBS decode end: Finish the decoding process
 * @param ctx COBS context
 * @param decoded Number of bytes decoded
 * @return Error code
 */
int32_t cobs_decode_end(const t_cobs *ctx, size_t *decoded);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: COBS -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_COBS_H_ */
