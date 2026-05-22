/**
 *******************************************************************************
 * @file    n6cam_spi.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam SPI API
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
#ifndef _N6CAM_SPI_H_
#define _N6CAM_SPI_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup SPI
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Available SPIs */
typedef enum
{
  /* Peripherals */
  #if defined(N6CAM)
  SPI_3 = 0x00U,
  SPI_4,
  SPI_5,
  #elif defined(STM32N6DK_C02)
  SPI_5 = 0x00U,
  #endif /* BOARD */
  SPI_NUM,
  /* Alias */
  #if defined(N6CAM)
  SPI_WIFI   = SPI_3,
  SPI_CAMERA = SPI_4,
  #endif /* BOARD */
} t_spi_id;

/** SPI status */
typedef enum
{
  SPI_STATUS_RX_CPLT = BIT(0U),
  SPI_STATUS_TX_CPLT = BIT(1U),
  SPI_STATUS_ERROR   = BIT(2U),
  SPI_STATUS_ALL     = BITMASK(3U),
} t_spi_status;

/** SPI modes */
typedef enum
{
  SPI_MODE_BLOCK = 0x00U,
  SPI_MODE_IT,
  SPI_MODE_DMA,
} t_spi_mode;

/**
 * @brief SPI IRQ callback
 * @param id     SPI ID
 * @param status Status flag
 */
typedef void (*t_spi_irq_cb)(t_spi_id id, t_spi_status status);

/** SPI BSP instance */
typedef struct
{
  SPI_HandleTypeDef     hspi;
  DMA_HandleTypeDef     hdmarx;
  DMA_HandleTypeDef     hdmatx;
  t_spi_mode            mode;
  t_spi_irq_cb          irq_cb;
} t_spi_bsp;

/** SPI RTOS instance */
typedef struct
{
  TX_EVENT_FLAGS_GROUP  evt;
  TX_MUTEX              mtx;
} t_spi_rtos;

/** SPI instance */
typedef struct
{
  bool                  ready;
  t_spi_bsp             bsp;
  t_spi_rtos            rtos;
} t_spi;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/** SPIs */
extern t_spi spi[SPI_NUM];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initialize SPI
 * @param id SPI ID
 * @return Error code
 */
int32_t bsp_spi_init(t_spi_id id);

/**
 * @brief Configure SPI mode
 * @param id    SPI ID
 * @param mode  SPI mode
 * @return Error code
 */
int32_t bsp_spi_set_mode(t_spi_id id, t_spi_mode mode);

/**
 * @brief Acquire/release SPI bus
 * @param id      SPI ID
 * @param acquire True to acquire the lock, false to release it
 * @return Error code
 */
int32_t bsp_spi_acquire(t_spi_id id, bool acquire);

/**
 * @brief Transfer data over SPI
 * @param id      SPI ID
 * @param rx_buff Receive buffer
 * @param tx_buff Transmit buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Error code
 */
int32_t bsp_spi_transfer(t_spi_id id, uint8_t* rx_buff, uint8_t* tx_buff, size_t size, uint32_t timeout);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: SPI -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_SPI_H_ */
