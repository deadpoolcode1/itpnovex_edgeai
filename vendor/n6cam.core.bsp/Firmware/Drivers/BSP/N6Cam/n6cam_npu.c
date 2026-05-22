/**
 *******************************************************************************
 * @file    n6cam_npu.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam NPU API
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
#include "common.h"
#include "n6cam_npu.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup NPU
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

CACHEAXI_HandleTypeDef  hcacheaxi         = { 0 };
RAMCFG_HandleTypeDef    hramcfg_axisram3  = { 0 };
RAMCFG_HandleTypeDef    hramcfg_axisram4  = { 0 };
RAMCFG_HandleTypeDef    hramcfg_axisram5  = { 0 };
RAMCFG_HandleTypeDef    hramcfg_axisram6  = { 0 };

static volatile bool    _cacheaxi_enabled = false;

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

void bsp_npu_init(void)
{
  /* Enable peripheral clock */
  __HAL_RCC_NPU_CLK_ENABLE();
  __HAL_RCC_NPU_FORCE_RESET();
  __HAL_RCC_NPU_RELEASE_RESET();
}

void bsp_npu_ram_enable(void)
{
  /* Enable peripheral clock */
  __HAL_RCC_RAMCFG_CLK_ENABLE();
  __HAL_RCC_AXISRAM3_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM4_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM5_MEM_CLK_ENABLE();
  __HAL_RCC_AXISRAM6_MEM_CLK_ENABLE();

  /* Enable NPU RAMs (4x448KB) */
  hramcfg_axisram3.Instance = RAMCFG_SRAM3_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg_axisram3);

  hramcfg_axisram4.Instance = RAMCFG_SRAM4_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg_axisram4);

  hramcfg_axisram5.Instance = RAMCFG_SRAM5_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg_axisram5);

  hramcfg_axisram6.Instance = RAMCFG_SRAM6_AXI;
  HAL_RAMCFG_EnableAXISRAM(&hramcfg_axisram6);
}

void bsp_npu_cache_enable(void)
{
  HAL_StatusTypeDef status;

  /* Initialize */
  if (!_cacheaxi_enabled)
  {
    /* Enable peripheral clock */
    __HAL_RCC_CACHEAXI_CLK_ENABLE();
    __HAL_RCC_CACHEAXI_FORCE_RESET();
    __HAL_RCC_CACHEAXI_RELEASE_RESET();
    __HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();

    /* Configure peripheral */
    hcacheaxi.Instance = CACHEAXI;
    HAL_CACHEAXI_Init(&hcacheaxi);
    _cacheaxi_enabled = true;
  }
  /* Enable */
  do
  {
    status = HAL_CACHEAXI_Enable(&hcacheaxi);
  } while (status == HAL_BUSY);
}

void bsp_npu_cache_disable(void)
{
  HAL_StatusTypeDef status;
  if (_cacheaxi_enabled)
  {
    do
    {
      status = HAL_CACHEAXI_Disable(&hcacheaxi);
    } while (status == HAL_BUSY);
  }
}

void bsp_npu_cache_invalidate(void)
{
  if (_cacheaxi_enabled)
  {
    HAL_CACHEAXI_Invalidate(&hcacheaxi);
  }
}

void bsp_npu_cache_clean(uint32_t *addr, int32_t size)
{
  if (_cacheaxi_enabled)
  {
    HAL_CACHEAXI_CleanByAddr(&hcacheaxi, addr, size);
  }
}

void bsp_npu_cache_clean_range(uint32_t start, uint32_t end)
{
  bsp_npu_cache_clean((uint32_t*)start, (uint32_t)(end - start));
}

void bsp_npu_cache_clean_invalidate(uint32_t *addr, int32_t size)
{
  if (_cacheaxi_enabled)
  {
    HAL_CACHEAXI_CleanInvalidByAddr(&hcacheaxi, addr, size);
  }
}

void bsp_npu_cache_clean_invalidate_range(uint32_t start, uint32_t end)
{
  bsp_npu_cache_clean_invalidate((uint32_t*)start, (uint32_t)(end - start));
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
* @} <!-- End: NPU -->
*//*--------------------------------------------------------------------------*/
