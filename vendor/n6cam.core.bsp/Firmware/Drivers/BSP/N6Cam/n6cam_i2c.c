/**
 *******************************************************************************
 * @file    n6cam_i2c.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam I2C API
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
#include "n6cam_i2c.h"
#include "n6cam_mcu.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup I2C
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

/* I2C defaults */
#define I2C_MODE_DEFAULT                I2C_MODE_DMA    /*!< I2C operation mode */
#define I2C_IT_PRIO                     5U              /*!< I2C interrupt priority */

/* I2C timing calculation parameters */
#define SEC2NSEC                        1000000000UL
#define I2C_VALID_TIMING_NBR            128U
#define I2C_ANALOG_FILTER_DELAY_MIN     50U   /* ns */
#define I2C_ANALOG_FILTER_DELAY_MAX     260U  /* ns */
#define I2C_USE_ANALOG_FILTER           1U
#define I2C_DIGITAL_FILTER_COEF         0U
#define I2C_PRESC_MAX                   16U
#define I2C_SCLDEL_MAX                  16U
#define I2C_SDADEL_MAX                  16U
#define I2C_SCLH_MAX                    256U
#define I2C_SCLL_MAX                    256U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** I2C speed IDs */
typedef enum
{
  I2C_SPEED_STANDARD = 0x00U,
  I2C_SPEED_FAST,
  I2C_SPEED_FAST_PLUS,
  I2C_SPEED_NUM,
} t_i2c_speed_id;

/** I2C timing parameters */
typedef struct
{
  uint32_t presc;      /* Timing prescaler */
  uint32_t tscldel;    /* SCL delay */
  uint32_t tsdadel;    /* SDA delay */
  uint32_t sclh;       /* SCL high period */
  uint32_t scll;       /* SCL low period */
} t_i2c_timing;

/** I2C speed parameters */
typedef struct
{
  uint32_t freq;       /* Frequency in Hz */
  uint32_t freq_min;   /* Minimum frequency in Hz */
  uint32_t freq_max;   /* Maximum frequency in Hz */
  uint32_t hddat_min;  /* Minimum data hold time in ns */
  uint32_t vddat_max;  /* Maximum data valid time in ns */
  uint32_t sudat_min;  /* Minimum data setup time in ns */
  uint32_t lscl_min;   /* Minimum low period of the SCL clock in ns */
  uint32_t hscl_min;   /* Minimum high period of SCL clock in ns */
  uint32_t trise;      /* Rise time in ns */
  uint32_t tfall;      /* Fall time in ns */
  uint32_t dnf;        /* Digital noise filter coefficient */
} t_i2c_speed;

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

/** I2C devices */
t_i2c                     i2c[I2C_NUM] = { 0 };

/** I2C RTOS naming */
static const char*        _i2c_name[I2C_NUM][2U] = {
  { "tx.evt.i2c1", "tx.mtx.i2c1" },
  { "tx.evt.i2c2", "tx.mtx.i2c2" },
  #if defined(N6CAM)
  { "tx.evt.i2c4", "tx.mtx.i2c4" },
  #endif /* BOARD */
};

static t_i2c_timing       _i2c_timing[I2C_VALID_TIMING_NBR];
static uint32_t           _i2c_timing_size = 0;
static const t_i2c_speed  _i2c_speed[I2C_SPEED_NUM] = {
  {
    .freq       = 100000,
    .freq_min   = 80000,
    .freq_max   = 120000,
    .hddat_min  = 0,
    .vddat_max  = 3450,
    .sudat_min  = 250,
    .lscl_min   = 4700,
    .hscl_min   = 4000,
    .trise      = 640,
    .tfall      = 20,
    .dnf        = I2C_DIGITAL_FILTER_COEF,
  },
  {
    .freq       = 400000,
    .freq_min   = 320000,
    .freq_max   = 480000,
    .hddat_min  = 0,
    .vddat_max  = 900,
    .sudat_min  = 100,
    .lscl_min   = 1300,
    .hscl_min   = 600,
    .trise      = 250,
    .tfall      = 100,
    .dnf        = I2C_DIGITAL_FILTER_COEF,
  },
  {
    .freq       = 1000000,
    .freq_min   = 800000,
    .freq_max   = 1200000,
    .hddat_min  = 0,
    .vddat_max  = 450,
    .sudat_min  = 50,
    .lscl_min   = 500,
    .hscl_min   = 260,
    .trise      = 60,
    .tfall      = 100,
    .dnf        = I2C_DIGITAL_FILTER_COEF,
  },
};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/* Internal */
static int32_t  _bsp_i2c_init_gpio(t_i2c_id id);
static int32_t  _bsp_i2c_init_peripheral(t_i2c_id id, uint32_t speed);
static int32_t  _bsp_i2c_deinit_gpio(t_i2c_id id);
static int32_t  _bsp_i2c_deinit_peripheral(t_i2c_id id);

/* RX/TX */
static int32_t  _bsp_i2c_read_regx(t_i2c_id id, uint16_t addr, uint16_t reg, uint16_t reg_size, uint8_t *buff, uint16_t size, uint32_t timeout);
static int32_t  _bsp_i2c_write_regx(t_i2c_id id, uint16_t addr, uint16_t reg, uint16_t reg_size, uint8_t *buff, uint16_t size, uint32_t timeout);

/* Timing */
static uint32_t _bsp_i2c_get_timing(uint32_t mcu_clk, uint32_t i2c_clk);
static uint32_t _bsp_i2c_compute_scll_sclh(uint32_t mcu_clk, uint32_t speed);
static void     _bsp_i2c_compute_presc_scldel_sdadel(uint32_t mcu_clk, uint32_t speed);

/* I2C1 */
extern void GPDMA1_Channel4_IRQHandler(void);
extern void GPDMA1_Channel5_IRQHandler(void);
extern void I2C1_EV_IRQHandler(void);
extern void I2C1_ER_IRQHandler(void);

/* I2C2 */
extern void GPDMA1_Channel6_IRQHandler(void);
extern void GPDMA1_Channel7_IRQHandler(void);
extern void I2C2_EV_IRQHandler(void);
extern void I2C2_ER_IRQHandler(void);

#if defined(N6CAM)
/* I2C4 */
extern void GPDMA1_Channel8_IRQHandler(void);
extern void GPDMA1_Channel9_IRQHandler(void);
extern void I2C3_EV_IRQHandler(void);
extern void I2C3_ER_IRQHandler(void);
#endif /* BOARD */

/* BSP handling */
extern void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c);
extern void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c);
extern void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *hi2c);
extern void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_i2c_init(t_i2c_id id, uint32_t speed)
{
  int32_t status;

  /* Validate */
  if (id >= I2C_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (i2c[id].ready)
  {
    return BSP_OK;
  }

  /* Init BSP */
  status = _bsp_i2c_init_gpio(id);
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_i2c_init_peripheral(id, speed);
  if (status != BSP_OK)
  {
    return status;
  }

  /* Init RTOS */
  status = tx_event_flags_create(&i2c[id].rtos.evt, (char*)_i2c_name[id][0U]);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_mutex_create(&i2c[id].rtos.mtx, (char*)_i2c_name[id][1U], TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  /* Set params */
  i2c[id].bsp.irq_cb  = NULL;
  i2c[id].bsp.mode    = I2C_MODE_DEFAULT;
  i2c[id].ready       = true;
  return BSP_OK;
}

int32_t bsp_i2c_deinit(t_i2c_id id)
{
  /* Validate */
  if (id >= I2C_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!i2c[id].ready)
  {
    return BSP_OK;
  }

  /* Wait for ongoing operations */
  rtos_mutex_acquire(&i2c[id].rtos.mtx, true);
  i2c[id].ready = false;

  /* Deinit BSP */
  _bsp_i2c_deinit_peripheral(id);
  _bsp_i2c_deinit_gpio(id);

  /* Deinit RTOS */
  tx_event_flags_delete(&i2c[id].rtos.evt);
  rtos_mutex_acquire(&i2c[id].rtos.mtx, false);
  tx_mutex_delete(&i2c[id].rtos.mtx);
  return BSP_OK;
}

int32_t bsp_i2c_set_mode(t_i2c_id id, t_i2c_mode mode)
{
  /* Validate */
  if (id >= I2C_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!i2c[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Configure */
  rtos_mutex_acquire(&i2c[id].rtos.mtx, true);
  i2c[id].bsp.mode = mode;
  rtos_mutex_acquire(&i2c[id].rtos.mtx, false);
  return BSP_OK;
}

int32_t bsp_i2c_acquire(t_i2c_id id, bool acquire)
{
  /* Validate */
  if (id >= I2C_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!i2c[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire */
  rtos_mutex_acquire(&i2c[id].rtos.mtx, acquire);
  return BSP_OK;
}

int32_t bsp_i2c_is_ready(t_i2c_id id, uint16_t addr, uint32_t trials, uint32_t timeout)
{
  int32_t status;
  /* Validate */
  if ((id >= I2C_NUM) || (trials == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!i2c[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire and check */
  rtos_mutex_acquire(&i2c[id].rtos.mtx, true);
  status = HAL_I2C_IsDeviceReady(&i2c[id].bsp.hi2c, addr, trials, timeout);
  rtos_mutex_acquire(&i2c[id].rtos.mtx, false);

  /* Return */
  if (status != HAL_OK)
  {
    return BSP_ERROR_BUSY;
  }
  return status;
}

int32_t bsp_i2c_read_reg8(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout)
{
  return _bsp_i2c_read_regx(id, addr, reg, I2C_MEMADD_SIZE_8BIT, buff, size, timeout);
}

int32_t bsp_i2c_write_reg8(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout)
{
  return _bsp_i2c_write_regx(id, addr, reg, I2C_MEMADD_SIZE_8BIT, buff, size, timeout);
}

int32_t bsp_i2c_read_reg16(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout)
{
  return _bsp_i2c_read_regx(id, addr, reg, I2C_MEMADD_SIZE_16BIT, buff, size, timeout);
}

int32_t bsp_i2c_write_reg16(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout)
{
  return _bsp_i2c_write_regx(id, addr, reg, I2C_MEMADD_SIZE_16BIT, buff, size, timeout);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-->> Internal <<-------------------*/
static int32_t _bsp_i2c_init_gpio(t_i2c_id id)
{
  GPIO_InitTypeDef gpio = {
    .Mode   = GPIO_MODE_AF_OD,
    .Pull   = GPIO_NOPULL,
    .Speed  = GPIO_SPEED_FREQ_MEDIUM,
  };

  /* Configure */
  switch (id)
  {
    case I2C_1:
      /* Start clocks */
      I2C1_SCL_CLK_ENABLE();
      I2C1_SDA_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = I2C1_SCL_PIN;
      gpio.Alternate = I2C1_SCL_AF;
      HAL_GPIO_Init(I2C1_SCL_PORT, &gpio);

      gpio.Pin       = I2C1_SDA_PIN;
      gpio.Alternate = I2C1_SDA_AF;
      HAL_GPIO_Init(I2C1_SDA_PORT, &gpio);
      break;

    case I2C_2:
      /* Start clocks */
      I2C2_SCL_CLK_ENABLE();
      I2C2_SDA_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = I2C2_SCL_PIN;
      gpio.Alternate = I2C2_SCL_AF;
      HAL_GPIO_Init(I2C2_SCL_PORT, &gpio);

      gpio.Pin       = I2C2_SDA_PIN;
      gpio.Alternate = I2C2_SDA_AF;
      HAL_GPIO_Init(I2C2_SDA_PORT, &gpio);
      break;

    #if defined(N6CAM)
    case I2C_4:
      /* Start clocks */
      I2C4_SCL_CLK_ENABLE();
      I2C4_SDA_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = I2C4_SCL_PIN;
      gpio.Alternate = I2C4_SCL_AF;
      HAL_GPIO_Init(I2C4_SCL_PORT, &gpio);

      gpio.Pin       = I2C4_SDA_PIN;
      gpio.Alternate = I2C4_SDA_AF;
      HAL_GPIO_Init(I2C4_SDA_PORT, &gpio);
      break;
    #endif /* BOARD */

    default:
      return BSP_ERROR_PARAMETER;
  }
  return BSP_OK;
}

static int32_t _bsp_i2c_init_peripheral(t_i2c_id id, uint32_t speed)
{
  int32_t   status;
  t_i2c_bsp *bsp;

  /* Get instance */
  bsp = &i2c[id].bsp;

  /* Set defaults */
  bsp->hi2c.Init.AddressingMode           = I2C_ADDRESSINGMODE_7BIT;
  bsp->hi2c.Init.DualAddressMode          = I2C_DUALADDRESS_DISABLE;
  bsp->hi2c.Init.OwnAddress1              = 0x00;
  bsp->hi2c.Init.OwnAddress2              = 0x00;
  bsp->hi2c.Init.OwnAddress2Masks         = I2C_OA2_NOMASK;
  bsp->hi2c.Init.NoStretchMode            = I2C_NOSTRETCH_DISABLE;
  bsp->hi2c.Init.GeneralCallMode          = I2C_GENERALCALL_DISABLE;

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
    case I2C_1:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_I2C1_CLK_ENABLE();
      __HAL_RCC_I2C1_FORCE_RESET();
      __HAL_RCC_I2C1_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel4_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel4_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel5_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel5_IRQn);
      HAL_NVIC_SetPriority(I2C1_EV_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (I2C1_EV_IRQn);
      HAL_NVIC_SetPriority(I2C1_ER_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (I2C1_ER_IRQn);

      /* Set specifics */
      bsp->hi2c.Instance        = I2C1;
      bsp->hi2c.Init.Timing     = _bsp_i2c_get_timing(HAL_RCC_GetPCLK1Freq(), speed);
      bsp->hdmarx.Instance      = GPDMA1_Channel4;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_I2C1_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel5;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_I2C1_TX;
      break;

    case I2C_2:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_I2C2_CLK_ENABLE();
      __HAL_RCC_I2C2_FORCE_RESET();
      __HAL_RCC_I2C2_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel6_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel6_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel7_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel7_IRQn);
      HAL_NVIC_SetPriority(I2C2_EV_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (I2C2_EV_IRQn);
      HAL_NVIC_SetPriority(I2C2_ER_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (I2C2_ER_IRQn);

      /* Set specifics */
      bsp->hi2c.Instance        = I2C2;
      bsp->hi2c.Init.Timing     = _bsp_i2c_get_timing(HAL_RCC_GetPCLK1Freq(), speed);
      bsp->hdmarx.Instance      = GPDMA1_Channel6;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_I2C2_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel7;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_I2C2_TX;
      break;

    #if defined(N6CAM)
    case I2C_4:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_I2C4_CLK_ENABLE();
      __HAL_RCC_I2C4_FORCE_RESET();
      __HAL_RCC_I2C4_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel8_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel8_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel9_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel9_IRQn);
      HAL_NVIC_SetPriority(I2C4_EV_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (I2C4_EV_IRQn);
      HAL_NVIC_SetPriority(I2C4_ER_IRQn, I2C_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (I2C4_ER_IRQn);

      /* Set specifics */
      bsp->hi2c.Instance        = I2C4;
      bsp->hi2c.Init.Timing     = _bsp_i2c_get_timing(HAL_RCC_GetPCLK1Freq(), speed);
      bsp->hdmarx.Instance      = GPDMA1_Channel8;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_I2C4_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel9;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_I2C4_TX;
      break;
    #endif /* BOARD */

    default:
      return BSP_ERROR_PARAMETER;
  }

  /* Init */
  /* > DMA - RX */
  status = HAL_DMA_Init(&bsp->hdmarx);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  __HAL_LINKDMA(&bsp->hi2c, hdmarx, bsp->hdmarx);
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
  __HAL_LINKDMA(&bsp->hi2c, hdmatx, bsp->hdmatx);
  status = HAL_DMA_ConfigChannelAttributes(&bsp->hdmatx, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* > Peripheral */
  status = HAL_I2C_Init(&bsp->hi2c);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_I2CEx_ConfigAnalogFilter(&bsp->hi2c, I2C_ANALOGFILTER_ENABLE);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_I2CEx_ConfigDigitalFilter(&bsp->hi2c, I2C_DIGITAL_FILTER_COEF);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_I2CEx_ConfigFastModePlus(&bsp->hi2c, I2C_FASTMODEPLUS_ENABLE);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

static int32_t _bsp_i2c_deinit_gpio(t_i2c_id id)
{
  /* Configure */
  switch (id)
  {
    case I2C_1:
      /* Disable specifics */
      HAL_GPIO_DeInit(I2C1_SCL_PORT, I2C1_SCL_PIN);
      HAL_GPIO_DeInit(I2C1_SDA_PORT, I2C1_SDA_PIN);
      break;

    case I2C_2:
      /* Disable specifics */
      HAL_GPIO_DeInit(I2C2_SCL_PORT, I2C2_SCL_PIN);
      HAL_GPIO_DeInit(I2C2_SDA_PORT, I2C2_SDA_PIN);
      break;

    #if defined(N6CAM)
    case I2C_4:
      /* Disable specifics */
      HAL_GPIO_DeInit(I2C4_SCL_PORT, I2C4_SCL_PIN);
      HAL_GPIO_DeInit(I2C4_SDA_PORT, I2C4_SDA_PIN);
      break;
    #endif /* BOARD */

    default:
      return BSP_ERROR_PARAMETER;
  }
  return BSP_OK;
}

static int32_t _bsp_i2c_deinit_peripheral(t_i2c_id id)
{
  t_i2c_bsp *bsp;

  /* Get instance */
  bsp = &i2c[id].bsp;

  /* Configure */
  switch (id)
  {
    case I2C_1:
      /* Reset clocks */
      __HAL_RCC_I2C1_CLK_DISABLE();

      /* Disable NVIC */
      HAL_NVIC_DisableIRQ(GPDMA1_Channel4_IRQn);
      HAL_NVIC_DisableIRQ(GPDMA1_Channel5_IRQn);
      HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
      HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
      break;

    case I2C_2:
      /* Reset clocks */
      __HAL_RCC_I2C2_CLK_DISABLE();

      /* Disable NVIC */
      HAL_NVIC_DisableIRQ(GPDMA1_Channel5_IRQn);
      HAL_NVIC_DisableIRQ(GPDMA1_Channel7_IRQn);
      HAL_NVIC_DisableIRQ(I2C2_ER_IRQn);
      HAL_NVIC_DisableIRQ(I2C2_EV_IRQn);
      break;

    #if defined(N6CAM)
    case I2C_4:
      /* Reset clocks */
      __HAL_RCC_I2C4_CLK_DISABLE();

      /* Disable NVIC */
      HAL_NVIC_DisableIRQ(GPDMA1_Channel8_IRQn);
      HAL_NVIC_DisableIRQ(GPDMA1_Channel9_IRQn);
      HAL_NVIC_DisableIRQ(I2C4_ER_IRQn);
      HAL_NVIC_DisableIRQ(I2C4_EV_IRQn);
      break;
    #endif /* BOARD */

    default:
      return BSP_ERROR_PARAMETER;
  }

  /* Deinit peripheral */
  HAL_DMA_DeInit(&bsp->hdmarx);
  HAL_DMA_DeInit(&bsp->hdmatx);
  HAL_I2C_DeInit(&bsp->hi2c);
  return BSP_OK;
}

/*-->> RX/TX <<----------------------*/
static int32_t _bsp_i2c_read_regx(t_i2c_id id, uint16_t addr, uint16_t reg, uint16_t reg_size, uint8_t *buff, uint16_t size, uint32_t timeout)
{
  uint32_t flags;
  int32_t  status;

  /* Validate */
  if (!buff || (id >= I2C_NUM) || (size == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!i2c[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire and clear events */
  rtos_mutex_acquire(&i2c[id].rtos.mtx, true);
  rtos_clear_event(&i2c[id].rtos.evt, I2C_STATUS_ALL);

  /* Handle cache */
  bsp_mcu_cache_clean_invalidate((uint32_t*)buff, size);

  /* Execute */
  switch (i2c[id].bsp.mode)
  {
    case I2C_MODE_DMA: status = HAL_I2C_Mem_Read_DMA(&i2c[id].bsp.hi2c, addr, reg, reg_size, buff, size); break;
    case I2C_MODE_IT:  status = HAL_I2C_Mem_Read_IT (&i2c[id].bsp.hi2c, addr, reg, reg_size, buff, size); break;
    default:
      /* Blocking mode, go directly to exit */
      status = HAL_I2C_Mem_Read(&i2c[id].bsp.hi2c, addr, reg, reg_size, buff, size, timeout);
      goto EXIT;
  }

  /* Handle start */
  if (status != HAL_OK)
  {
    goto EXIT;
  }

  /* Wait for event */
  WAIT:
  {
    status = tx_event_flags_get(&i2c[id].rtos.evt, I2C_STATUS_RX_CPLT | I2C_STATUS_ABORT | I2C_STATUS_ERROR, TX_OR_CLEAR, &flags, timeout);
    if (status == TX_NO_EVENTS)
    {
      /* On timeout, abort */
      HAL_I2C_Master_Abort_IT(&i2c[id].bsp.hi2c, addr);
      goto WAIT;
    }
  }

  /* Handle exit */
  EXIT:
  {
    /* Release and return */
    rtos_mutex_acquire(&i2c[id].rtos.mtx, false);
    if ((status == HAL_ERROR) || (flags & I2C_STATUS_ERROR))
    {
      return HAL_I2C_GetError(&i2c[id].bsp.hi2c);
    }
    else if ((status == HAL_TIMEOUT) || (flags & I2C_STATUS_ABORT))
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

static int32_t _bsp_i2c_write_regx(t_i2c_id id, uint16_t addr, uint16_t reg, uint16_t reg_size, uint8_t *buff, uint16_t size, uint32_t timeout)
{
  uint32_t flags;
  int32_t  status;

  /* Validate */
  if (!buff || (id >= I2C_NUM) || (size == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!i2c[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire and clear events */
  rtos_mutex_acquire(&i2c[id].rtos.mtx, true);
  rtos_clear_event(&i2c[id].rtos.evt, I2C_STATUS_ALL);

  /* Handle cache */
  bsp_mcu_cache_clean((uint32_t*)buff, size);

  /* Execute */
  switch (i2c[id].bsp.mode)
  {
    case I2C_MODE_DMA: status = HAL_I2C_Mem_Write_DMA(&i2c[id].bsp.hi2c, addr, reg, reg_size, buff, size); break;
    case I2C_MODE_IT:  status = HAL_I2C_Mem_Write_IT (&i2c[id].bsp.hi2c, addr, reg, reg_size, buff, size); break;
    default:
      /* Blocking mode, go directly to exit */
      status = HAL_I2C_Mem_Write(&i2c[id].bsp.hi2c, addr, reg, reg_size, buff, size, timeout);
      goto EXIT;
  }

  /* Handle start */
  if (status != HAL_OK)
  {
    goto EXIT;
  }

  /* Wait for event */
  WAIT:
  {
    status = tx_event_flags_get(&i2c[id].rtos.evt, I2C_STATUS_TX_CPLT | I2C_STATUS_ABORT | I2C_STATUS_ERROR, TX_OR_CLEAR, &flags, timeout);
    if (status == TX_NO_EVENTS)
    {
      /* On timeout, abort */
      HAL_I2C_Master_Abort_IT(&i2c[id].bsp.hi2c, addr);
      goto WAIT;
    }
  }

  /* Handle exit */
  EXIT:
  {
    /* Release and return */
    rtos_mutex_acquire(&i2c[id].rtos.mtx, false);
    if ((status == HAL_ERROR) || (flags & I2C_STATUS_ERROR))
    {
      return HAL_I2C_GetError(&i2c[id].bsp.hi2c);
    }
    else if ((status == HAL_TIMEOUT) || (flags & I2C_STATUS_ABORT))
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

/*-->> Timing <<---------------------*/
static uint32_t _bsp_i2c_get_timing(uint32_t mcu_clk, uint32_t i2c_clk)
{
  uint8_t   idx    = 0;
  uint32_t  timing = 0;

  /* Validate */
  if ((mcu_clk == 0U) && (i2c_clk == 0U))
  {
    return timing;
  }

  /* Compute */
  for (uint8_t speed = I2C_SPEED_STANDARD; speed <= I2C_SPEED_FAST_PLUS; speed++)
  {
    if ((i2c_clk >= _i2c_speed[speed].freq_min) && (i2c_clk <= _i2c_speed[speed].freq_max))
    {
      _bsp_i2c_compute_presc_scldel_sdadel(mcu_clk, speed);
      idx = _bsp_i2c_compute_scll_sclh(mcu_clk, speed);
      if (idx < I2C_VALID_TIMING_NBR)
      {
        timing = (
          ((_i2c_timing[idx].presc   & 0x0FU) << 28) |\
          ((_i2c_timing[idx].tscldel & 0x0FU) << 20) |\
          ((_i2c_timing[idx].tsdadel & 0x0FU) << 16) |\
          ((_i2c_timing[idx].sclh    & 0xFFU) <<  8) |\
          ((_i2c_timing[idx].scll    & 0xFFU) <<  0)
        );
      }
      break;
    }
  }
  return timing;
}

static uint32_t _bsp_i2c_compute_scll_sclh(uint32_t mcu_clk, uint32_t speed)
{
  uint32_t idx = 0xFFFFFFFFU;
  uint32_t ti2cclk;
  uint32_t ti2cspeed;
  uint32_t prev_error;
  uint32_t dnf_delay;
  uint32_t clk_min;
  uint32_t clk_max;
  uint32_t scll;
  uint32_t sclh;
  uint32_t tafdel_min;

  ti2cclk     = (SEC2NSEC + (mcu_clk / 2U)) / mcu_clk;
  ti2cspeed   = (SEC2NSEC + (_i2c_speed[speed].freq / 2U)) / _i2c_speed[speed].freq;
  tafdel_min  = I2C_ANALOG_FILTER_DELAY_MIN;

  /* tDNF = DNF x tI2CCLK */
  dnf_delay   = _i2c_speed[speed].dnf * ti2cclk;
  clk_max     = SEC2NSEC / _i2c_speed[speed].freq_min;
  clk_min     = SEC2NSEC / _i2c_speed[speed].freq_max;
  prev_error  = ti2cspeed;
  for (uint32_t count = 0; count < _i2c_timing_size; count++)
  {
    /* tPRESC = (PRESC+1) x tI2CCLK*/
    uint32_t tpresc = (_i2c_timing[count].presc + 1U) * ti2cclk;
    for (scll = 0; scll < I2C_SCLL_MAX; scll++)
    {
      /* tLOW(min) <= tAF(min) + tDNF + 2 x tI2CCLK + [(SCLL+1) x tPRESC ] */
      uint32_t tscl_l = tafdel_min + dnf_delay + (2U * ti2cclk) + ((scll + 1U) * tpresc);

      /* The I2CCLK period tI2CCLK must respect the following conditions:
       *    tI2CCLK < (tLOW - tfilters) / 4 and tI2CCLK < tHIGH
       */
      if ((tscl_l > _i2c_speed[speed].lscl_min) && (ti2cclk < ((tscl_l - tafdel_min - dnf_delay) / 4U)))
      {
        for (sclh = 0; sclh < I2C_SCLH_MAX; sclh++)
        {
          /* tHIGH(min) <= tAF(min) + tDNF + 2 x tI2CCLK + [(SCLH+1) x tPRESC] */
          uint32_t tscl_h = tafdel_min + dnf_delay + (2U * ti2cclk) + ((sclh + 1U) * tpresc);

          /* tSCL = tf + tLOW + tr + tHIGH */
          uint32_t tscl = tscl_l + tscl_h + _i2c_speed[speed].trise + _i2c_speed[speed].tfall;
          if ((tscl >= clk_min) && (tscl <= clk_max) && (tscl_h >= _i2c_speed[speed].hscl_min) && (ti2cclk < tscl_h))
          {
            int32_t error = (int32_t)tscl - (int32_t)ti2cspeed;
            if (error < 0)
            {
              error = -error;
            }
            /* Look for the timings with the lowest clock error */
            if ((uint32_t)error < prev_error)
            {
              prev_error = (uint32_t)error;
              _i2c_timing[count].scll = scll;
              _i2c_timing[count].sclh = sclh;
              idx = count;
            }
          }
        }
      }
    }
  }
  return idx;
}

static void _bsp_i2c_compute_presc_scldel_sdadel(uint32_t mcu_clk, uint32_t speed)
{
  uint32_t prev_presc = I2C_PRESC_MAX;
  uint32_t ti2cclk;
  int32_t  tsdadel_min;
  int32_t  tsdadel_max;
  int32_t  tscldel_min;
  uint32_t presc;
  uint32_t scldel;
  uint32_t sdadel;
  uint32_t tafdel_min;
  uint32_t tafdel_max;

  ti2cclk    = (SEC2NSEC + (mcu_clk / 2U))/ mcu_clk;
  tafdel_min = I2C_ANALOG_FILTER_DELAY_MIN;
  tafdel_max = I2C_ANALOG_FILTER_DELAY_MAX;

  /* tDNF = DNF x tI2CCLK
   * tPRESC = (PRESC+1) x tI2CCLK
   * SDADEL >= {tf +tHD;DAT(min) - tAF(min) - tDNF - [3 x tI2CCLK]} / tPRESC
   * SDADEL <= {tVD;DAT(max) - tr - tAF(max) - tDNF- [4 x tI2CCLK]} / tPRESC
   */
  tsdadel_min = (int32_t)_i2c_speed[speed].tfall + (int32_t)_i2c_speed[speed].hddat_min -
    (int32_t)tafdel_min - (int32_t)(((int32_t)_i2c_speed[speed].dnf + 3) * (int32_t)ti2cclk);

  tsdadel_max = (int32_t)_i2c_speed[speed].vddat_max - (int32_t)_i2c_speed[speed].trise -
    (int32_t)tafdel_max - (int32_t)(((int32_t)_i2c_speed[speed].dnf + 4) * (int32_t)ti2cclk);

  /* {[tr+ tSU;DAT(min)] / [tPRESC]} - 1 <= SCLDEL */
  tscldel_min = (int32_t)_i2c_speed[speed].trise + (int32_t)_i2c_speed[speed].sudat_min;

  if (tsdadel_min <= 0)
  {
    tsdadel_min = 0;
  }
  if (tsdadel_max <= 0)
  {
    tsdadel_max = 0;
  }
  for (presc = 0; presc < I2C_PRESC_MAX; presc++)
  {
    for (scldel = 0; scldel < I2C_SCLDEL_MAX; scldel++)
    {
      /* TSCLDEL = (SCLDEL+1) * (PRESC+1) * TI2CCLK */
      uint32_t tscldel = (scldel + 1U) * (presc + 1U) * ti2cclk;
      if (tscldel >= (uint32_t)tscldel_min)
      {
        for (sdadel = 0; sdadel < I2C_SDADEL_MAX; sdadel++)
        {
          /* TSDADEL = SDADEL * (PRESC+1) * TI2CCLK */
          uint32_t tsdadel = (sdadel * (presc + 1U)) * ti2cclk;
          if ((tsdadel >= (uint32_t)tsdadel_min) && (tsdadel <= (uint32_t)tsdadel_max))
          {
            if(presc != prev_presc)
            {
              prev_presc                            = presc;
              _i2c_timing[_i2c_timing_size].presc   = presc;
              _i2c_timing[_i2c_timing_size].tscldel = scldel;
              _i2c_timing[_i2c_timing_size].tsdadel = sdadel;
              _i2c_timing_size ++;
              if(_i2c_timing_size >= I2C_VALID_TIMING_NBR)
              {
                return;
              }
            }
          }
        }
      }
    }
  }
}

/*-->> I2C1 <<-----------------------*/
void GPDMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2c[I2C_1].bsp.hdmarx);
}

void GPDMA1_Channel5_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2c[I2C_1].bsp.hdmatx);
}

void I2C1_EV_IRQHandler(void)
{
  HAL_I2C_EV_IRQHandler(&i2c[I2C_1].bsp.hi2c);
}

void I2C1_ER_IRQHandler(void)
{
  HAL_I2C_ER_IRQHandler(&i2c[I2C_1].bsp.hi2c);
}

/*-->> I2C2 <<-----------------------*/
void GPDMA1_Channel6_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2c[I2C_2].bsp.hdmarx);
}

void GPDMA1_Channel7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2c[I2C_2].bsp.hdmatx);
}

void I2C2_EV_IRQHandler(void)
{
  HAL_I2C_EV_IRQHandler(&i2c[I2C_2].bsp.hi2c);
}

void I2C2_ER_IRQHandler(void)
{
  HAL_I2C_ER_IRQHandler(&i2c[I2C_2].bsp.hi2c);
}

#if defined(N6CAM)
/*-->> I2C4 <<-----------------------*/
void GPDMA1_Channel8_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2c[I2C_4].bsp.hdmarx);
}

void GPDMA1_Channel9_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&i2c[I2C_4].bsp.hdmatx);
}

void I2C4_EV_IRQHandler(void)
{
  HAL_I2C_EV_IRQHandler(&i2c[I2C_4].bsp.hi2c);
}

void I2C4_ER_IRQHandler(void)
{
  HAL_I2C_ER_IRQHandler(&i2c[I2C_4].bsp.hi2c);
}
#endif /* BOARD */

/*-->> BSP handling <<---------------*/
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < I2C_NUM; idx++)
  {
    /* Validate */
    if (!i2c[idx].ready || (&i2c[idx].bsp.hi2c != hi2c))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&i2c[idx].rtos.evt, I2C_STATUS_RX_CPLT);
    if (i2c[idx].bsp.irq_cb)
    {
      i2c[idx].bsp.irq_cb(idx, I2C_STATUS_RX_CPLT);
    }
    return;
  }
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < I2C_NUM; idx++)
  {
    /* Validate */
    if (!i2c[idx].ready || (&i2c[idx].bsp.hi2c != hi2c))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&i2c[idx].rtos.evt, I2C_STATUS_TX_CPLT);
    if (i2c[idx].bsp.irq_cb)
    {
      i2c[idx].bsp.irq_cb(idx, I2C_STATUS_TX_CPLT);
    }
    return;
  }
}

extern void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *hi2c)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < I2C_NUM; idx++)
  {
    /* Validate */
    if (!i2c[idx].ready || (&i2c[idx].bsp.hi2c != hi2c))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&i2c[idx].rtos.evt, I2C_STATUS_ABORT);
    if (i2c[idx].bsp.irq_cb)
    {
      i2c[idx].bsp.irq_cb(idx, I2C_STATUS_ABORT);
    }
    return;
  }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < I2C_NUM; idx++)
  {
    /* Validate */
    if (!i2c[idx].ready || (&i2c[idx].bsp.hi2c != hi2c))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&i2c[idx].rtos.evt, I2C_STATUS_ERROR);
    if (i2c[idx].bsp.irq_cb)
    {
      i2c[idx].bsp.irq_cb(idx, I2C_STATUS_ERROR);
    }
    return;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: I2C -->
*//*--------------------------------------------------------------------------*/
