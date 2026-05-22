/**
 *******************************************************************************
 * @file    slib32_buffer.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for generic buffer manipulation
 * @note    In this context a "buffer" is an array of custom data structures
 *          that can be pushed and popped in a FIFO or LIFO manner with optional
 *          overflow mode.
 *          Also, this implementation can be used in a RTOS environment when the
 *          loch-handler function is set.
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
#ifndef _SLIB32_BUFFER_H_
#define _SLIB32_BUFFER_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "slib32_core.h"
#include "slib32_error.h"
#include "slib32_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Buffer
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

/** Enumeration with buffer operation modes */
typedef enum
{
  BUFFER_OPMODE_R_FIFO     = 0x01U,   /*!< R_FIFO: Reading mode: FIFO -> Queue */
  BUFFER_OPMODE_R_LIFO     = 0x02U,   /*!< R_LIFO: Reading mode: LIFO -> Stack */
  BUFFER_OPMODE_W_OVERFLOW = 0x04U,   /*!< W_OVF:  Write mode:   Overflow > Oldest items are overwritten */
  BUFFER_OPMODE_DEFAULT    = BUFFER_OPMODE_R_FIFO | BUFFER_OPMODE_W_OVERFLOW,
} t_buffer_opmode;

/** Buffer controller instance */
typedef struct
{
  bool            init;       /*!< Initialization flag */
  uint8_t         *item;      /*!< Array of items */
  uint32_t        mode;       /*!< Buffer operation mode */
  size_t          buff_size;  /*!< Buffer size (# items) */
  size_t          item_size;  /*!< Item size (bytes) */
  size_t          capacity;   /*!< Buffer size (bytes) */
  volatile size_t stored;     /*!< Buffer usage (bytes) */
  volatile size_t head;       /*!< Oldest item */
  volatile size_t tail;       /*!< Store new items */
  /* RTOS support */
  t_rtos_lock_fn  lock_fn;    /*!< Lock handler function */
  void            *lock;      /*!< Lock object */
} t_buffer;

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
 * @brief Initializes a buffer instance
 *
 * @note This function is thread unsafe. Use with care
 *
 * @param buff Buffer instance
 * @param item Array of items
 * @param size Array size (# items)
 * @param item_size Item size (bytes)
 * @param mode Operation mode (see t_buff_mode)
 * @param clear True to purge the items buffer (fill with zeros)
 * @return Error code
 */
int32_t buffer_init(t_buffer *buff, void *item, size_t size, size_t item_size, uint32_t mode, bool clear);

/**
 * @brief Initializes the buffer RTOS support
 * @param buff Buffer instance
 * @param lock_fn Lock handler function
 * @param lock Lock object, such as a mutex or semaphore
 * @return Error code
 */
int32_t buffer_init_rtos(t_buffer *buff, t_rtos_lock_fn lock_fn, void *lock);

/**
 * @brief Acquire/release the buffer
 * @param buff Buffer instance
 * @param acquire True to acquire, false to release
 * @return Error code
 */
int32_t buffer_acquire(t_buffer *buff, bool acquire);

/**
 * @brief Clears the buffer
 * @param buff Buffer instance
 * @return Error code
 */
int32_t buffer_clear(t_buffer *buff);

/**
 * @brief Pushes an item into the buffer
 *
 * @note  Depending if the buffer is on overflow mode, the oldest item may be
 *        overwritten (an error is returned otherwise)
 *
 * @param buff Buffer instance
 * @param item Incoming item
 * @return Error code
 */
int32_t buffer_push(t_buffer *buff, const void *item);

/**
 * @brief Pushes several items into the buffer
 *
 * @note  Depending if the buffer is on overflow mode, the oldest item may be
 *        overwritten (an error is returned otherwise)
 *        If the number of items to push is bigger than the capacity, only the
 *        last items are pushed
 *
 * @param buff Buffer instance
 * @param item Incoming array of items
 * @param size Number of items to push
 * @param pushed Number of items actually pushed
 * @return Error code
 */
int32_t buffer_push_chunk(t_buffer *buff, const void *item, size_t size, size_t *pushed);

/**
 * @brief Pops an item from the buffer
 *
 * @note  Depending on the read mode (FIFO/LIFO), the oldest or newest item is popped
 *
 * @param buff Buffer instance
 * @param item Receiving item
 * @return Error code
 */
int32_t buffer_pop(t_buffer *buff, void *item);

/**
 * @brief Pops several items from the buffer
 *
 * @note  Depending on the read mode (FIFO/LIFO), the oldest or newest item is popped
 *
 * @param buff Buffer instance
 * @param item Receiving array of items
 * @param size Number of items to pop
 * @param popped Number of items actually popped
 * @return Error code
 */
int32_t buffer_pop_chunk(t_buffer *buff, void *item, size_t size, size_t *popped);

/**
 * @brief Reads an item from the buffer without popping it
 *
 * @note  Depending on the read mode (FIFO/LIFO), the oldest or newest item is read
 *
 * @param buff Buffer instance
 * @param item Receiving item
 * @param index Item index
 * @return Error code
 */
int32_t buffer_peek(t_buffer *buff, void *item, size_t index);

/**
 * @brief Returns if the buffer is full
 * @param buff Buffer instance
 * @return True if buffer is full
 */
bool buffer_is_full(const t_buffer *buff);

/**
 * @brief Returns if the buffer is empty
 * @param buff Buffer instance
 * @return True if buffer is empty
 */
bool buffer_is_empty(const t_buffer *buff);

/**
 * @brief Get the buffer total size (capacity)
 * @param buff Buffer instance
 * @return Buffer size (# items)
 */
size_t buffer_get_size(const t_buffer *buff);

/**
 * @brief Get the number of items stored in the buffer
 * @param buff Buffer instance
 * @return Items stored (# items)
 */
size_t buffer_get_count(const t_buffer *buff);

/**
 * @brief Get the number of free slots in the buffer
 * @param buff Buffer instance
 * @return Free slots (# items)
 */
size_t buffer_get_available(const t_buffer *buff);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Buffer -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_BUFFER_H_ */
