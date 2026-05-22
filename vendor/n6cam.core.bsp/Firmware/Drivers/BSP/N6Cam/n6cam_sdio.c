/**
 *******************************************************************************
 * @file    n6cam_sdio.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam SDIO API
 *
 * @todo    Complete implementation (multiple instances, DMA, etc)
 *
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
#include "n6cam_core.h"
#include "n6cam_sdio.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup SDIO
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

#define SDMMC_IT_PRIO   5U

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

static bool       _sdio_gpio_ready = false;
static bool       _sdio_ready      = false;
SD_HandleTypeDef  hsd2 = {};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t  _bsp_sdio_init_gpio(void);
static int32_t  _bsp_sdio_deinit_gpio(void);
static int32_t  _bsp_sdio_init_peripheral(void);
static int32_t  _bsp_sdio_deinit_peripheral(void);
static void     _bsp_sdio_set_power(t_sdio_power power);

extern void     SDMMC2_IRQHandler(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_sdio_init(void)
{
  int32_t status;

  /* Validate */
  if (_sdio_ready)
  {
    return BSP_OK;
  }
  /* Init */
  status = _bsp_sdio_init_gpio();
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_sdio_init_peripheral();
  if (status != BSP_OK)
  {
    return status;
  }

  /* Set params */
  _sdio_ready = true;
  return status;
}

int32_t bsp_sdio_init_gpio(void)
{
  /* Validate */
  if (_sdio_ready || _sdio_gpio_ready)
  {
    return BSP_OK;
  }

  /* Init */
  return _bsp_sdio_init_gpio();
}

int32_t bsp_sdio_deinit(void)
{
  int32_t status;

  /* Deinit */
  status = _bsp_sdio_deinit_peripheral();
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_sdio_deinit_gpio();
  if (status != BSP_OK)
  {
    return status;
  }

  /* Set params */
  _sdio_ready = false;
  return status;
}

void bsp_sdio_set_power(t_sdio_power power)
{
  /* Validate */
  if (!_sdio_gpio_ready)
  {
    return;
  }

  /* Set power */
  _bsp_sdio_set_power(power);
}

int32_t bsp_sdio_get_info(t_sdio_info *info)
{
  int32_t status;

  /* Validate */
  if (!_sdio_ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Get card info */
  status = HAL_SD_GetCardInfo(&hsd2, info);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

bool bsp_sdio_is_busy(void)
{
  int32_t status;

  /* Validate */
  if (!_sdio_ready)
  {
    return false;
  }

  /* Get status */
  status = HAL_SD_GetCardState(&hsd2);
  return status != HAL_SD_CARD_TRANSFER;
}

bool bsp_sdio_is_detected(void)
{
  /* Validate */
  if (!_sdio_gpio_ready)
  {
    return false;
  }

  /* Get detected */
  return bsp_io_get_state(IO_SD_DETECT);
}

WEAK void bsp_sdio_detect_callback(bool detected)
{
  /* This function should be implemented by the user application. */
  UNUSED(detected);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t _bsp_sdio_init_gpio(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* Validate */
  if (_sdio_gpio_ready)
  {
    return BSP_OK;
  }

  /* Init IO */
  bsp_io_init(IO_SD_DETECT);
  bsp_io_init(IO_PWR_SD_EN);
  bsp_io_init(IO_PWR_SD_SEL);
  _bsp_sdio_set_power(SD_ON);

  /* Init peripheral */
  /* Enable power */
  HAL_PWREx_EnableVddIO5();

  /* Enable clocks */
  SDIO2_CLK_CLK_ENABLE();
  SDIO2_CMD_CLK_ENABLE();
  SDIO2_D0_CLK_ENABLE();
  SDIO2_D1_CLK_ENABLE();
  SDIO2_D2_CLK_ENABLE();
  SDIO2_D3_CLK_ENABLE();

  /* Configure GPIO */
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  gpio.Pin        = SDIO2_CLK_PIN;
  gpio.Alternate  = SDIO2_CLK_AF;
  HAL_GPIO_Init(SDIO2_CLK_PORT, &gpio);
  gpio.Pin        = SDIO2_CMD_PIN;
  gpio.Alternate  = SDIO2_CMD_AF;
  HAL_GPIO_Init(SDIO2_CMD_PORT, &gpio);
  gpio.Pin        = SDIO2_D0_PIN;
  gpio.Alternate  = SDIO2_D0_AF;
  HAL_GPIO_Init(SDIO2_D0_PORT, &gpio);
  gpio.Pin        = SDIO2_D1_PIN;
  gpio.Alternate  = SDIO2_D1_AF;
  HAL_GPIO_Init(SDIO2_D1_PORT, &gpio);
  gpio.Pin        = SDIO2_D2_PIN;
  gpio.Alternate  = SDIO2_D2_AF;
  HAL_GPIO_Init(SDIO2_D2_PORT, &gpio);
  gpio.Pin        = SDIO2_D3_PIN;
  gpio.Alternate  = SDIO2_D3_AF;
  HAL_GPIO_Init(SDIO2_D3_PORT, &gpio);

  /* Set params */
  _sdio_gpio_ready = true;
  return BSP_OK;
}

static int32_t _bsp_sdio_deinit_gpio(void)
{
  /* Validate */
  if (!_sdio_gpio_ready)
  {
    return BSP_OK;
  }

  /* Disable power */
  _bsp_sdio_set_power(SD_OFF);

  /* Deinit IO */
  bsp_io_deinit(IO_SD_DETECT);
  bsp_io_deinit(IO_PWR_SD_EN);
  bsp_io_deinit(IO_PWR_SD_SEL);

  /* Deinit peripheral */
  HAL_GPIO_DeInit(SDIO2_CLK_PORT, SDIO2_CLK_PIN);
  HAL_GPIO_DeInit(SDIO2_CMD_PORT, SDIO2_CMD_PIN);
  HAL_GPIO_DeInit(SDIO2_D0_PORT, SDIO2_D0_PIN);
  HAL_GPIO_DeInit(SDIO2_D1_PORT, SDIO2_D1_PIN);
  HAL_GPIO_DeInit(SDIO2_D2_PORT, SDIO2_D2_PIN);
  HAL_GPIO_DeInit(SDIO2_D3_PORT, SDIO2_D3_PIN);

  /* Set params */
  _sdio_gpio_ready = false;
  return BSP_OK;
}

static int32_t _bsp_sdio_init_peripheral(void)
{
  int32_t status;

  /* Start clock */
  __HAL_RCC_SDMMC2_CLK_ENABLE();

  /* Configure */
  hsd2.Instance                 = SDMMC2;
  hsd2.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  hsd2.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd2.Init.BusWide             = SDMMC_BUS_WIDE_1B;
  hsd2.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd2.Init.ClockDiv            = 0U;

  /* Init peripheral */
  status = HAL_SD_Init(&hsd2);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Enable interrupts */
  HAL_NVIC_SetPriority(SDMMC2_IRQn, SDMMC_IT_PRIO, 0);
  HAL_NVIC_EnableIRQ(SDMMC2_IRQn);
  return BSP_OK;
}

static int32_t _bsp_sdio_deinit_peripheral(void)
{
  int32_t status;

  /* Disable interrupts */
  HAL_NVIC_DisableIRQ(SDMMC2_IRQn);

  /* Deinit peripheral */
  status = HAL_SD_DeInit(&hsd2);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Reset clock */
  __HAL_RCC_SDMMC2_CLK_DISABLE();
  return BSP_OK;
}

static void _bsp_sdio_set_power(t_sdio_power power)
{
  bool pwr_en;
  bool pwr_sel;

  /* Configure */
  switch (power)
  {
    case SD_ON:
      /* Normal     3V3: SD power switch */
      pwr_en  = true;
      pwr_sel = true;
      break;

    case SD_ON_LOW_POWER:
      /* Low power  1V8: VDDA */
      pwr_en  = false;
      pwr_sel = false;
      break;

    default:
      /* Attached to SD power switch OFF */
      pwr_en  = false;
      pwr_sel = true;
      break;
  }

  /* Apply */
  bsp_io_set_state(IO_PWR_SD_EN, pwr_en);
  bsp_io_set_state(IO_PWR_SD_SEL, pwr_sel);
}

/**
 * @brief This function handles SDMMC2 interrupt request.
 */
void SDMMC2_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd2);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: SDIO -->
*//*--------------------------------------------------------------------------*/
