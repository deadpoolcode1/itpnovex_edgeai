/**
 *******************************************************************************
 * @file    slib32_buffer.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements generic buffer manipulation
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
#include <string.h>

#include "slib32_buffer.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Buffer
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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t _buff_push_backend(t_buffer *ctrl, const void *item);
static int32_t _buff_pop_backend(t_buffer *ctrl, void *item);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t buffer_init(t_buffer *buff, void *item, size_t size, size_t item_size, uint32_t mode, bool clear)
{
  /* Sanity check */
  if (!buff || !item || (item_size == 0) || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Initialize buffer */
  buff->init      = true;
  buff->item      = (uint8_t*)item;
  buff->buff_size = size;
  buff->item_size = item_size;
  buff->capacity  = (size * item_size);
  buff->mode      = mode;
  buff->lock_fn   = NULL;
  buff->lock      = NULL;

  /* Clear buffer if required */
  if (clear)
  {
    buffer_clear(buff);
  }
  return SLIB32_OK;
}

int32_t buffer_init_rtos(t_buffer *buff, t_rtos_lock_fn lock_fn, void *lock)
{
  /* Validate */
  if (!buff || !lock_fn || !lock)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Initialize RTOS support */
  buff->lock_fn = lock_fn;
  buff->lock    = lock;
  return SLIB32_OK;
}

int32_t buffer_acquire(t_buffer *buff, bool acquire)
{
  /* Sanity check */
  if (!buff)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }
  /* Lock */
  if (buff->lock_fn)
  {
    buff->lock_fn(buff->lock, acquire);
  }
  return SLIB32_OK;
}

int32_t buffer_clear(t_buffer *buff)
{
  /* Sanity check */
  if (!buff || !buff->item)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }

  buffer_acquire(buff, true);

  /* Process */
  memset(buff->item, 0U, buff->capacity);
  buff->stored = 0;
  buff->head   = 0;
  buff->tail   = 0;

  buffer_acquire(buff, false);

  return SLIB32_OK;
}

int32_t buffer_push(t_buffer *buff, const void *item)
{
  int32_t status;

  /* Sanity check */
  if (!buff || !buff->item || !item)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }

  buffer_acquire(buff, true);

  /* Handle push */
  status = _buff_push_backend(buff, item);

  buffer_acquire(buff, false);

  return status;
}

int32_t buffer_push_chunk(t_buffer *buff, const void *item, size_t size, size_t *pushed)
{
  int32_t       status = SLIB32_OK;
  size_t        count  = 0;
  const uint8_t *head  = item;

  /* Sanity check */
  if (!buff || !buff->item || !item)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }

  buffer_acquire(buff, true);

  /* Handle push */
  while (count < size)
  {
    status = _buff_push_backend(buff, (head + (count * buff->item_size)));
    if (status != SLIB32_OK)
    {
      break;
    }
    count++;
  }

  buffer_acquire(buff, false);

  /* Update pushed count */
  if (pushed)
  {
    *pushed = count;
  }
  return status;
}

int32_t buffer_pop(t_buffer *buff, void *item)
{
  int32_t status;

  /* Sanity check */
  if (!buff || !buff->item || !item)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }

  buffer_acquire(buff, true);

  /* Handle pop */
  status = _buff_pop_backend(buff, item);

  buffer_acquire(buff, false);

  return status;
}

int32_t buffer_pop_chunk(t_buffer *buff, void *item, size_t size, size_t *popped)
{
  int32_t status = SLIB32_OK;
  size_t  count  = 0;
  uint8_t *head  = item;

  /* Sanity check */
  if (!buff || !buff->item || !item)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }

  buffer_acquire(buff, true);

  /* Handle pop */
  while (count < size)
  {
    status = _buff_pop_backend(buff, (head + (count * buff->item_size)));
    if (status != SLIB32_OK)
    {
      break;
    }
    count++;
  }

  buffer_acquire(buff, false);

  /* Update popped count */
  if (popped)
  {
    *popped = count;
  }
  return (count == 0) ? SLIB32_ERROR_EMPTY : SLIB32_OK;
}

int32_t buffer_peek(t_buffer *buff, void *item, size_t index)
{
  size_t offset;

  /* Sanity check */
  if (!buff || !buff->item || !item)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (!buff->init)
  {
    return SLIB32_ERROR_INIT;
  }
  if ((index >= buff->buff_size) || (index >= buffer_get_count(buff)))
  {
    return SLIB32_ERROR_INDEX;
  }

  buffer_acquire(buff, true);

  /* Read item */
  if (buff->mode & BUFFER_OPMODE_R_FIFO)
  {
    offset = (buff->head + (index * buff->item_size)) % buff->capacity;
  }
  else
  {
    offset = (buff->capacity + buff->tail - ((index + 1) * buff->item_size)) % buff->capacity;
  }
  memcpy(item, (buff->item + offset), buff->item_size);

  buffer_acquire(buff, false);

  return SLIB32_OK;
}

bool buffer_is_full(const t_buffer *buff)
{
  /* Sanity check */
  if (!buff)
  {
    return false;
  }

  return buff->stored == buff->capacity;
}

bool buffer_is_empty(const t_buffer *buff)
{
  /* Sanity check */
  if (!buff)
  {
    return true;
  }

  return buff->stored == 0;
}

size_t buffer_get_size(const t_buffer *buff)
{
  /* Sanity check */
  if (!buff)
  {
    return false;
  }

  return buff->buff_size;
}

size_t buffer_get_count(const t_buffer *buff)
{
  /* Sanity check */
  if (!buff)
  {
    return false;
  }

  return buff->stored / buff->item_size;
}

size_t buffer_get_available(const t_buffer *buff)
{
  /* Sanity check */
  if (!buff)
  {
    return false;
  }

  return (buff->capacity - buff->stored) / buff->item_size;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Pushes an item into the buffer
 *
 * @note  This function is the backend for the public API. It is not intended to be called directly.
 *        This function is NOT performing any sanity check.
 *
 * @param ctrl Buffer controller
 * @param item Incoming item
 * @return Error code
 */
static int32_t _buff_push_backend(t_buffer *ctrl, const void *item)
{
  /* Handle overflow */
  if (!(ctrl->mode & BUFFER_OPMODE_W_OVERFLOW) && buffer_is_full(ctrl))
  {
    return SLIB32_ERROR_OVERFLOW;
  }

  /* Handle push */
  memcpy((ctrl->item + ctrl->tail), item, ctrl->item_size);
  ctrl->tail = (ctrl->tail + ctrl->item_size) % ctrl->capacity;

  /* Handle storage and read offset */
  if (ctrl->stored == ctrl->capacity)
  {
    ctrl->head = ctrl->tail;
  }
  else
  {
    ctrl->stored += ctrl->item_size;
  }
  return SLIB32_OK;
}

/**
 * @brief Pops an item from the buffer
 *
 * @note  This function is the backend for the public API. It is not intended to be called directly.
 *        This function is NOT performing any sanity check.
 *
 * @param ctrl Buffer controller
 * @param item  Receiving item
 * @return Error code
 */
static int32_t _buff_pop_backend(t_buffer *ctrl, void *item)
{
  /* Handle empty */
  if (buffer_is_empty(ctrl))
  {
    memset(item, 0U, ctrl->item_size);
    return SLIB32_ERROR_EMPTY;
  }

  /* Handle read */
  if (ctrl->mode & BUFFER_OPMODE_R_FIFO)
  {
    memcpy(item, (ctrl->item + ctrl->head), ctrl->item_size);
    ctrl->head = (ctrl->head + ctrl->item_size) % ctrl->capacity;
  }
  else
  {
    ctrl->tail = (ctrl->capacity + ctrl->tail - ctrl->item_size) % ctrl->capacity;
    memcpy(item, (ctrl->item + ctrl->tail), ctrl->item_size);
  }
  ctrl->stored -= ctrl->item_size;
  return SLIB32_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Buffer -->
*//*--------------------------------------------------------------------------*/
