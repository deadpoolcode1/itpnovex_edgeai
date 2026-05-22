/**
 *******************************************************************************
 * @file    slib32_fifo8.h
 * @author  SIANA Systems
 * @date    2014
 * @brief   Defines the API for the uint8_t fifo
 * @todo    Add support for RTOS locks
 *          Fix return values: use either error codes or elements available
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
#ifndef _SLIB32_FIFO8_H_
#define _SLIB32_FIFO8_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "slib32_core.h"
#include "slib32_error.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup FIFO8
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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Represents a FIFO instance.
 * @param buffer  Pointer to array of uint8_t elements
 * @param in      Input data index.
 * @param out     Output data index.
 * @param count   # of elements in fifo.
 * @param size    Total # of slots in fifo.
 * @param max     ???
 */
typedef struct
{
  uint8_t *buffer;
  int32_t in;
  int32_t out;
  int32_t count;
  int32_t size;
  int32_t max;
} t_fifo8;

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
 * @brief Initializes a fifo instance.
 *
 * @param fifo Pointer to fifo instance.
 * @param buffer container array of int32 elements
 * @param size max size of int32 in container array.
 * @retval true on success, false on failure.
 */
bool fifo8_init(t_fifo8 *fifo, uint8_t buffer[], int32_t size);

/**
 * @brief Resets the fifo.
 * @param fifo Pointer to fifo instance.
 * @retval true on success, false on failure.
 */
bool fifo8_reset(t_fifo8 *fifo);

/**
 * @brief Pushes an element into the fifo.
 *
 * @param fifo Pointer to fifo instance.
 * @param data Incoming data.
 * @return # of slots available.
 */
int32_t fifo8_push(t_fifo8 *fifo, uint8_t data);

/**
 * @brief Pushes several elements into the fifo.
 *
 * @param fifo Pointer to fifo instance.
 * @param data Incoming array of data.
 * @param len # of elements to push.
 * @return # of elements successfully pushed.
 */
int32_t fifo8_push_chunk(t_fifo8 *fifo, const uint8_t *data, int32_t len);

/**
 * @brief Pops an element from the fifo.
 *
 * @param fifo Pointer to fifo instance.
 * @param data Pointer to receiving data.
 * @return # of remaining elements.
 */
int32_t fifo8_pop(t_fifo8 *fifo, uint8_t *data);

/**
 * @brief Pops several elements from the fifo.
 *
 * @param fifo Pointer to fifo instance.
 * @param data Destination array of data.
 * @param len Max. # of elements to pop
 * @return # of elements successfully popped.
 */
int32_t fifo8_pop_chunk(t_fifo8 *fifo, uint8_t *data, int32_t len);

/**
 * @brief Returns the number of elements stored in the fifo.
 * @param fifo Pointer to fifo instance.
 * @return Amount of data inside the fifo.
 */
int32_t fifo8_count(const t_fifo8 *fifo);

/**
 * @brief Get the fifo total size (capacity.)
 * @param fifo Pointer to fifo instance.
 * @return Fifo capacity.
 */
int32_t fifo8_size(const t_fifo8 *fifo);

/**
 * @brief Returns the number of free slots in the fifo.
 * @param fifo Pointer to fifo instance.
 * @return Amount of free slots.
 */
int32_t fifo8_available(const t_fifo8 *fifo);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: FIFO8 -->
*//*--------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif /* _SLIB32_FIFO8_H_ */
