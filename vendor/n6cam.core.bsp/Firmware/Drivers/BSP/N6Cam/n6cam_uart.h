/**
 *******************************************************************************
 * @file    n6cam_uart.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam UART API
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
#ifndef _N6CAM_UART_H_
#define _N6CAM_UART_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup UART
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

/** Available UARTs */
typedef enum
{
  /* Peripherals */
  UART_1 = 0x00U,
  UART_2,
  UART_NUM,
  /* Alias */
  UART_STLINK = UART_1,
#if defined(N6CAM_WIFI_MURATA)
  UART_WIFI   = UART_2,
#else
  UART_APP    = UART_2,
#endif /* WIFI */
} t_uart_id;

/** UART status */
typedef enum
{
  UART_STATUS_RX_CPLT = BIT(0U),
  UART_STATUS_TX_CPLT = BIT(1U),
  UART_STATUS_ERROR   = BIT(2U),
  UART_STATUS_ALL     = BITMASK(3U),
} t_uart_status;

/** UART modes */
typedef enum
{
  UART_MODE_BLOCK = 0x00U,
  UART_MODE_IT,
  UART_MODE_DMA,
} t_uart_mode;

/**
 * @brief UART IRQ callback
 * @param id      UART ID
 * @param status  Status flag
 */
typedef void (*t_uart_irq_cb)(t_uart_id id, t_uart_status status);

/** UART BSP instance */
typedef struct
{
  UART_HandleTypeDef    huart;
  DMA_HandleTypeDef     hdmarx;
  DMA_HandleTypeDef     hdmatx;
  t_uart_mode           mode_rx;
  t_uart_mode           mode_tx;
  t_uart_irq_cb         irq_cb;
} t_uart_bsp;

/** UART RTOS instance */
typedef struct
{
  TX_EVENT_FLAGS_GROUP  evt;
  TX_MUTEX              mtx_rx;
  TX_MUTEX              mtx_tx;
} t_uart_rtos;

/** UART instance */
typedef struct
{
  bool                  ready;
  t_uart_bsp            bsp;
  t_uart_rtos           rtos;
  t_stream              stream;
} t_uart;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/** UARTs */
extern t_uart uart[UART_NUM];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initialize UART
 * @param id    UART ID
 * @param baud  Baudrate in bps
 * @param swap  If true, swap RX/TX pins
 * @return Error code
 */
int32_t bsp_uart_init(t_uart_id id, uint32_t baud, bool swap);

/**
 * @brief Configure UART RX/TX modes
 * @param id      UART ID
 * @param mode_rx RX mode
 * @param mode_tx TX mode
 * @return Error code
 */
int32_t bsp_uart_set_mode(t_uart_id id, t_uart_mode mode_rx, t_uart_mode mode_tx);

/**
 * @brief Get the UART stream
 * @param id      UART ID
 * @return Stream instance
 */
t_stream *bsp_uart_get_stream(t_uart_id id);

/**
 * @brief Read from UART
 * @param id      UART ID
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Bytes read or error code
 */
int32_t bsp_uart_read(t_uart_id id, uint8_t* buff, size_t size, uint32_t timeout);

/**
 * @brief Write to UART
 * @param id      UART ID
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Bytes written or error code
 */
int32_t bsp_uart_write(t_uart_id id, const uint8_t* buff, size_t size, uint32_t timeout);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: UART -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_UART_H_ */
