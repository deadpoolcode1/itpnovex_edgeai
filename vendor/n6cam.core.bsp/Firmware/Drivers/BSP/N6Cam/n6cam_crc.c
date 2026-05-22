/**
 *******************************************************************************
 * @file    n6cam_crc.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam CRC API
 *******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
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
#include "n6cam_crc.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup CRC
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

CRC_HandleTypeDef hcrc;
static TX_MUTEX   _crc_mtx;
static bool       _crc_init = false;

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

int32_t bsp_crc_init(void)
{
  int32_t status;

  /* Validate */
  if (_crc_init)
  {
    return BSP_OK;
  }

  /* Init Mutex */
  status = tx_mutex_create(&_crc_mtx, "tx.mtx.crc", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return BSP_ERROR_COMPONENT;
  }

  /* Init CRC */
  __HAL_RCC_CRC_CLK_ENABLE();

  hcrc.Instance                     = CRC;
  hcrc.Init.CRCLength               = CRC_POLYLENGTH_16B;
  hcrc.InputDataFormat              = CRC_INPUTDATA_FORMAT_BYTES;
  hcrc.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_DISABLE;
  hcrc.Init.GeneratingPolynomial    = 0x1021U;
  hcrc.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_DISABLE;
  hcrc.Init.InitValue               = 0xFFFFU;
  hcrc.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  status = HAL_CRC_Init(&hcrc);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  _crc_init = true;
  return BSP_OK;
}

int32_t bsp_crc_acquire(bool acquire)
{
  /* Validate */
  if (!_crc_init)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Capture */
  rtos_mutex_acquire(&_crc_mtx, acquire);
  return BSP_OK;
}

uint32_t bsp_crc_calculate(uint32_t* buff, uint32_t size)
{
  if (!_crc_init)
  {
    /* CRC not initialized */
    return 0;
  }
  if (!buff || (size == 0))
  {
    /* Nothing to calculate */
    return (uint32_t)hcrc.Instance->INIT;
  }

  /* Run CRC */
  uint32_t crc;
  rtos_mutex_acquire(&_crc_mtx, true);
  crc = HAL_CRC_Calculate(&hcrc, buff, size);
  rtos_mutex_acquire(&_crc_mtx, false);
  return crc;
}

uint32_t bsp_crc_accumulate(uint32_t* buff, uint32_t size)
{
  /* Validate */
  if (!_crc_init)
  {
    /* CRC not initialized */
    return 0;
  }
  if (!buff || (size == 0))
  {
    /* Nothing to accumulate */
    return (uint32_t)hcrc.Instance->DR;
  }

  /* Run CRC */
  uint32_t crc;
  rtos_mutex_acquire(&_crc_mtx, true);
  crc = HAL_CRC_Accumulate(&hcrc, buff, size);
  rtos_mutex_acquire(&_crc_mtx, false);
  return crc;
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
* @} <!-- End: BSP -->
* @} <!-- End: CRC -->
*//*--------------------------------------------------------------------------*/
