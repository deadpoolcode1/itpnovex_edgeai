/**
 *******************************************************************************
 * @file    n6cam_audio.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam AUDIO API
 *
 * @todo    This module is incomplete. Minimal implementation was done for testing
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
#include "n6cam_audio.h"
#include "n6cam_io.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup AUDIO
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

#define AUDIO_IT_PRIO   7U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

#define MDF_DECIMATION(__FREQUENCY__) \
    ((__FREQUENCY__) == (AUDIO_FREQUENCY_8K ))  ? (64U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_11K))  ? (64U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_16K))  ? (32U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_22K))  ? (32U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_32K))  ? (16U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_44K))  ? (16U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_48K))  ? (16U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_96K))  ? (8U) : (4U)

#define MDF_GAIN(__FREQUENCY__) \
    ((__FREQUENCY__) == (AUDIO_FREQUENCY_8K ))  ? (-4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_11K))  ? (-6) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_16K))  ? (2)  \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_22K))  ? (2)  \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_32K))  ? (10) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_44K))  ? (10) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_48K))  ? (10) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_96K))  ? (18) : (24)

#define MDF_CIC_MODE(__FREQUENCY__) \
    ((__FREQUENCY__) == (AUDIO_FREQUENCY_8K ))  ? (MDF_ONE_FILTER_SINC4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_11K))  ? (MDF_ONE_FILTER_SINC4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_16K))  ? (MDF_ONE_FILTER_SINC4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_22K))  ? (MDF_ONE_FILTER_SINC4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_32K))  ? (MDF_ONE_FILTER_SINC4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_44K))  ? (MDF_ONE_FILTER_SINC4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_48K))  ? (MDF_ONE_FILTER_SINC4) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_96K))  ? (MDF_ONE_FILTER_SINC4) : (MDF_ONE_FILTER_SINC4)

#define MDF_MCU_CLK_DIV(__FREQUENCY__) \
    ((__FREQUENCY__) == (AUDIO_FREQUENCY_8K ))  ? (2U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_11K))  ? (1U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_16K))  ? (2U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_22K))  ? (1U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_32K))  ? (2U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_44K))  ? (1U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_48K))  ? (2U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_96K))  ? (2U) : (1U)

#define MDF_OUT_CLK_DIV(__FREQUENCY__) \
    ((__FREQUENCY__) == (AUDIO_FREQUENCY_8K ))  ? (12U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_11K))  ? (4U)  \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_16K))  ? (12U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_22K))  ? (4U)  \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_32K))  ? (12U) \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_44K))  ? (4U)  \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_48K))  ? (8U)  \
  : ((__FREQUENCY__) == (AUDIO_FREQUENCY_96K))  ? (16U) : (16U)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

typedef struct
{
  int32_t   gain;
  uint32_t  cic_mode;
  uint32_t  decimation;
  uint32_t  mcu_clk_div;
  uint32_t  out_clk_div;
} t_mic_config;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/* Internal */
static int32_t _bsp_audio_config_clocks(void);
static int32_t _bsp_audio_mic_init_gpio(void);
static int32_t _bsp_audio_mic_deinit_gpio(void);
static int32_t _bsp_audio_mic_init_peripheral(void);
static int32_t _bsp_audio_mic_deinit_peripheral(void);

/* IT Handlers */
extern void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf);
extern void HAL_MDF_AcqHalfCpltCallback(MDF_HandleTypeDef *hmdf);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static uint32_t           _audio_freq = AUDIO_FREQUENCY_16K;

static bool               _mic_ready  = false;
static t_mic_config       _mic_config = { 0 };
MDF_HandleTypeDef         _mic_hmdf   = { 0 };
MDF_FilterConfigTypeDef   _mic_filter = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_audio_mic_init(uint32_t freq)
{
  int32_t status;

  /* Validate */
  if (_mic_ready)
  {
    return BSP_OK;
  }
  _audio_freq = freq;

  /* Initialize */
  status = _bsp_audio_config_clocks();
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_audio_mic_init_gpio();
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_audio_mic_init_peripheral();
  if (status != BSP_OK)
  {
    return status;
  }

  /* Set params */
  _mic_ready = true;
  return status;
}

int32_t bsp_audio_mic_deinit(void)
{
  int32_t status;

  /* Validate */
  if (!_mic_ready)
  {
    return BSP_OK;
  }

  /* Deinit */
  status = _bsp_audio_mic_deinit_peripheral();
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_audio_mic_deinit_gpio();
  if (status != BSP_OK)
  {
    return status;
  }

  /* Set params */
  _mic_ready = false;
  return status;
}

int32_t bsp_audio_mic_start(void)
{
  int32_t status;

  /* Validate */
  if (!_mic_ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Start MDF */
  status = HAL_MDF_AcqStart_IT(&_mic_hmdf, &_mic_filter);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

int32_t bsp_audio_mic_stop(void)
{
  int32_t status;

  /* Validate */
  if (!_mic_ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Stop MDF */
  status = HAL_MDF_AcqStop_IT(&_mic_hmdf);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

WEAK void bsp_audio_mic_on_sample(int32_t sample)
{
  UNUSED(sample);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-->> Internal <<-------------------*/
static int32_t _bsp_audio_config_clocks(void)
{
  RCC_OscInitTypeDef        osc = {};
  RCC_PeriphCLKInitTypeDef  per = {};
  int32_t                   status;

  /* Configure PLL4 */
  osc.PLL4.PLLSource     = RCC_PLLSOURCE_HSE;
  osc.PLL4.PLLState      = RCC_PLL_ON;
  osc.PLL4.PLLFractional = 0;
  osc.PLL4.PLLM          = 6U;
  /* Start: 48MHz / 6 = 8MHz */
  switch (_audio_freq)
  {
    case AUDIO_FREQUENCY_44K:
    case AUDIO_FREQUENCY_22K:
    case AUDIO_FREQUENCY_11K:
      /* (8 * 192) / (4 * 2) = 192MHz */
      osc.PLL4.PLLN  = 192U;
      osc.PLL4.PLLP1 = 4U;
      osc.PLL4.PLLP2 = 2U;
      break;

    case AUDIO_FREQUENCY_96K:
      /* (8 * 172) / (2 * 1) = 688MHz */
      osc.PLL4.PLLN  = 172U;
      osc.PLL4.PLLP1 = 2U;
      osc.PLL4.PLLP2 = 1U;
      break;

    default:
      /* (8 * 172) / (7 * 4) = 49.142MHz */
      osc.PLL4.PLLN  = 172U;
      osc.PLL4.PLLP1 = 7U;
      osc.PLL4.PLLP2 = 4U;
      break;
  }
  status = HAL_RCC_OscConfig(&osc);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Configure MDF1 */
  per.PeriphClockSelection                = RCC_PERIPHCLK_MDF1;
  per.Mdf1ClockSelection                  = RCC_MDF1CLKSOURCE_IC8;
  per.ICSelection[RCC_IC8].ClockSelection = RCC_ICCLKSOURCE_PLL4;
  switch (_audio_freq)
  {
    case AUDIO_FREQUENCY_11K:
    case AUDIO_FREQUENCY_22K:
    case AUDIO_FREQUENCY_44K:
      /* (192 / 17) = 11.294MHz */
      per.ICSelection[RCC_IC8].ClockDivider = 17;
      break;

    case AUDIO_FREQUENCY_96K:
      /* (688 / 7)  = 98.285MHz */
      per.ICSelection[RCC_IC8].ClockDivider = 7;
      break;

    default:
      /* (49.142 / 1) = 49.142MHz */
      per.ICSelection[RCC_IC8].ClockDivider = 1;
      break;
  }
  status = HAL_RCCEx_PeriphCLKConfig(&per);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

static int32_t _bsp_audio_mic_init_gpio(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* Enable clocks */
  AUDIO_MIC_CLK_CLK_ENABLE();
  AUDIO_MIC_D1_CLK_ENABLE();

  /* Init GPIO */
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  gpio.Pin       = AUDIO_MIC_CLK_PIN;
  gpio.Alternate = AUDIO_MIC_CLK_AF;
  HAL_GPIO_Init(AUDIO_MIC_CLK_PORT, &gpio);
  gpio.Pin       = AUDIO_MIC_D1_PIN;
  gpio.Alternate = AUDIO_MIC_D1_AF;
  HAL_GPIO_Init(AUDIO_MIC_D1_PORT, &gpio);

  return BSP_OK;
}

static int32_t _bsp_audio_mic_deinit_gpio(void)
{
  /* Deinit GPIO */
  HAL_GPIO_DeInit(AUDIO_MIC_CLK_PORT, AUDIO_MIC_CLK_PIN);
  HAL_GPIO_DeInit(AUDIO_MIC_D1_PORT , AUDIO_MIC_D1_PIN);

  return BSP_OK;
}

static int32_t _bsp_audio_mic_init_peripheral(void)
{
  int32_t                     status;

  /* Enable clock */
  __HAL_RCC_MDF1_CLK_ENABLE();

  /* Prepare */
  _mic_config.gain        = MDF_GAIN(_audio_freq);
  _mic_config.cic_mode    = MDF_CIC_MODE(_audio_freq);
  _mic_config.decimation  = MDF_DECIMATION(_audio_freq);
  _mic_config.mcu_clk_div = MDF_MCU_CLK_DIV(_audio_freq);
  _mic_config.out_clk_div = MDF_OUT_CLK_DIV(_audio_freq);

  /* Configure MDF */
  _mic_hmdf.Instance                                        = MDF1_Filter0;
  _mic_hmdf.Init.FilterBistream                             = MDF_BITSTREAM0_FALLING;
  _mic_hmdf.Init.CommonParam.ProcClockDivider               = _mic_config.mcu_clk_div;
  _mic_hmdf.Init.CommonParam.OutputClock.Activation         = ENABLE;
  _mic_hmdf.Init.CommonParam.OutputClock.Divider            = _mic_config.out_clk_div;
  _mic_hmdf.Init.CommonParam.OutputClock.Pins               = MDF_OUTPUT_CLOCK_0;
  _mic_hmdf.Init.CommonParam.OutputClock.Trigger.Activation = DISABLE ;
  _mic_hmdf.Init.SerialInterface.Activation                 = ENABLE;
  _mic_hmdf.Init.SerialInterface.ClockSource                = MDF_SITF_CCK0_SOURCE;
  _mic_hmdf.Init.SerialInterface.Mode                       = MDF_SITF_NORMAL_SPI_MODE;
  _mic_hmdf.Init.SerialInterface.Threshold                  = 31U;

  /* Configure MDF filter */
  _mic_filter.DataSource                      = MDF_DATA_SOURCE_BSMX;
  _mic_filter.Delay                           = 0U;
  _mic_filter.Offset                          = 0U;
  _mic_filter.Gain                            = _mic_config.gain;
  _mic_filter.CicMode                         = _mic_config.cic_mode;
  _mic_filter.DecimationRatio                 = _mic_config.decimation;
  _mic_filter.AcquisitionMode                 = MDF_MODE_ASYNC_CONT;
  _mic_filter.FifoThreshold                   = MDF_FIFO_THRESHOLD_NOT_EMPTY;
  _mic_filter.DiscardSamples                  = 0U;
  _mic_filter.Integrator.Activation           = DISABLE;
  _mic_filter.ReshapeFilter.Activation        = ENABLE;
  _mic_filter.ReshapeFilter.DecimationRatio   = MDF_RSF_DECIMATION_RATIO_4;
  _mic_filter.HighPassFilter.Activation       = ENABLE;
  _mic_filter.HighPassFilter.CutOffFrequency  = MDF_HPF_CUTOFF_0_000625FPCM;

  /* Start MDF */
  status = HAL_MDF_Init(&_mic_hmdf);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Start NVIC */
  HAL_NVIC_SetPriority(MDF1_FLT0_IRQn, AUDIO_IT_PRIO, 0);
  HAL_NVIC_EnableIRQ  (MDF1_FLT0_IRQn);

  return BSP_OK;
}

static int32_t _bsp_audio_mic_deinit_peripheral(void)
{
  int32_t status;

  /* Disable interrupts */
  HAL_NVIC_DisableIRQ(MDF1_FLT0_IRQn);

  /* Deinit peripheral */
  status = HAL_MDF_DeInit(&_mic_hmdf);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Disable clocks */
  __HAL_RCC_MDF1_CLK_DISABLE();

  return BSP_OK;
}

/*-->> IT Handlers <<----------------*/
void MDF1_FLT0_IRQHandler(void)
{
  HAL_MDF_IRQHandler(&_mic_hmdf);
}

void HAL_MDF_AcqCpltCallback(MDF_HandleTypeDef *hmdf)
{
  int32_t sample;
  HAL_MDF_GetAcqValue(hmdf, &sample);
  bsp_audio_mic_on_sample(sample);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: AUDIO -->
*//*--------------------------------------------------------------------------*/
