/**
 *******************************************************************************
 * @file    n6cam_i2c.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam I2C API
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
#ifndef _N6CAM_I2C_H_
#define _N6CAM_I2C_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup I2C
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

/** Available I2Cs */
typedef enum
{
  /* Peripherals */
  I2C_1 = 0x00U,
  I2C_2,
  #if defined(N6CAM)
  I2C_4,
  #endif /* BOARD */
  I2C_NUM,
  /* Aliases */
  #if defined(N6CAM)
  I2C_SENSORS = I2C_1,
  I2C_CAMERA  = I2C_2,
  I2C_POWER   = I2C_2,
  #elif defined(STM32N6DK_C02)
  I2C_CAMERA  = I2C_1,
  #endif /* BOARD */
} t_i2c_id;

/** I2C status */
typedef enum
{
  I2C_STATUS_RX_CPLT = BIT(0U),
  I2C_STATUS_TX_CPLT = BIT(1U),
  I2C_STATUS_ABORT   = BIT(2U),
  I2C_STATUS_ERROR   = BIT(3U),
  I2C_STATUS_ALL     = BITMASK(4U)
} t_i2c_status;

/** I2C modes */
typedef enum
{
  I2C_MODE_BLOCK = 0x00U,
  I2C_MODE_IT,
  I2C_MODE_DMA,
} t_i2c_mode;

/**
 * @brief I2C IRQ callback
 * @param id     I2C ID
 * @param status Status flag
 */
typedef void (*t_i2c_irq_cb)(t_i2c_id id, t_i2c_status status);

/** I2C BSP instance */
typedef struct
{
  I2C_HandleTypeDef     hi2c;
  DMA_HandleTypeDef     hdmarx;
  DMA_HandleTypeDef     hdmatx;
  t_i2c_mode            mode;
  t_i2c_irq_cb          irq_cb;
} t_i2c_bsp;

/** I2C RTOS instance */
typedef struct
{
  TX_EVENT_FLAGS_GROUP  evt;
  TX_MUTEX              mtx;
} t_i2c_rtos;

/** I2C instance */
typedef struct
{
  bool                  ready;
  t_i2c_bsp             bsp;
  t_i2c_rtos            rtos;
} t_i2c;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/** I2Cs */
extern t_i2c i2c[I2C_NUM];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initialize I2C
 * @param id    I2C ID
 * @param speed I2C speed in Hz
 * @return Error code
 */
int32_t bsp_i2c_init(t_i2c_id id, uint32_t speed);

/**
 * @brief Deinitialize I2C
 * @param id    I2C ID
 * @return Error code
 */
int32_t bsp_i2c_deinit(t_i2c_id id);

/**
 * @brief Configure I2C mode
 * @param id    I2C ID
 * @param mode  I2C mode
 * @return Error code
 */
int32_t bsp_i2c_set_mode(t_i2c_id id, t_i2c_mode mode);

/**
 * @brief Acquire/release I2C bus
 * @param id      I2C ID
 * @param acquire True to acquire the lock, false to release it
 * @return Error code
 */
int32_t bsp_i2c_acquire(t_i2c_id id, bool acquire);

/**
 * @brief Check if device is ready
 * @param id      I2C ID
 * @param addr    Device address
 * @param trials  Number of trials
 * @param timeout Timeout in ms
 * @return Error code
 */
int32_t bsp_i2c_is_ready(t_i2c_id id, uint16_t addr, uint32_t trials, uint32_t timeout);

/**
 * @brief Read from 8-bit register
 * @param id      I2C ID
 * @param addr    Device address
 * @param reg     Register address
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Error code
 */
int32_t bsp_i2c_read_reg8(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout);

/**
 * @brief Write to 8-bit register
 * @param id      I2C ID
 * @param addr    Device address
 * @param reg     Register address
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Error code
 */
int32_t bsp_i2c_write_reg8(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout);

/**
 * @brief Read from 16-bit register
 * @param id      I2C ID
 * @param addr    Device address
 * @param reg     Register address
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Error code
 */
int32_t bsp_i2c_read_reg16(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout);

/**
 * @brief Write to 16-bit register
 * @param id      I2C ID
 * @param addr    Device address
 * @param reg     Register address
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Error code
 */
int32_t bsp_i2c_write_reg16(t_i2c_id id, uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size, uint32_t timeout);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: I2C -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_I2C_H_ */
