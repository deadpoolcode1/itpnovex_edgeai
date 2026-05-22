/**
 *******************************************************************************
 * @file    n6cam_watchdog.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam Watchdog API
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
#include "n6cam_watchdog.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup Watchdog
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

/* WWDG settings */
#define WWDG_WINDOW    0x50
#define WWDG_COUNTER   0x7F

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

WWDG_HandleTypeDef    hwwdg;
static volatile bool  _wwdg_init      = false;
static volatile bool  _wwdg_released  = false;
static volatile bool  _wwdg_status    = false;
static uint32_t       _wwdg_window;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

extern void HAL_WWDG_MspInit(WWDG_HandleTypeDef *hwwdg);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_watchdog_init(void)
{
  int32_t status;

  /* Store initialization status */
  _wwdg_status = __HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != 0x00U;

  /* Reset flags */
  __HAL_RCC_CLEAR_RESET_FLAGS();

  /* Configure
   * 1. Prescaler to 128
   * 2. Counter to 0x7F (127) and window to 0x50 (80)
   *
   * Timing
   * 1. CLK period  = (4096 * 128) / (PCLK1 / 1000) = 2.621 ms
   * 2. Timeout     = (127 + 1) * 2.621 ms = 335.488 ms
   * 3. Window      = (127 - 80 + 1) * 2.621 ms = 125.376 ms
   */
  hwwdg.Instance        = WWDG;
  hwwdg.Init.Prescaler  = WWDG_PRESCALER_128;
  hwwdg.Init.Window     = WWDG_WINDOW;
  hwwdg.Init.Counter    = WWDG_COUNTER;
  hwwdg.Init.EWIMode    = WWDG_EWI_DISABLE;
  status = HAL_WWDG_Init(&hwwdg);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Configure timeout */
  _wwdg_window = bsp_watchdog_timeout((hwwdg.Init.Counter - hwwdg.Init.Window) + 1U) + 1U;
  _wwdg_init   = true;
  return BSP_OK;
}

uint32_t bsp_watchdog_window(void)
{
  return _wwdg_window;
}

uint32_t bsp_watchdog_timeout(uint32_t count)
{
  /* This assumes APB1 = PCLK1 */
  uint32_t presc = (1U << ((hwwdg.Init.Prescaler) >> WWDG_CFR_WDGTB_Pos));
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  return (4096U * presc * count) / (pclk1 / 1000U);
}

bool bsp_watchdog_reboot(void)
{
  return _wwdg_status;
}

void bsp_watchdog_release(void)
{
  /* Release the dog */
  if (_wwdg_init)
  {
    _wwdg_released = true;
  }
}

void bsp_watchdog_refresh(void)
{
  /* Pat the dog only if initialized and not released */
  if (_wwdg_init && !_wwdg_released)
  {
    HAL_WWDG_Refresh(&hwwdg);
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

void HAL_WWDG_MspInit(WWDG_HandleTypeDef *hwwdg)
{
  if (hwwdg->Instance == WWDG)
  {
    /* Enable peripheral clock */
    __HAL_RCC_WWDG_CLK_ENABLE();
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: Watchdog -->
*//*--------------------------------------------------------------------------*/
