/**
 *******************************************************************************
 * @file    slib32_fifo8.c
 * @author  SIANA Systems
 * @date    2014
 * @brief   Implements an uint8_t fifo
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
#include "slib32_fifo8.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup FIFO8
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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

bool fifo8_init(t_fifo8 *fifo, uint8_t buffer[], int32_t size)
{
  fifo->buffer = buffer;
  fifo->size = size;

  return fifo8_reset(fifo);
}

bool fifo8_reset(t_fifo8 *fifo)
{
  fifo->in = 0;
  fifo->out = 0;
  fifo->count = 0;

  return true;
}

int32_t fifo8_push(t_fifo8 *fifo, uint8_t data)
{
  /* room in container? */
  if (fifo->count < fifo->size)
  {
    /* yes -> push data */
    fifo->buffer[fifo->in++] = data;
    fifo->count++;

    /* make circular... */
    if (fifo->in == fifo->size)
    {
      fifo->in = 0;
    }

    return (fifo->size - fifo->count);
  }

  return SLIB32_ERROR_FULL;
}

int32_t fifo8_push_chunk(t_fifo8 *fifo, const uint8_t *data, int32_t len)
{
  int32_t count = 0;

  while (count < len)
  {
    if (fifo8_push(fifo, data[count]) == SLIB32_ERROR_FULL)
    {
      break;
    }

    count++;
  }

  return count;
}

int32_t fifo8_pop(t_fifo8 *fifo, uint8_t *data)
{
  /* anything in fifo? */
  if (fifo->count > 0)
  {
    /* yes -> pop it */
    *data = fifo->buffer[fifo->out++];
    fifo->count--;

    /* make circular */
    if (fifo->out == fifo->size)
    {
      fifo->out = 0;
    }

    return (fifo->size - fifo->count);
  }

  return SLIB32_ERROR_EMPTY;
}

int32_t fifo8_pop_chunk(t_fifo8 *fifo, uint8_t *data, int32_t len)
{
  int32_t count = 0;

  while (count < len)
  {
    if (fifo8_pop(fifo, &data[count]) == SLIB32_ERROR_EMPTY)
    {
      break;
    }

    count++;
  }

  return count;
}

int32_t fifo8_count(const t_fifo8 *fifo)
{
  return fifo->count;
}

int32_t fifo8_size(const t_fifo8 *fifo)
{
  return fifo->size;
}

int32_t fifo8_available(const t_fifo8 *fifo)
{
  return (fifo->size - fifo->count);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: FIFO8 -->
*//*--------------------------------------------------------------------------*/
