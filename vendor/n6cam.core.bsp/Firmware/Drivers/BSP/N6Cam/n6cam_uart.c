/**
 *******************************************************************************
 * @file    n6cam_uart.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam UART BSP
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
#include "n6cam_uart.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup UART
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

/* UART defaults */
#define UART_MODE_DEFAULT     UART_MODE_DMA   /*!< UART operation mode */
#define UART_IT_PRIO          5U              /*!< UART interrupt priority */

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

/** UART devices */
t_uart                  uart[UART_NUM] = { 0 };

/** UART RTOS naming */
static const char*      _uart_name[UART_NUM][3U] = {
  {"tx.evt.uart1", "tx.mtx.uart1.rx", "tx.mtx.uart1.tx"},
  {"tx.evt.uart2", "tx.mtx.uart2.rx", "tx.mtx.uart2.tx"},
};

/** AUX for RX complete */
static volatile size_t  _uart_recv[UART_NUM];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/* Internal */
static int32_t _bsp_uart_init_gpio(t_uart_id id);
static int32_t _bsp_uart_init_peripheral(t_uart_id id, uint32_t baud, bool swap);

/* Stream */
static int32_t _bsp_uart1_read(uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _bsp_uart1_write(const uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _bsp_uart2_read(uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _bsp_uart2_write(const uint8_t *buff, size_t size, uint32_t timeout);

/* BSP handling */
extern void GPDMA1_Channel0_IRQHandler(void);
extern void GPDMA1_Channel1_IRQHandler(void);
extern void USART1_IRQHandler(void);
extern void GPDMA1_Channel2_IRQHandler(void);
extern void GPDMA1_Channel3_IRQHandler(void);
extern void USART2_IRQHandler(void);
extern void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size);
extern void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
extern void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_uart_init(t_uart_id id, uint32_t baud, bool swap)
{
  int32_t status;

  /* Validate */
  if (id >= UART_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (uart[id].ready)
  {
    return BSP_OK;
  }

  /* Init BSP */
  status = _bsp_uart_init_gpio(id);
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_uart_init_peripheral(id, baud, swap);
  if (status != BSP_OK)
  {
    return status;
  }

  /* Init RTOS */
  status = tx_event_flags_create(&uart[id].rtos.evt, (char*)_uart_name[id][0U]);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_mutex_create(&uart[id].rtos.mtx_rx, (char*)_uart_name[id][1U], TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_mutex_create(&uart[id].rtos.mtx_tx, (char*)_uart_name[id][2U], TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  /* Configure streams */
  switch (id)
  {
    case UART_1: status = stream_init(&uart[id].stream, _bsp_uart1_read, _bsp_uart1_write); break;
    case UART_2: status = stream_init(&uart[id].stream, _bsp_uart2_read, _bsp_uart2_write); break;
    default:     return BSP_ERROR_PARAMETER;
  }

  /* Set params */
  uart[id].bsp.irq_cb  = NULL;
  uart[id].bsp.mode_rx = UART_MODE_DEFAULT;
  uart[id].bsp.mode_tx = UART_MODE_DEFAULT;
  uart[id].ready       = true;
  return BSP_OK;
}

int32_t bsp_uart_set_mode(t_uart_id id, t_uart_mode mode_rx, t_uart_mode mode_tx)
{
  /* Validate */
  if (id >= UART_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!uart[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Configure */
  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, true);
  uart[id].bsp.mode_rx = mode_rx;
  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, false);
  rtos_mutex_acquire(&uart[id].rtos.mtx_tx, true);
  uart[id].bsp.mode_tx = mode_tx;
  rtos_mutex_acquire(&uart[id].rtos.mtx_tx, false);
  return BSP_OK;
}

t_stream *bsp_uart_get_stream(t_uart_id id)
{
  /* Validate */
  if ((id >= UART_NUM) || !uart[id].ready)
  {
    return NULL;
  }
  return &uart[id].stream;
}

int32_t bsp_uart_read(t_uart_id id, uint8_t* buff, size_t size, uint32_t timeout)
{
  uint32_t flags;
  int32_t  status;

  /* Validate */
  if (!buff || (id >= UART_NUM) || (size == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!uart[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire and clear events */
  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, true);
  rtos_clear_event(&uart[id].rtos.evt, UART_STATUS_ALL);

  /* Handle cache */
  bsp_mcu_cache_clean_invalidate((uint32_t*)buff, size);

  /* Execute */
  switch (uart[id].bsp.mode_rx)
  {
    case UART_MODE_DMA: status = HAL_UARTEx_ReceiveToIdle_DMA(&uart[id].bsp.huart, buff, size); break;
    case UART_MODE_IT:  status = HAL_UARTEx_ReceiveToIdle_IT (&uart[id].bsp.huart, buff, size); break;
    default:
      /* Blocking mode, go directly to exit */
      status = HAL_UARTEx_ReceiveToIdle(&uart[id].bsp.huart, buff, size, (uint16_t*)&_uart_recv[id], timeout);
      goto EXIT;
  }

  /* Handle start */
  if (status != HAL_OK)
  {
    goto EXIT;
  }

  /* Wait for event */
  status = tx_event_flags_get(&uart[id].rtos.evt, UART_STATUS_RX_CPLT | UART_STATUS_ERROR, TX_OR_CLEAR, &flags, timeout);
  if (status == TX_NO_EVENTS)
  {
    /* On timeout, abort */
    HAL_UART_AbortReceive(&uart[id].bsp.huart);
  }

  /* Handle exit */
  EXIT:
  {
    /* Release and return */
    rtos_mutex_acquire(&uart[id].rtos.mtx_rx, false);
    if ((status == HAL_ERROR) || (flags & UART_STATUS_ERROR))
    {
      return BSP_ERROR_PERIPHERAL;
    }
    else if ((status == HAL_TIMEOUT) || (status == TX_NO_EVENTS))
    {
      return BSP_ERROR_TIMEOUT;
    }
    else if ((status == HAL_OK) || (status == TX_SUCCESS))
    {
      return (int32_t)_uart_recv[id];
    }
    return BSP_ERROR_UNKNOWN;
  }
}

int32_t bsp_uart_write(t_uart_id id, const uint8_t* buff, size_t size, uint32_t timeout)
{
  uint32_t flags;
  int32_t  status;

  /* Validate */
  if (!buff || (id >= UART_NUM) || (size == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!uart[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire and clear events */
  rtos_mutex_acquire(&uart[id].rtos.mtx_tx, true);
  rtos_clear_event(&uart[id].rtos.evt, UART_STATUS_ALL);

  /* Handle cache */
  bsp_mcu_cache_clean((uint32_t*)buff, size);

  /* Execute */
  switch (uart[id].bsp.mode_tx)
  {
    case UART_MODE_DMA: status = HAL_UART_Transmit_DMA(&uart[id].bsp.huart, buff, size); break;
    case UART_MODE_IT:  status = HAL_UART_Transmit_IT (&uart[id].bsp.huart, buff, size); break;
    default:
      /* Blocking mode, go directly to exit */
      status = HAL_UART_Transmit(&uart[id].bsp.huart, buff, size, timeout);
      goto EXIT;
  }

  /* Handle start */
  if (status != HAL_OK)
  {
    goto EXIT;
  }

  /* Wait for event */
  status = tx_event_flags_get(&uart[id].rtos.evt, UART_STATUS_TX_CPLT | UART_STATUS_ERROR, TX_OR_CLEAR, &flags, timeout);
  if (status == TX_NO_EVENTS)
  {
    /* On timeout, abort */
    HAL_UART_AbortReceive(&uart[id].bsp.huart);
  }

  /* Handle exit */
  EXIT:
  {
    /* Release and return */
    rtos_mutex_acquire(&uart[id].rtos.mtx_tx, false);
    if ((status == HAL_ERROR) || (flags & UART_STATUS_ERROR))
    {
      return BSP_ERROR_PERIPHERAL;
    }
    else if ((status == HAL_TIMEOUT) || (status == TX_NO_EVENTS))
    {
      return BSP_ERROR_TIMEOUT;
    }
    else if ((status == HAL_OK) || (status == TX_SUCCESS))
    {
      return size;
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
static int32_t _bsp_uart_init_gpio(t_uart_id id)
{
  GPIO_InitTypeDef gpio = {
    .Mode   = GPIO_MODE_AF_PP,
    .Pull   = GPIO_PULLUP,
    .Speed  = GPIO_SPEED_FREQ_MEDIUM,
  };

  /* Configure */
  switch (id)
  {
    case UART_1:
      /* Start clocks */
      USART1_RX_CLK_ENABLE();
      USART1_TX_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = USART1_RX_PIN;
      gpio.Alternate = USART1_RX_AF;
      HAL_GPIO_Init(USART1_RX_PORT, &gpio);

      gpio.Pin       = USART1_TX_PIN;
      gpio.Alternate = USART1_TX_AF;
      HAL_GPIO_Init(USART1_TX_PORT, &gpio);
      break;

    case UART_2:
      /* Start clocks */
      USART2_RX_CLK_ENABLE();
      USART2_TX_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = USART2_RX_PIN;
      gpio.Alternate = USART2_RX_AF;
      HAL_GPIO_Init(USART2_RX_PORT, &gpio);

      gpio.Pin       = USART2_TX_PIN;
      gpio.Alternate = USART2_TX_AF;
      HAL_GPIO_Init(USART2_TX_PORT, &gpio);
      break;

    default:
      return BSP_ERROR_PARAMETER;
  }
  return BSP_OK;
}

static int32_t _bsp_uart_init_peripheral(t_uart_id id, uint32_t baud, bool swap)
{
  int32_t     status;
  t_uart_bsp  *bsp;

  /* Get instance */
  bsp = &uart[id].bsp;

  /* Set defaults */
  /* > Peripheral */
  bsp->huart.Init.BaudRate                = baud;
  bsp->huart.Init.Mode                    = UART_MODE_TX_RX;
  bsp->huart.Init.Parity                  = UART_PARITY_NONE;
  bsp->huart.Init.WordLength              = UART_WORDLENGTH_8B;
  bsp->huart.Init.StopBits                = UART_STOPBITS_1;
  bsp->huart.Init.HwFlowCtl               = UART_HWCONTROL_NONE;
  bsp->huart.Init.OverSampling            = UART_OVERSAMPLING_8;

  /* > Advanced options */
  if (swap)
  {
    bsp->huart.AdvancedInit.AdvFeatureInit|= UART_ADVFEATURE_SWAP_INIT;
    bsp->huart.AdvancedInit.Swap           = UART_ADVFEATURE_SWAP_ENABLE;
  }

  /* > DMA - RX */
  bsp->hdmarx.Init.BlkHWRequest           = DMA_BREQ_SINGLE_BURST;
  bsp->hdmarx.Init.Direction              = DMA_PERIPH_TO_MEMORY;
  bsp->hdmarx.Init.SrcInc                 = DMA_SINC_FIXED;
  bsp->hdmarx.Init.DestInc                = DMA_DINC_INCREMENTED;
  bsp->hdmarx.Init.SrcDataWidth           = DMA_SRC_DATAWIDTH_BYTE;
  bsp->hdmarx.Init.DestDataWidth          = DMA_DEST_DATAWIDTH_BYTE;
  bsp->hdmarx.Init.Priority               = DMA_HIGH_PRIORITY;
  bsp->hdmarx.Init.SrcBurstLength         = 1;
  bsp->hdmarx.Init.DestBurstLength        = 1;
  bsp->hdmarx.Init.TransferAllocatedPort  = (DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1);
  bsp->hdmarx.Init.TransferEventMode      = DMA_TCEM_BLOCK_TRANSFER;
  bsp->hdmarx.Init.Mode                   = DMA_NORMAL;

  /* > DMA -TX */
  bsp->hdmatx.Init.BlkHWRequest           = DMA_BREQ_SINGLE_BURST;
  bsp->hdmatx.Init.Direction              = DMA_MEMORY_TO_PERIPH;
  bsp->hdmatx.Init.SrcInc                 = DMA_SINC_INCREMENTED;
  bsp->hdmatx.Init.DestInc                = DMA_DINC_FIXED;
  bsp->hdmatx.Init.SrcDataWidth           = DMA_SRC_DATAWIDTH_BYTE;
  bsp->hdmatx.Init.DestDataWidth          = DMA_DEST_DATAWIDTH_BYTE;
  bsp->hdmatx.Init.Priority               = DMA_HIGH_PRIORITY;
  bsp->hdmatx.Init.SrcBurstLength         = 1;
  bsp->hdmatx.Init.DestBurstLength        = 1;
  bsp->hdmatx.Init.TransferAllocatedPort  = (DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1);
  bsp->hdmatx.Init.TransferEventMode      = DMA_TCEM_BLOCK_TRANSFER;
  bsp->hdmatx.Init.Mode                   = DMA_NORMAL;

  /* Configure */
  switch (id)
  {
    case UART_1:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_USART1_CLK_ENABLE();
      __HAL_RCC_USART1_FORCE_RESET();
      __HAL_RCC_USART1_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel0_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel1_IRQn);
      HAL_NVIC_SetPriority(USART1_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (USART1_IRQn);

      /* Set specifics */
      bsp->huart.Instance       = USART1;
      bsp->hdmarx.Instance      = GPDMA1_Channel0;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_USART1_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel1;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_USART1_TX;
      break;

    case UART_2:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_USART2_CLK_ENABLE();
      __HAL_RCC_USART2_FORCE_RESET();
      __HAL_RCC_USART2_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel2_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel2_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel3_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel3_IRQn);
      HAL_NVIC_SetPriority(USART2_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (USART2_IRQn);

      /* Set specifics */
      bsp->huart.Instance       = USART2;
      bsp->hdmarx.Instance      = GPDMA1_Channel2;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_USART2_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel3;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_USART2_TX;
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
  __HAL_LINKDMA(&bsp->huart, hdmarx, bsp->hdmarx);
  status = HAL_DMA_ConfigChannelAttributes(&bsp->hdmarx, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* DMA -TX */
  status = HAL_DMA_Init(&bsp->hdmatx);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  __HAL_LINKDMA(&bsp->huart, hdmatx, bsp->hdmatx);
  status = HAL_DMA_ConfigChannelAttributes(&bsp->hdmatx, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* > Peripheral */
  status = HAL_UART_Init(&bsp->huart);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_UARTEx_SetTxFifoThreshold(&bsp->huart, UART_TXFIFO_THRESHOLD_1_8);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_UARTEx_SetRxFifoThreshold(&bsp->huart, UART_RXFIFO_THRESHOLD_1_8);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_UARTEx_DisableFifoMode(&bsp->huart);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

/*-->> Stream <<---------------------*/
static int32_t _bsp_uart1_read(uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_read(UART_1, buff, size, timeout);
}

static int32_t _bsp_uart1_write(const uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_write(UART_1, buff, size, timeout);
}

static int32_t _bsp_uart2_read(uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_read(UART_2, buff, size, timeout);
}

static int32_t _bsp_uart2_write(const uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_write(UART_2, buff, size, timeout);
}

/*-->> BSP handling <<---------------*/
void GPDMA1_Channel0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_1].bsp.hdmarx);
}

void GPDMA1_Channel1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_1].bsp.hdmatx);
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart[UART_1].bsp.huart);
}

void GPDMA1_Channel2_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_2].bsp.hdmarx);
}

void GPDMA1_Channel3_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_2].bsp.hdmatx);
}

void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart[UART_2].bsp.huart);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < UART_NUM; idx++)
  {
    /* Validate */
    if (!uart[idx].ready || (&uart[idx].bsp.huart != huart))
    {
      continue;
    }
    /* Handle internal */
    _uart_recv[idx] = size;
    rtos_raise_event(&uart[idx].rtos.evt, UART_STATUS_RX_CPLT);
    if (uart[idx].bsp.irq_cb)
    {
      uart[idx].bsp.irq_cb(idx, UART_STATUS_RX_CPLT);
    }
    return;
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < UART_NUM; idx++)
  {
    /* Validate */
    if (!uart[idx].ready || (&uart[idx].bsp.huart != huart))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&uart[idx].rtos.evt, UART_STATUS_TX_CPLT);
    if (uart[idx].bsp.irq_cb)
    {
      uart[idx].bsp.irq_cb(idx, UART_STATUS_TX_CPLT);
    }
    return;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < UART_NUM; idx++)
  {
    /* Validate */
    if (!uart[idx].ready || (&uart[idx].bsp.huart != huart))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&uart[idx].rtos.evt, UART_STATUS_ERROR);
    if (uart[idx].bsp.irq_cb)
    {
      uart[idx].bsp.irq_cb(idx, UART_STATUS_ERROR);
    }
    return;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: UART -->
*//*--------------------------------------------------------------------------*/
