/**
 *******************************************************************************
 * @file    slib32_bitmask.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for bitmask utilities
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
#ifndef _SLIB32_BITMASK_H_
#define _SLIB32_BITMASK_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Bitmask
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Get a mask with the Nth bit set.
 * Examples:
 * - BIT(0) -> 0b00000001
 * - BIT(5) -> 0b00100000
 */
#ifndef BIT
  #define BIT(n)                (1U << (n))
#endif /* BIT */

/** Get a mask with the first N bits set.
 * Examples:
 * - BITMASK(0) -> 0b00000000
 * - BITMASK(5) -> 0b00011111
 */
#ifndef BITMASK
  #define BITMASK(n)            (BIT(n) - 1U)
#endif /* BITMASK */

/** Get the low nibble of a byte
 * Examples:
 * - NIBBLE_LOW(0x12U) -> 0x2U
 */
#ifndef NIBBLE_LOW
  #define NIBBLE_LOW(x)         (0x0FU & (x))
#endif /* NIBBLE_LOW */

/** Get the high nibble of a byte
 * Examples:
 * - NIBBLE_HIGH(0x12U) -> 0x1U
 */
#ifndef NIBBLE_HIGH
  #define NIBBLE_HIGH(x)        (NIBBLE_LOW((x) >> 4U))
#endif /* NIBBLE_HIGH */

/** Get the low byte of a word
 * Examples:
 * - BYTE_LOW(0x1234U) -> 0x34U
 */
#ifndef BYTE_LOW
  #define BYTE_LOW(x)           (0xFFU & (x))
#endif /* BYTE_LOW */

/** Get the high byte of a word
 * Examples:
 * - BYTE_HIGH(0x1234U) -> 0x12U
 */
#ifndef BYTE_HIGH
  #define BYTE_HIGH(x)          BYTE_LOW((x) >> 8U)
#endif /* BYTE_HIGH */

/** Get the low word of a dword
 * Examples:
 * - WORD_LOW(0x12345678U) -> 0x5678U
 */
#ifndef WORD_LOW
  #define WORD_LOW(x)           (0xFFFFU & (x))
#endif /* WORD_LOW */

/** Get the high word of a dword
 * Examples:
 * - WORD_HIGH(0x12345678U) -> 0x1234U
 */
#ifndef WORD_HIGH
  #define WORD_HIGH(x)          WORD_LOW((x) >> 16U)
#endif /* WORD_HIGH */

/** Reverse the nibbles of a byte
 * Examples:
 * - U8_REVERSE_NIBBLES(0x12U) -> 0x21U
 */
#ifndef U8_REVERSE_NIBBLES
  #define U8_REVERSE_NIBBLES(x) ((NIBBLE_LOW(x) << 4U) + NIBBLE_HIGH(x))
#endif /* U8_REVERSE_NIBBLES */

/** Reverse the bytes of a word
 * Examples:
 * - U16_REVERSE_BYTES(0x1234U) -> 0x3412U
 */
#ifndef U16_REVERSE_BYTES
  #define U16_REVERSE_BYTES(x)  ((BYTE_LOW(x) << 8U) + BYTE_HIGH(x))
#endif /* U16_REVERSE_BYTES */

/** Reverse the bytes of a dword
 * Examples:
 * - U32_REVERSE_BYTES(0x12345678U) -> 0x78563412U
 */
#ifndef U32_REVERSE_BYTES
  #define U32_REVERSE_BYTES(x)  ((U16_REVERSE_BYTES(WORD_LOW(x)) << 16U) + U16_REVERSE_BYTES(WORD_HIGH(x)))
#endif /* U32_REVERSE_BYTES */

/** Reverse the words of a dword
 * Examples:
 * - U32_REVERSE_WORDS(0x12345678U) -> 0x56781234U
 */
#ifndef U32_REVERSE_WORDS
  #define U32_REVERSE_WORDS(x)  ((WORD_LOW(x) << 16U) + WORD_HIGH(x))
#endif /* U32_REVERSE_WORDS */

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

/**
 * @brief Set a bit in the bitmask
 *
 * @param mask Ptr. to mask
 * @param bit Bit to set
 * @return Bitmask
 */
uint32_t bitmask_set(uint32_t *mask, uint8_t bit);

/**
 * @brief Clear a bit in the bitmask
 *
 * @param mask Ptr. to mask
 * @param bit Bit to clear
 * @return Bitmask
 */
uint32_t bitmask_clear(uint32_t *mask, uint8_t bit);

/**
 * @brief Update a bit in the bitmask
 *
 * @param mask Ptr. to mask
 * @param bit Bit to update
 * @param value New value
 * @return Bitmask
 */
uint32_t bitmask_update(uint32_t *mask, uint8_t bit, bool value);

/**
 * @brief Check if a bits is set
 *
 * @param mask Ptr. to mask
 * @param bit Bit to read
 * @return True if bit is set
 */
bool bitmask_get(const uint32_t *mask, uint8_t bit);

/**
 * @brief Check if a bits is set and clear it
 *
 * @param mask Ptr. to mask
 * @param bit Bit to read and clear
 * @return True if bit was set
 */
bool bitmask_get_and_clear(uint32_t *mask, uint8_t bit);

/**
 * @brief Check if any bit is set
 *
 * @param mask Ptr. to mask
 * @return True if any bit is set
 */
bool bitmask_any(const uint32_t *mask);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Bitmask -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_BITMASK_H_ */
