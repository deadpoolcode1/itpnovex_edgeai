/**
 *******************************************************************************
 * @file    slib32_registry.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements the system registry module
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

/* This trick allows the user to define the registry data structure and the
 * default values in the registry.h file.
 */
#define SLIB32_REGISTRY_DECLARATION
#include "slib32_registry.h"
#undef SLIB32_REGISTRY_DECLARATION

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup Registry
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

/** Registry metadata */
typedef struct
{
  uint32_t version;
  uint32_t size;
  uint32_t crc;
} t_registry_meta;

/** Registry store */
typedef struct
{
  t_registry_meta meta;
  t_registry_data data;
} t_registry;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_registry           _reg          = { 0 };

/* Control */
static bool                 _reg_init     = false;
static bool                 _reg_started  = false;
static t_registry_read_fn   _reg_read_fn  = NULL;
static t_registry_write_fn  _reg_write_fn = NULL;
static t_registry_crc32_fn  _reg_crc32_fn = NULL;

/* RTOS support */
static t_rtos_lock_fn       _reg_lock_fn = NULL;
static void                 *_reg_lock   = NULL;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void     _reg_acquire(bool acquire);
static int32_t  _reg_load(void);
static int32_t  _reg_reset(void);
static int32_t  _reg_save(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t registry_init(t_registry_read_fn read, t_registry_write_fn write, t_registry_crc32_fn crc32)
{
  /* Validate */
  if (!read || !write || !crc32)
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (_reg_init)
  {
    return SLIB32_OK;
  }

  /* Initialize registry */
  _reg_init     = true;
  _reg_started  = false;
  _reg_read_fn  = read;
  _reg_write_fn = write;
  _reg_crc32_fn = crc32;
  _reg_lock_fn  = NULL;
  _reg_lock     = NULL;
  return SLIB32_OK;
}

int32_t registry_init_rtos(t_rtos_lock_fn lock_fn, void *lock)
{
  /* Validate */
  if (!_reg_init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!lock_fn || !lock)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Initialize RTOS support */
  _reg_lock_fn = lock_fn;
  _reg_lock    = lock;
  return SLIB32_OK;
}

int32_t registry_start(bool force)
{
  /* Validate */
  if (!_reg_init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!force && _reg_started)
  {
    return SLIB32_OK;
  }

  /* Acquire registry */
  _reg_acquire(true);

  /* Read registry */
  int32_t status = _reg_load();
  if (status != SLIB32_OK)
  {
    _reg_acquire(false);
    return status;
  }

  /* Check reset reasons:
   * - Empty: RESET
   * - Same version but different size: Corruption: RESET
   * - Older version but more data: Only grow on new versions: RESET
   * - Newer version: On the future, not possible: RESET
   */
  if (
    (_reg.meta.size == 0) ||
    ((_reg.meta.size != sizeof(t_registry)) && (_reg.meta.version == REGISTRY_VERSION)) ||
    ((_reg.meta.size  > sizeof(t_registry)) && (_reg.meta.version  < REGISTRY_VERSION)) ||
    (_reg.meta.version > REGISTRY_VERSION)
  )
  {
    status = _reg_reset();
    status = status == SLIB32_OK? SLIB32_RESET : status;
    goto finish;
  }

  /* Check data integrity:
   * - CRC mismatch: Corruption: RESET
   */
  if (_reg.meta.crc != _reg_crc32_fn((uint8_t*)&_reg.data, sizeof(t_registry_data)))
  {
    status = _reg_reset();
    status = status == SLIB32_OK? SLIB32_RESET : status;
    goto finish;
  }

  /* Check updates */
  if (_reg.meta.version < REGISTRY_VERSION)
  {
    /* PAST? Update NEW fields and store */
    memcpy(
      (uint8_t*)&_reg + _reg.meta.size,
      (const uint8_t*)&registry_defaults + (_reg.meta.size - sizeof(t_registry_meta)),
      sizeof(t_registry_data) - (_reg.meta.size - sizeof(t_registry_meta))
    );
    status = _reg_save();
    status = status == SLIB32_OK? SLIB32_UPDATED : status;
    goto finish;
  }

finish:
  /* Started! */
  _reg_started = true;

  /* Release registry */
  _reg_acquire(false);
  return status;
}

t_registry_data *registry_acquire(void)
{
  /* Validate */
  if (!_reg_init || !_reg_started)
  {
    return NULL;
  }

  /* Acquire registry */
  _reg_acquire(true);
  return &_reg.data;
}

void registry_release(void)
{
  /* Validate */
  if (!_reg_init || !_reg_started)
  {
    return;
  }

  /* Release registry */
  _reg_acquire(false);
}

int32_t registry_save(void)
{
  /* Validate */
  if (!_reg_init || !_reg_started)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Acquire registry */
  _reg_acquire(true);

  /* Save data */
  int32_t status = _reg_save();

  /* Release registry */
  _reg_acquire(false);
  return status;
}

const t_registry_data *registry_get_defaults(void)
{
  return &registry_defaults;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Acquire/release the registry
 * @param acquire True to acquire, false to release
 */
static void _reg_acquire(bool acquire)
{
  if (_reg_lock_fn)
  {
    _reg_lock_fn(_reg_lock, acquire);
  }
}

/**
 * @brief Load registry data
 * @return Error code
 */
static int32_t _reg_load(void)
{
  /* Load */
  int32_t size = _reg_read_fn((uint8_t*)&_reg, sizeof(t_registry));
  if (size == sizeof(t_registry))
  {
    return SLIB32_OK;
  }
  return SLIB32_ERROR_LOAD;

}

/**
 * @brief Reset registry data to defaults
 * @return Error code
 */
static int32_t _reg_reset(void)
{
  /* Set defaults */
  memcpy(
    (uint8_t*)&_reg.data,
    (const uint8_t*)&registry_defaults,
    sizeof(t_registry_data)
  );

  /* Store */
  return _reg_save();
}

/**
 * @brief Save registry data
 * @return Error code
 */
static int32_t _reg_save(void)
{
  /* Update metadata */
  _reg.meta.version = REGISTRY_VERSION;
  _reg.meta.size    = sizeof(t_registry);
  _reg.meta.crc     = _reg_crc32_fn((uint8_t*)&_reg.data, sizeof(t_registry_data));

  /* Save */
  int32_t stored = _reg_write_fn((uint8_t*)&_reg, _reg.meta.size);
  if (stored == (int32_t)_reg.meta.size)
  {
    return SLIB32_OK;
  }
  return SLIB32_ERROR_SAVE;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: Registry -->
*//*--------------------------------------------------------------------------*/
