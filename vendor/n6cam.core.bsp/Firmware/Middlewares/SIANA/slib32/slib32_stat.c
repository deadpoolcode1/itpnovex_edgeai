/**
 *******************************************************************************
 * @file    slib32_stat.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements statistics utilities
 *
 * Statistics:
 * - Frequency: Number of counts over a period of time (normalized).
 * - Time: Time between start/stop (using Cumulative Average or Exponential Weighted Average).
 *
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
#include <math.h>
#include <string.h>

#include "slib32_stat.h"
#include "slib32_tick.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Stat
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

/** Default ticks per second */
#ifndef SLIB32_STAT_TPS
  #define SLIB32_STAT_TPS         1000U
#endif /* SLIB32_STAT_TPS */

/** Default EWA alpha/smoothing factor.
 * A SMA (simple moving average) equivalent window could be computed based on:
 * (1) Number of observations > window = 1 / (1 - alpha)
 * (2) Center of mass/lag     > window = (2 / alpha) - 1
 */
#ifndef SLIB32_STAT_EWA_ALPHA
  #define SLIB32_STAT_EWA_ALPHA   0.9f      /* (1) window = 10 */
#endif /* SLIB32_STAT_EWA_ALPHA */

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

/** Statistics instance */
typedef struct
{
  bool            init;         /*!< Initialization flag */
  size_t          size;         /*!< Number of entries */
  t_stat_entry    *entry;       /*!< Statistics entries */
  bool            ewa_enable;   /*!< Time: EWA enable */
  float           ewa_alpha;    /*!< Time: EWA alpha */
  uint32_t        freq_tps;     /*!< Ticks per second */
  /* RTOS support */
  t_rtos_lock_fn  lock_fn;      /*!< Lock handler function */
  void            *lock;        /*!< Lock object */
} t_stat;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_stat _stat = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _stat_update(t_stat_entry *entry, uint32_t value);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t stat_init(t_stat_entry *entry, size_t size)
{
  /* Validate */
  if (!entry || (size == 0U))
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (_stat.init)
  {
    return SLIB32_OK;
  }

  /* Initialize */
  memset(&_stat, 0U, sizeof(t_stat));
  _stat.init       = true;
  _stat.size       = size;
  _stat.entry      = entry;
  _stat.ewa_enable = false;
  _stat.ewa_alpha  = SLIB32_STAT_EWA_ALPHA;
  _stat.freq_tps   = SLIB32_STAT_TPS;
  _stat.lock_fn    = NULL;
  _stat.lock       = NULL;
  return SLIB32_OK;
}

int32_t stat_init_rtos(t_rtos_lock_fn lock_fn, void *lock)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!lock_fn || !lock)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Initialize RTOS support */
  _stat.lock_fn = lock_fn;
  _stat.lock    = lock;
  return SLIB32_OK;
}

int32_t stat_acquire(bool acquire)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Lock */
  if (_stat.lock_fn)
  {
    _stat.lock_fn(_stat.lock, acquire);
  }
  return SLIB32_OK;
}

int32_t stat_ewa_enable(bool enable)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Update */
  stat_acquire(true);
  _stat.ewa_enable = enable;
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_ewa_alpha(float ewa)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Update */
  stat_acquire(true);
  _stat.ewa_alpha = ewa;
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_entry(size_t idx, t_stat_entry *entry)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (idx >= _stat.size)
  {
    return SLIB32_ERROR_INDEX;
  }
  if (!entry)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Copy entry */
  stat_acquire(true);
  memcpy(entry, &_stat.entry[idx], sizeof(t_stat_entry));
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_update(size_t idx, uint32_t value)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (idx >= _stat.size)
  {
    return SLIB32_ERROR_INDEX;
  }

  /* Update */
  stat_acquire(true);
  _stat_update(&_stat.entry[idx], value);
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_reset(size_t idx)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (idx >= _stat.size)
  {
    return SLIB32_ERROR_INDEX;
  }

  /* Reset stat */
  stat_acquire(true);
  memset(&_stat.entry[idx], 0U, sizeof(t_stat_entry));
  stat_acquire(false);
  return SLIB32_OK;
}

float stat_value(size_t idx)
{
  /* Validate */
  if (!_stat.init || (idx >= _stat.size))
  {
    return NAN;
  }

  /* Get value */
  stat_acquire(true);
  float value = _stat.entry[idx].value;
  stat_acquire(false);
  return value;
}

int32_t stat_freq_set_tps(uint32_t tps)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Update */
  stat_acquire(true);
  _stat.freq_tps = tps;
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_freq_count(size_t idx, uint32_t count)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (idx >= _stat.size)
  {
    return SLIB32_ERROR_INDEX;
  }

  /* Count event */
  stat_acquire(true);
  _stat.entry[idx].count += count;
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_freq_update(size_t idx)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (idx >= _stat.size)
  {
    return SLIB32_ERROR_INDEX;
  }

  /* Update */
  stat_acquire(true);
  t_stat_entry *entry = &_stat.entry[idx];
  entry->value = (float)(_stat.freq_tps * entry->count) / (float)tick_get_since(entry->start);
  entry->start = tick_get();
  entry->count = 0U;
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_time_start(size_t idx)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (idx >= _stat.size)
  {
    return SLIB32_ERROR_INDEX;
  }

  /* Start */
  stat_acquire(true);
  _stat.entry[idx].start = tick_get();
  stat_acquire(false);
  return SLIB32_OK;
}

int32_t stat_time_stop(size_t idx)
{
  /* Validate */
  if (!_stat.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (idx >= _stat.size)
  {
    return SLIB32_ERROR_INDEX;
  }

  /* Update */
  stat_acquire(true);
  _stat_update(&_stat.entry[idx], tick_get_since(_stat.entry[idx].start));
  stat_acquire(false);
  return SLIB32_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Backend for updating a statistics entry
 * @param entry Statistics entry
 * @param value New value
 */
static void _stat_update(t_stat_entry *entry, uint32_t value)
{
  entry->last = value;
  if (_stat.ewa_enable)
  {
    /* Use Exponential Weighted Average */
    entry->value = (_stat.ewa_alpha * (float)value) + ((1.0f - _stat.ewa_alpha) * entry->value);
  }
  else
  {
    /* Use custom cumulative average */
    if ((UINT32_MAX - value) < entry->accum)
    {
      entry->accum = (uint32_t)entry->value + value;
      entry->count = 2U;
    }
    else
    {
      entry->accum += value;
      entry->count++;
    }
    entry->value = (float)entry->accum / (float)entry->count;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Stat -->
*//*--------------------------------------------------------------------------*/
