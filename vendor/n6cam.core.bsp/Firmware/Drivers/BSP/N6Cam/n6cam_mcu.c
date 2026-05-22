/**
 *******************************************************************************
 * @file    n6cam_mcu.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam MCU API
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
#include "n6cam_mcu.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup MCU
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

__STATIC_FORCEINLINE int bsp_mcu_cache_enabled(void)
{
  #if defined (__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  if (SCB->CCR & SCB_CCR_DC_Msk)
  {
    return 1;  /* return `1` if DCache is enabled */
  }
  #endif
  return 0;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

void bsp_mcu_cache_enable(void)
{
  /* Enable I-Cache */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_ICACTIVE_Msk;
  SCB_EnableICache();

  #if ENABLE_DCACHE
  /* Enable D-Cache */
  MEMSYSCTL->MSCR |= MEMSYSCTL_MSCR_DCACTIVE_Msk;
  SCB_EnableDCache();
  #endif /* ENABLE_DCACHE */
}

void bsp_mcu_cache_disable(void)
{
  SCB_DisableICache();
  #if ENABLE_DCACHE
  SCB_DisableDCache();
  #endif /* ENABLE_DCACHE */
}

void bsp_mcu_cache_clean(uint32_t *addr, int32_t size)
{
  #if ENABLE_DCACHE
  if(bsp_mcu_cache_enabled())
  {
    SCB_CleanDCache_by_Addr(addr, size);
  }
  #endif /* ENABLE_DCACHE */
}

void bsp_mcu_cache_clean_range(uint32_t start, uint32_t end)
{
  bsp_mcu_cache_clean((uint32_t*)start, (int32_t)(end - start));
}

void bsp_mcu_cache_invalidate(uint32_t *addr, int32_t size)
{
  #if ENABLE_DCACHE
  if(bsp_mcu_cache_enabled())
  {
    SCB_InvalidateDCache_by_Addr((void*)addr, size);
  }
  #endif /* ENABLE_DCACHE */
}

void bsp_mcu_cache_invalidate_range(uint32_t start, uint32_t end)
{
  bsp_mcu_cache_invalidate((uint32_t*)start, (int32_t)(end - start));
}

void bsp_mcu_cache_clean_invalidate(uint32_t *addr, int32_t size)
{
  #if ENABLE_DCACHE
  if(bsp_mcu_cache_enabled())
  {
    SCB_CleanInvalidateDCache_by_Addr(addr, size);
  }
  #endif /* ENABLE_DCACHE */
}

void bsp_mcu_cache_clean_invalidate_range(uint32_t start, uint32_t end)
{
  bsp_mcu_cache_clean_invalidate((uint32_t*)start, (int32_t)(end - start));
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
* @} <!-- End: MCU -->
*//*--------------------------------------------------------------------------*/
