/**
 *******************************************************************************
 * @file    n6cam_spi.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam SPI BSP
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
#include "n6cam_mcu.h"
#include "n6cam_spi.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup SPI
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

/* SPI defaults */
#define SPI_MODE_DEFAULT    SPI_MODE_DMA    /*!< SPI operation mode */
#define SPI_IT_PRIO         5U              /*!< SPI interrupt priority */

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

/** SPI devices */
t_spi               spi[SPI_NUM] = { 0 };

/** SPI RTOS naming */
static const char*  _spi_name[SPI_NUM][2U] = {
  #if defined(N6CAM)
  {"tx.evt.spi3", "tx.mtx.spi3"},
  {"tx.evt.spi4", "tx.mtx.spi4"},
  #endif /* BOARD */
  {"tx.evt.spi5", "tx.mtx.spi5"},
};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/* Internal */
static int32_t _bsp_spi_init_gpio(t_spi_id id);
static int32_t _bsp_spi_init_peripheral(t_spi_id id);

/* BSP handling */
#if defined(N6CAM)
extern void GPDMA1_Channel10_IRQHandler(void);
extern void GPDMA1_Channel11_IRQHandler(void);
extern void SPI3_IRQHandler(void);
extern void GPDMA1_Channel12_IRQHandler(void);
extern void GPDMA1_Channel13_IRQHandler(void);
extern void SPI4_IRQHandler(void);
#endif /* BOARD */
extern void GPDMA1_Channel14_IRQHandler(void);
extern void GPDMA1_Channel15_IRQHandler(void);
extern void SPI5_IRQHandler(void);
extern void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi);
extern void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);
extern void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi);
extern void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_spi_init(t_spi_id id)
{
  int32_t status;

  /* Validate */
  if (id >= SPI_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (spi[id].ready)
  {
    return BSP_OK;
  }

  /* Init BSP */
  status = _bsp_spi_init_gpio(id);
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_spi_init_peripheral(id);
  if (status != BSP_OK)
  {
    return status;
  }

  /* Init RTOS */
  status = tx_event_flags_create(&spi[id].rtos.evt, (char*)_spi_name[id][0U]);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_mutex_create(&spi[id].rtos.mtx, (char*)_spi_name[id][1U], TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  /* Set params */
  spi[id].bsp.irq_cb = NULL;
  spi[id].bsp.mode   = SPI_MODE_DEFAULT;
  spi[id].ready      = true;
  return BSP_OK;
}

int32_t bsp_spi_set_mode(t_spi_id id, t_spi_mode mode)
{
  /* Validate */
  if (id >= SPI_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!spi[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Configure */
  rtos_mutex_acquire(&spi[id].rtos.mtx, true);
  spi[id].bsp.mode = mode;
  rtos_mutex_acquire(&spi[id].rtos.mtx, false);
  return BSP_OK;
}

int32_t bsp_spi_acquire(t_spi_id id, bool acquire)
{
  /* Validate */
  if (id >= SPI_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!spi[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire */
  rtos_mutex_acquire(&spi[id].rtos.mtx, acquire);
  return BSP_OK;
}

int32_t bsp_spi_transfer(t_spi_id id, uint8_t* rx_buff, uint8_t* tx_buff, size_t size, uint32_t timeout)
{
  uint32_t flags;
  int32_t  status;

  /* Validate */
  if ((!rx_buff && !tx_buff) || (id >= SPI_NUM) || (size == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!spi[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire and clear events */
  rtos_mutex_acquire(&spi[id].rtos.mtx, true);
  rtos_clear_event(&spi[id].rtos.evt, SPI_STATUS_ALL);

  /* Handle cache */
  if (rx_buff)
  {
    bsp_mcu_cache_clean_invalidate((uint32_t*)rx_buff, size);
  }
  if (tx_buff)
  {
    bsp_mcu_cache_clean((uint32_t*)tx_buff, size);
  }

  /* Execute */
  if (rx_buff && tx_buff)
  {
    switch (spi[id].bsp.mode)
    {
      case SPI_MODE_DMA: status = HAL_SPI_TransmitReceive_DMA(&spi[id].bsp.hspi, tx_buff, rx_buff, size); break;
      case SPI_MODE_IT:  status = HAL_SPI_TransmitReceive_IT (&spi[id].bsp.hspi, tx_buff, rx_buff, size); break;
      default:
        /* Blocking mode, go directly to exit */
        status = HAL_SPI_TransmitReceive(&spi[id].bsp.hspi, tx_buff, rx_buff, size, timeout);
        goto EXIT;
    }
  }
  else if (rx_buff)
  {
    switch (spi[id].bsp.mode)
    {
      case SPI_MODE_DMA: status = HAL_SPI_Receive_DMA(&spi[id].bsp.hspi, rx_buff, size); break;
      case SPI_MODE_IT:  status = HAL_SPI_Receive_IT (&spi[id].bsp.hspi, rx_buff, size); break;
      default:
        /* Blocking mode, go directly to exit */
        status = HAL_SPI_Receive(&spi[id].bsp.hspi, rx_buff, size, timeout);
        goto EXIT;
    }
  }
  else if (tx_buff)
  {
    switch (spi[id].bsp.mode)
    {
      case SPI_MODE_DMA: status = HAL_SPI_Transmit_DMA(&spi[id].bsp.hspi, tx_buff, size); break;
      case SPI_MODE_IT:  status = HAL_SPI_Transmit_IT (&spi[id].bsp.hspi, tx_buff, size); break;
      default:
        /* Blocking mode, go directly to exit */
        status = HAL_SPI_Transmit(&spi[id].bsp.hspi, tx_buff, size, timeout);
        goto EXIT;
    }
  }
  else
  {
    /* Should not happen */
    return BSP_ERROR_PARAMETER;
  }

  /* Handle start */
  if (status != HAL_OK)
  {
    goto EXIT;
  }

  /* Wait for event */
  status = tx_event_flags_get(&spi[id].rtos.evt, SPI_STATUS_ALL, TX_OR_CLEAR, &flags, timeout);
  if (status == TX_NO_EVENTS)
  {
    /* On timeout, abort */
    HAL_SPI_Abort(&spi[id].bsp.hspi);
  }

  /* Handle exit */
  EXIT:
  {
    /* Release and return */
    rtos_mutex_acquire(&spi[id].rtos.mtx, false);
    if ((status == HAL_ERROR) || (flags & SPI_STATUS_ERROR))
    {
      return BSP_ERROR_PERIPHERAL;
    }
    else if ((status == HAL_TIMEOUT) || (status == TX_NO_EVENTS))
    {
      return BSP_ERROR_TIMEOUT;
    }
    else if ((status == HAL_OK) || (status == TX_SUCCESS))
    {
      return BSP_OK;
    }
    return BSP_ERROR_UNKNOWN;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-->> Internal <<-------------------*/
static int32_t _bsp_spi_init_gpio(t_spi_id id)
{
  GPIO_InitTypeDef gpio = {
    .Mode   = GPIO_MODE_AF_PP,
    .Pull   = GPIO_NOPULL,
    .Speed  = GPIO_SPEED_FREQ_VERY_HIGH,
  };

  /* Configure */
  switch (id)
  {
    #if defined(N6CAM)
    case SPI_3:
      /* Start clocks */
      SPI3_CLK_CLK_ENABLE();
      SPI3_MISO_CLK_ENABLE();
      SPI3_MOSI_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = SPI3_CLK_PIN;
      gpio.Alternate = SPI3_CLK_AF;
      HAL_GPIO_Init(SPI3_CLK_PORT, &gpio);

      gpio.Pin       = SPI3_MISO_PIN;
      gpio.Alternate = SPI3_MISO_AF;
      HAL_GPIO_Init(SPI3_MISO_PORT, &gpio);

      gpio.Pin       = SPI3_MOSI_PIN;
      gpio.Alternate = SPI3_MOSI_AF;
      HAL_GPIO_Init(SPI3_MOSI_PORT, &gpio);
      break;

    case SPI_4:
      /* Start clocks */
      SPI4_CLK_CLK_ENABLE();
      SPI4_MISO_CLK_ENABLE();
      SPI4_MOSI_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = SPI4_CLK_PIN;
      gpio.Alternate = SPI4_CLK_AF;
      HAL_GPIO_Init(SPI4_CLK_PORT, &gpio);

      gpio.Pin       = SPI4_MISO_PIN;
      gpio.Alternate = SPI4_MISO_AF;
      HAL_GPIO_Init(SPI4_MISO_PORT, &gpio);

      gpio.Pin       = SPI4_MOSI_PIN;
      gpio.Alternate = SPI4_MOSI_AF;
      HAL_GPIO_Init(SPI4_MOSI_PORT, &gpio);
      break;
    #endif /* BOARD */

    case SPI_5:
      /* Start clocks */
      SPI5_CLK_CLK_ENABLE();
      SPI5_MISO_CLK_ENABLE();
      SPI5_MOSI_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = SPI5_CLK_PIN;
      gpio.Alternate = SPI5_CLK_AF;
      HAL_GPIO_Init(SPI5_CLK_PORT, &gpio);

      gpio.Pin       = SPI5_MISO_PIN;
      gpio.Alternate = SPI5_MISO_AF;
      HAL_GPIO_Init(SPI5_MISO_PORT, &gpio);

      gpio.Pin       = SPI5_MOSI_PIN;
      gpio.Alternate = SPI5_MOSI_AF;
      HAL_GPIO_Init(SPI5_MOSI_PORT, &gpio);
      break;

    default:
      return BSP_ERROR_PARAMETER;
  }
  return BSP_OK;
}

static int32_t _bsp_spi_init_peripheral(t_spi_id id)
{
  DMA_DataHandlingConfTypeDef config = { 0 };
  int32_t                     status;
  t_spi_bsp                   *bsp;

  /* Get instance */
  bsp = &spi[id].bsp;

  /* Set defaults */
  /* > Peripheral */
  bsp->hspi.Init.Mode                     = SPI_MODE_MASTER;
  bsp->hspi.Init.Direction                = SPI_DIRECTION_2LINES;
  bsp->hspi.Init.DataSize                 = SPI_DATASIZE_8BIT;
  bsp->hspi.Init.CLKPolarity              = SPI_POLARITY_LOW;
  bsp->hspi.Init.CLKPhase                 = SPI_PHASE_1EDGE;
  bsp->hspi.Init.NSS                      = SPI_NSS_SOFT;
  bsp->hspi.Init.BaudRatePrescaler        = SPI_BAUDRATEPRESCALER_2;
  bsp->hspi.Init.FirstBit                 = SPI_FIRSTBIT_MSB;
  bsp->hspi.Init.TIMode                   = SPI_TIMODE_DISABLE;
  bsp->hspi.Init.CRCCalculation           = SPI_CRCCALCULATION_DISABLE;
  bsp->hspi.Init.CRCPolynomial            = 0x7;
  bsp->hspi.Init.NSSPMode                 = SPI_NSS_PULSE_ENABLE;
  bsp->hspi.Init.NSSPolarity              = SPI_NSS_POLARITY_LOW;
  bsp->hspi.Init.FifoThreshold            = SPI_FIFO_THRESHOLD_01DATA;
  bsp->hspi.Init.MasterSSIdleness         = SPI_MASTER_SS_IDLENESS_00CYCLE;
  bsp->hspi.Init.MasterInterDataIdleness  = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  bsp->hspi.Init.MasterReceiverAutoSusp   = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  bsp->hspi.Init.MasterKeepIOState        = SPI_MASTER_KEEP_IO_STATE_ENABLE;
  bsp->hspi.Init.IOSwap                   = SPI_IO_SWAP_DISABLE;
  bsp->hspi.Init.ReadyMasterManagement    = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  bsp->hspi.Init.ReadyPolarity            = SPI_RDY_POLARITY_HIGH;

  /* > DMA - RX */
  bsp->hdmarx.Init.BlkHWRequest           = DMA_BREQ_SINGLE_BURST;
  bsp->hdmarx.Init.Direction              = DMA_PERIPH_TO_MEMORY;
  bsp->hdmarx.Init.SrcInc                 = DMA_SINC_FIXED;
  bsp->hdmarx.Init.DestInc                = DMA_DINC_INCREMENTED;
  bsp->hdmarx.Init.SrcDataWidth           = DMA_SRC_DATAWIDTH_BYTE;
  bsp->hdmarx.Init.DestDataWidth          = DMA_DEST_DATAWIDTH_BYTE;
  bsp->hdmarx.Init.Priority               = DMA_LOW_PRIORITY_HIGH_WEIGHT;
  bsp->hdmarx.Init.SrcBurstLength         = 1;
  bsp->hdmarx.Init.DestBurstLength        = 1;
  bsp->hdmarx.Init.TransferAllocatedPort  = (DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0);
  bsp->hdmarx.Init.TransferEventMode      = DMA_TCEM_BLOCK_TRANSFER;
  bsp->hdmarx.Init.Mode                   = DMA_NORMAL;

  /* > DMA - TX */
  bsp->hdmatx.Init.BlkHWRequest           = DMA_BREQ_SINGLE_BURST;
  bsp->hdmatx.Init.Direction              = DMA_MEMORY_TO_PERIPH;
  bsp->hdmatx.Init.SrcInc                 = DMA_SINC_INCREMENTED;
  bsp->hdmatx.Init.DestInc                = DMA_DINC_FIXED;
  bsp->hdmatx.Init.SrcDataWidth           = DMA_SRC_DATAWIDTH_BYTE;
  bsp->hdmatx.Init.DestDataWidth          = DMA_DEST_DATAWIDTH_BYTE;
  bsp->hdmatx.Init.Priority               = DMA_LOW_PRIORITY_HIGH_WEIGHT;
  bsp->hdmatx.Init.SrcBurstLength         = 1;
  bsp->hdmatx.Init.DestBurstLength        = 1;
  bsp->hdmatx.Init.TransferAllocatedPort  = (DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0);
  bsp->hdmatx.Init.TransferEventMode      = DMA_TCEM_BLOCK_TRANSFER;
  bsp->hdmatx.Init.Mode                   = DMA_NORMAL;

  /* Configure */
  switch (id)
  {
    #if defined(N6CAM)
    case SPI_3:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_SPI3_CLK_ENABLE();
      __HAL_RCC_SPI3_FORCE_RESET();
      __HAL_RCC_SPI3_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel10_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel10_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel11_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel11_IRQn);
      HAL_NVIC_SetPriority(SPI3_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ(SPI3_IRQn);

      /* Set specifics */
      bsp->hspi.Instance        = SPI3;
      bsp->hdmarx.Instance      = GPDMA1_Channel10;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_SPI3_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel11;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_SPI3_TX;
      break;

    case SPI_4:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_SPI4_CLK_ENABLE();
      __HAL_RCC_SPI4_FORCE_RESET();
      __HAL_RCC_SPI4_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel12_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel12_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel13_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel13_IRQn);
      HAL_NVIC_SetPriority(SPI4_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ(SPI4_IRQn);

      /* Set specifics */
      bsp->hspi.Instance        = SPI4;
      bsp->hdmarx.Instance      = GPDMA1_Channel12;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_SPI4_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel13;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_SPI4_TX;
      break;
    #endif /* BOARD */

    case SPI_5:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_SPI5_CLK_ENABLE();
      __HAL_RCC_SPI5_FORCE_RESET();
      __HAL_RCC_SPI5_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel14_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel14_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel15_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel15_IRQn);
      HAL_NVIC_SetPriority(SPI5_IRQn, SPI_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ(SPI5_IRQn);

      /* Set specifics */
      bsp->hspi.Instance        = SPI5;
      bsp->hdmarx.Instance      = GPDMA1_Channel14;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_SPI5_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel15;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_SPI5_TX;
      break;

    default:
      return BSP_ERROR_NOT_SUPPORTED;
  }

  /* Init */
  /* > DMA - RX */
  status = HAL_DMA_Init(&bsp->hdmarx);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  config.DataExchange   = DMA_EXCHANGE_NONE;
  config.DataAlignment  = DMA_DATA_RIGHTALIGN_ZEROPADDED;
  status = HAL_DMAEx_ConfigDataHandling(&bsp->hdmarx, &config);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  __HAL_LINKDMA(&bsp->hspi, hdmarx, bsp->hdmarx);
  status = HAL_DMA_ConfigChannelAttributes(&bsp->hdmarx, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* > DMA - TX */
  status = HAL_DMA_Init(&bsp->hdmatx);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  config.DataExchange   = DMA_EXCHANGE_NONE;
  config.DataAlignment  = DMA_DATA_RIGHTALIGN_ZEROPADDED;
  status = HAL_DMAEx_ConfigDataHandling(&bsp->hdmatx, &config);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  __HAL_LINKDMA(&bsp->hspi, hdmatx, bsp->hdmatx);
  status = HAL_DMA_ConfigChannelAttributes(&bsp->hdmatx, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* > Peripheral */
  status = HAL_SPI_Init(&bsp->hspi);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

/*-->> BSP handling <<---------------*/
#if defined(N6CAM)
void GPDMA1_Channel10_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&spi[SPI_3].bsp.hdmarx);
}

void GPDMA1_Channel11_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&spi[SPI_3].bsp.hdmatx);
}

void SPI3_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&spi[SPI_3].bsp.hspi);
}

void GPDMA1_Channel12_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&spi[SPI_4].bsp.hdmarx);
}

void GPDMA1_Channel13_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&spi[SPI_4].bsp.hdmatx);
}

void SPI4_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&spi[SPI_4].bsp.hspi);
}
#endif /* BOARD */

void GPDMA1_Channel14_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&spi[SPI_5].bsp.hdmarx);
}

void GPDMA1_Channel15_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&spi[SPI_5].bsp.hdmatx);
}

void SPI5_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&spi[SPI_5].bsp.hspi);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  /* Find instance */
  for (size_t idx = 0U; idx < SPI_NUM; idx++)
  {
    /* Validate */
    if (!spi[idx].ready || (&spi[idx].bsp.hspi != hspi))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&spi[idx].rtos.evt, SPI_STATUS_RX_CPLT);
    if (spi[idx].bsp.irq_cb)
    {
      spi[idx].bsp.irq_cb(idx, SPI_STATUS_RX_CPLT);
    }
    return;
  }
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  /* Find instance */
  for (size_t idx = 0U; idx < SPI_NUM; idx++)
  {
    /* Validate */
    if (!spi[idx].ready || (&spi[idx].bsp.hspi != hspi))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&spi[idx].rtos.evt, SPI_STATUS_TX_CPLT);
    if (spi[idx].bsp.irq_cb)
    {
      spi[idx].bsp.irq_cb(idx, SPI_STATUS_TX_CPLT);
    }
    return;
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  /* Find instance */
  for (size_t idx = 0U; idx < SPI_NUM; idx++)
  {
    /* Validate */
    if (!spi[idx].ready || (&spi[idx].bsp.hspi != hspi))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&spi[idx].rtos.evt, SPI_STATUS_RX_CPLT | SPI_STATUS_TX_CPLT);
    if (spi[idx].bsp.irq_cb)
    {
      spi[idx].bsp.irq_cb(idx, SPI_STATUS_RX_CPLT | SPI_STATUS_TX_CPLT);
    }
    return;
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  /* Find instance */
  for (size_t idx = 0U; idx < SPI_NUM; idx++)
  {
    /* Validate */
    if (!spi[idx].ready || (&spi[idx].bsp.hspi != hspi))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&spi[idx].rtos.evt, SPI_STATUS_ERROR);
    if (spi[idx].bsp.irq_cb)
    {
      spi[idx].bsp.irq_cb(idx, SPI_STATUS_ERROR);
    }
    return;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: SPI -->
*//*--------------------------------------------------------------------------*/
