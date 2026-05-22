/**
 *******************************************************************************
 * @file    n6cam_io.h
 * @author  SIANA Systems
 * @date    05/2025
 * @brief   N6Cam IO Definitions
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
#ifndef _N6CAM_IO_H_
#define _N6CAM_IO_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "stm32_hal.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup IO
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* CORE: BTN/HW/LEDs ----------------*/
/* Button definitions */
#if defined(N6CAM)
#define BTN_USER1_PORT                  GPIOH
#define BTN_USER1_PIN                   GPIO_PIN_4
#define BTN_USER1_CLK_ENABLE()          __HAL_RCC_GPIOH_CLK_ENABLE()
#define BTN_USER1_CLK_DISABLE()         __HAL_RCC_GPIOH_CLK_DISABLE()
#define BTN_USER1_EXTI_IRQn             EXTI4_IRQn
#define BTN_USER1_EXTI_LINE             EXTI_LINE_4
#elif defined(STM32N6DK_C02)
#define BTN_USER1_PORT                  GPIOC
#define BTN_USER1_PIN                   GPIO_PIN_13
#define BTN_USER1_CLK_ENABLE()          __HAL_RCC_GPIOC_CLK_ENABLE()
#define BTN_USER1_CLK_DISABLE()         __HAL_RCC_GPIOC_CLK_DISABLE()
#define BTN_USER1_EXTI_IRQn             EXTI13_IRQn
#define BTN_USER1_EXTI_LINE             EXTI_LINE_13
#endif /* BOARD */

/* HW definitions */
#if defined(N6CAM)
#define HW_ID1_PORT                     GPIOA
#define HW_ID1_PIN                      GPIO_PIN_0
#define HW_ID1_CLK_ENABLE()             __HAL_RCC_GPIOA_CLK_ENABLE()
#define HW_ID1_CLK_DISABLE()            __HAL_RCC_GPIOA_CLK_DISABLE()

#define HW_ID2_PORT                     GPIOF
#define HW_ID2_PIN                      GPIO_PIN_0
#define HW_ID2_CLK_ENABLE()             __HAL_RCC_GPIOF_CLK_ENABLE()
#define HW_ID2_CLK_DISABLE()            __HAL_RCC_GPIOF_CLK_DISABLE()

#define HW_ID3_PORT                     GPIOH
#define HW_ID3_PIN                      GPIO_PIN_3
#define HW_ID3_CLK_ENABLE()             __HAL_RCC_GPIOH_CLK_ENABLE()
#define HW_ID3_CLK_DISABLE()            __HAL_RCC_GPIOH_CLK_DISABLE()
#endif /* BOARD */

/* LED definitions */
#if defined(N6CAM)
#define LED_USER1_PORT                  GPIOG
#define LED_USER1_PIN                   GPIO_PIN_9
#define LED_USER1_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define LED_USER1_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()
#elif defined(STM32N6DK_C02)
#define LED_USER1_PORT                  GPIOO
#define LED_USER1_PIN                   GPIO_PIN_1
#define LED_USER1_CLK_ENABLE()          __HAL_RCC_GPIOO_CLK_ENABLE()
#define LED_USER1_CLK_DISABLE()         __HAL_RCC_GPIOO_CLK_DISABLE()
#endif /* BOARD */

#define LED_USER2_PORT                  GPIOG
#define LED_USER2_PIN                   GPIO_PIN_10
#define LED_USER2_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define LED_USER2_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()

#if defined(N6CAM)
#define LED_USER3_PORT                  GPIOG
#define LED_USER3_PIN                   GPIO_PIN_4
#define LED_USER3_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define LED_USER3_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()

#define LED_CAMERA_PORT                 GPIOF
#define LED_CAMERA_PIN                  GPIO_PIN_4
#define LED_CAMERA_CLK_ENABLE()         __HAL_RCC_GPIOF_CLK_ENABLE()
#define LED_CAMERA_CLK_DISABLE()        __HAL_RCC_GPIOF_CLK_DISABLE()

#define LED_POWER_PORT                  GPIOB
#define LED_POWER_PIN                   GPIO_PIN_13
#define LED_POWER_CLK_ENABLE()          __HAL_RCC_GPIOB_CLK_ENABLE()
#define LED_POWER_CLK_DISABLE()         __HAL_RCC_GPIOB_CLK_DISABLE()
#endif /* BOARD */

/* CAMERA ---------------------------*/
#define CAMERA_PWR_EN_PORT              GPIOD
#define CAMERA_PWR_EN_PIN               GPIO_PIN_2
#define CAMERA_PWR_EN_CLK_ENABLE()      __HAL_RCC_GPIOD_CLK_ENABLE()
#define CAMERA_PWR_EN_CLK_DISABLE()     __HAL_RCC_GPIOD_CLK_DISABLE()

#if defined(STM32N6DK_C02)
#define CAMERA_PWR_RST_PORT             GPIOC
#define CAMERA_PWR_RST_PIN              GPIO_PIN_8
#define CAMERA_PWR_RST_CLK_ENABLE()     __HAL_RCC_GPIOC_CLK_ENABLE()
#define CAMERA_PWR_RST_CLK_DISABLE()    __HAL_RCC_GPIOC_CLK_DISABLE()
#endif /* BOARD */

/* VL53L7 ---------------------------*/
#if defined(N6CAM)
#define VL53L7_INT_PORT                 GPIOQ
#define VL53L7_INT_PIN                  GPIO_PIN_0
#define VL53L7_INT_CLK_ENABLE()         __HAL_RCC_GPIOQ_CLK_ENABLE()
#define VL53L7_INT_CLK_DISABLE()        __HAL_RCC_GPIOQ_CLK_DISABLE()
#define VL53L7_INT_EXTI_IRQn            EXTI0_IRQn
#define VL53L7_INT_EXTI_LINE            EXTI_LINE_0

#define VL53L7_LPN_PORT                 GPIOQ
#define VL53L7_LPN_PIN                  GPIO_PIN_5
#define VL53L7_LPN_CLK_ENABLE()         __HAL_RCC_GPIOQ_CLK_ENABLE()
#define VL53L7_LPN_CLK_DISABLE()        __HAL_RCC_GPIOQ_CLK_DISABLE()
#endif /* BOARD */

/* I2C1 -----------------------------*/
#define I2C1_SCL_PORT                   GPIOH
#define I2C1_SCL_PIN                    GPIO_PIN_9
#define I2C1_SCL_AF                     GPIO_AF4_I2C1
#define I2C1_SCL_CLK_ENABLE()           __HAL_RCC_GPIOH_CLK_ENABLE()
#define I2C1_SCL_CLK_DISABLE()          __HAL_RCC_GPIOH_CLK_DISABLE()

#if defined(N6CAM)
#define I2C1_SDA_PORT                   GPIOE
#define I2C1_SDA_PIN                    GPIO_PIN_6
#define I2C1_SDA_AF                     GPIO_AF4_I2C1
#define I2C1_SDA_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define I2C1_SDA_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()
#elif defined(STM32N6DK_C02)
#define I2C1_SDA_PORT                   GPIOC
#define I2C1_SDA_PIN                    GPIO_PIN_1
#define I2C1_SDA_AF                     GPIO_AF4_I2C1
#define I2C1_SDA_CLK_ENABLE()           __HAL_RCC_GPIOC_CLK_ENABLE()
#define I2C1_SDA_CLK_DISABLE()          __HAL_RCC_GPIOC_CLK_DISABLE()
#endif /* BOARD */

/* I2C2 -----------------------------*/
#define I2C2_SCL_PORT                   GPIOD
#define I2C2_SCL_PIN                    GPIO_PIN_14
#define I2C2_SCL_AF                     GPIO_AF4_I2C2
#define I2C2_SCL_CLK_ENABLE()           __HAL_RCC_GPIOD_CLK_ENABLE()
#define I2C2_SCL_CLK_DISABLE()          __HAL_RCC_GPIOD_CLK_DISABLE()

#define I2C2_SDA_PORT                   GPIOD
#define I2C2_SDA_PIN                    GPIO_PIN_4
#define I2C2_SDA_AF                     GPIO_AF4_I2C2
#define I2C2_SDA_CLK_ENABLE()           __HAL_RCC_GPIOD_CLK_ENABLE()
#define I2C2_SDA_CLK_DISABLE()          __HAL_RCC_GPIOD_CLK_DISABLE()

/* I2C4 -----------------------------*/
#if defined(N6CAM)
#define I2C4_SCL_PORT                   GPIOE
#define I2C4_SCL_PIN                    GPIO_PIN_13
#define I2C4_SCL_AF                     GPIO_AF4_I2C4
#define I2C4_SCL_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define I2C4_SCL_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()

#define I2C4_SDA_PORT                   GPIOD
#define I2C4_SDA_PIN                    GPIO_PIN_11
#define I2C4_SDA_AF                     GPIO_AF4_I2C4
#define I2C4_SDA_CLK_ENABLE()           __HAL_RCC_GPIOD_CLK_ENABLE()
#define I2C4_SDA_CLK_DISABLE()          __HAL_RCC_GPIOD_CLK_DISABLE()
#endif /* BOARD */

/* MDF1: Audio Mic ------------------*/
#define AUDIO_MIC_CLK_PORT              GPIOE
#define AUDIO_MIC_CLK_PIN               GPIO_PIN_2
#define AUDIO_MIC_CLK_AF                GPIO_AF4_MDF1
#define AUDIO_MIC_CLK_CLK_ENABLE()      __HAL_RCC_GPIOE_CLK_ENABLE()
#define AUDIO_MIC_CLK_CLK_DISABLE()     __HAL_RCC_GPIOE_CLK_DISABLE()

#define AUDIO_MIC_D1_PORT               GPIOE
#define AUDIO_MIC_D1_PIN                GPIO_PIN_8
#define AUDIO_MIC_D1_AF                 GPIO_AF4_MDF1
#define AUDIO_MIC_D1_CLK_ENABLE()       __HAL_RCC_GPIOE_CLK_ENABLE()
#define AUDIO_MIC_D1_CLK_DISABLE()      __HAL_RCC_GPIOE_CLK_DISABLE()

/* SDIO2: SD Card -------------------*/
#if defined(N6CAM)
#define SDIO2_PWR_EN_PORT               GPIOB
#define SDIO2_PWR_EN_PIN                GPIO_PIN_15
#define SDIO2_PWR_EN_CLK_ENABLE()       __HAL_RCC_GPIOB_CLK_ENABLE()
#define SDIO2_PWR_EN_CLK_DISABLE()      __HAL_RCC_GPIOB_CLK_DISABLE()

#define SDIO2_PWR_SEL_PORT              GPIOE
#define SDIO2_PWR_SEL_PIN               GPIO_PIN_1
#define SDIO2_PWR_SEL_CLK_ENABLE()      __HAL_RCC_GPIOE_CLK_ENABLE()
#define SDIO2_PWR_SEL_CLK_DISABLE()     __HAL_RCC_GPIOE_CLK_DISABLE()

#define SDIO2_DETECT_PORT               GPIOE
#define SDIO2_DETECT_PIN                GPIO_PIN_15
#define SDIO2_DETECT_CLK_ENABLE()       __HAL_RCC_GPIOE_CLK_ENABLE()
#define SDIO2_DETECT_CLK_DISABLE()      __HAL_RCC_GPIOE_CLK_DISABLE()
#define SDIO2_DETECT_EXTI_IRQn          EXTI15_IRQn
#define SDIO2_DETECT_EXTI_LINE          EXTI_LINE_15
#elif defined(STM32N6DK_C02)
#define SDIO2_PWR_EN_PORT               GPIOQ
#define SDIO2_PWR_EN_PIN                GPIO_PIN_7
#define SDIO2_PWR_EN_CLK_ENABLE()       __HAL_RCC_GPIOQ_CLK_ENABLE()
#define SDIO2_PWR_EN_CLK_DISABLE()      __HAL_RCC_GPIOQ_CLK_DISABLE()

#define SDIO2_PWR_SEL_PORT              GPIOO
#define SDIO2_PWR_SEL_PIN               GPIO_PIN_5
#define SDIO2_PWR_SEL_CLK_ENABLE()      __HAL_RCC_GPIOO_CLK_ENABLE()
#define SDIO2_PWR_SEL_CLK_DISABLE()     __HAL_RCC_GPIOO_CLK_DISABLE()

#define SDIO2_DETECT_PORT               GPION
#define SDIO2_DETECT_PIN                GPIO_PIN_12
#define SDIO2_DETECT_CLK_ENABLE()       __HAL_RCC_GPION_CLK_ENABLE()
#define SDIO2_DETECT_CLK_DISABLE()      __HAL_RCC_GPION_CLK_DISABLE()
#define SDIO2_DETECT_EXTI_IRQn          EXTI12_IRQn
#define SDIO2_DETECT_EXTI_LINE          EXTI_LINE_12
#endif /* BOARD */

#define SDIO2_CLK_PORT                  GPIOC
#define SDIO2_CLK_PIN                   GPIO_PIN_2
#define SDIO2_CLK_AF                    GPIO_AF11_SDMMC2
#define SDIO2_CLK_CLK_ENABLE()          __HAL_RCC_GPIOC_CLK_ENABLE()
#define SDIO2_CLK_CLK_DISABLE()         __HAL_RCC_GPIOC_CLK_DISABLE()

#define SDIO2_CMD_PORT                  GPIOC
#define SDIO2_CMD_PIN                   GPIO_PIN_3
#define SDIO2_CMD_AF                    GPIO_AF11_SDMMC2
#define SDIO2_CMD_CLK_ENABLE()          __HAL_RCC_GPIOC_CLK_ENABLE()
#define SDIO2_CMD_CLK_DISABLE()         __HAL_RCC_GPIOC_CLK_DISABLE()

#define SDIO2_D0_PORT                   GPIOC
#define SDIO2_D0_PIN                    GPIO_PIN_4
#define SDIO2_D0_AF                     GPIO_AF11_SDMMC2
#define SDIO2_D0_CLK_ENABLE()           __HAL_RCC_GPIOC_CLK_ENABLE()
#define SDIO2_D0_CLK_DISABLE()          __HAL_RCC_GPIOC_CLK_DISABLE()

#define SDIO2_D1_PORT                   GPIOC
#define SDIO2_D1_PIN                    GPIO_PIN_5
#define SDIO2_D1_AF                     GPIO_AF11_SDMMC2
#define SDIO2_D1_CLK_ENABLE()           __HAL_RCC_GPIOC_CLK_ENABLE()
#define SDIO2_D1_CLK_DISABLE()          __HAL_RCC_GPIOC_CLK_DISABLE()

#define SDIO2_D2_PORT                   GPIOC
#define SDIO2_D2_PIN                    GPIO_PIN_0
#define SDIO2_D2_AF                     GPIO_AF11_SDMMC2
#define SDIO2_D2_CLK_ENABLE()           __HAL_RCC_GPIOC_CLK_ENABLE()
#define SDIO2_D2_CLK_DISABLE()          __HAL_RCC_GPIOC_CLK_DISABLE()

#define SDIO2_D3_PORT                   GPIOE
#define SDIO2_D3_PIN                    GPIO_PIN_4
#define SDIO2_D3_AF                     GPIO_AF11_SDMMC2
#define SDIO2_D3_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define SDIO2_D3_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()

#if defined(N6CAM)
/* SPI3 -----------------------------*/
#define SPI3_CLK_PORT                   GPIOC
#define SPI3_CLK_PIN                    GPIO_PIN_10
#define SPI3_CLK_AF                     GPIO_AF6_SPI3
#define SPI3_CLK_CLK_ENABLE()           __HAL_RCC_GPIOC_CLK_ENABLE()
#define SPI3_CLK_CLK_DISABLE()          __HAL_RCC_GPIOC_CLK_DISABLE()

#define SPI3_MISO_PORT                  GPIOC
#define SPI3_MISO_PIN                   GPIO_PIN_11
#define SPI3_MISO_AF                    GPIO_AF6_SPI3
#define SPI3_MISO_CLK_ENABLE()          __HAL_RCC_GPIOC_CLK_ENABLE()
#define SPI3_MISO_CLK_DISABLE()         __HAL_RCC_GPIOC_CLK_DISABLE()

#define SPI3_MOSI_PORT                  GPIOC
#define SPI3_MOSI_PIN                   GPIO_PIN_12
#define SPI3_MOSI_AF                    GPIO_AF6_SPI3
#define SPI3_MOSI_CLK_ENABLE()          __HAL_RCC_GPIOC_CLK_ENABLE()
#define SPI3_MOSI_CLK_DISABLE()         __HAL_RCC_GPIOC_CLK_DISABLE()

#define SPI3_CS1_PORT                   GPIOC
#define SPI3_CS1_PIN                    GPIO_PIN_9
#define SPI3_CS1_CLK_ENABLE()           __HAL_RCC_GPIOC_CLK_ENABLE()
#define SPI3_CS1_CLK_DISABLE()          __HAL_RCC_GPIOC_CLK_DISABLE()

/* SPI4 -----------------------------*/
#define SPI4_CLK_PORT                   GPIOE
#define SPI4_CLK_PIN                    GPIO_PIN_12
#define SPI4_CLK_AF                     GPIO_AF5_SPI4
#define SPI4_CLK_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define SPI4_CLK_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()

#define SPI4_MISO_PORT                  GPIOB
#define SPI4_MISO_PIN                   GPIO_PIN_6
#define SPI4_MISO_AF                    GPIO_AF5_SPI4
#define SPI4_MISO_CLK_ENABLE()          __HAL_RCC_GPIOB_CLK_ENABLE()
#define SPI4_MISO_CLK_DISABLE()         __HAL_RCC_GPIOB_CLK_DISABLE()

#define SPI4_MOSI_PORT                  GPIOE
#define SPI4_MOSI_PIN                   GPIO_PIN_14
#define SPI4_MOSI_AF                    GPIO_AF5_SPI4
#define SPI4_MOSI_CLK_ENABLE()          __HAL_RCC_GPIOE_CLK_ENABLE()
#define SPI4_MOSI_CLK_DISABLE()         __HAL_RCC_GPIOE_CLK_DISABLE()

#define SPI4_CS1_PORT                   GPIOG
#define SPI4_CS1_PIN                    GPIO_PIN_6
#define SPI4_CS1_CLK_ENABLE()           __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI4_CS1_CLK_DISABLE()          __HAL_RCC_GPIOG_CLK_DISABLE()
#endif /* BOARD */

/* SPI5 -----------------------------*/
#if defined(N6CAM)
#define SPI5_CLK_PORT                   GPIOG
#define SPI5_CLK_PIN                    GPIO_PIN_12
#define SPI5_CLK_AF                     GPIO_AF5_SPI5
#define SPI5_CLK_CLK_ENABLE()           __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI5_CLK_CLK_DISABLE()          __HAL_RCC_GPIOG_CLK_DISABLE()

#define SPI5_MISO_PORT                  GPIOG
#define SPI5_MISO_PIN                   GPIO_PIN_1
#define SPI5_MISO_AF                    GPIO_AF5_SPI5
#define SPI5_MISO_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI5_MISO_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()

#define SPI5_MOSI_PORT                  GPIOG
#define SPI5_MOSI_PIN                   GPIO_PIN_2
#define SPI5_MOSI_AF                    GPIO_AF5_SPI5
#define SPI5_MOSI_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI5_MOSI_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()

#define SPI5_CS1_PORT                   GPIOG
#define SPI5_CS1_PIN                    GPIO_PIN_13
#define SPI5_CS1_CLK_ENABLE()           __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI5_CS1_CLK_DISABLE()          __HAL_RCC_GPIOG_CLK_DISABLE()

#define SPI5_CS2_PORT                   GPIOB
#define SPI5_CS2_PIN                    GPIO_PIN_10
#define SPI5_CS2_CLK_ENABLE()           __HAL_RCC_GPIOB_CLK_ENABLE()
#define SPI5_CS2_CLK_DISABLE()          __HAL_RCC_GPIOB_CLK_DISABLE()

#define SPI5_CS3_PORT                   GPIOG
#define SPI5_CS3_PIN                    GPIO_PIN_11
#define SPI5_CS3_CLK_ENABLE()           __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI5_CS3_CLK_DISABLE()          __HAL_RCC_GPIOG_CLK_DISABLE()

#define SPI5_CS4_PORT                   GPIOG
#define SPI5_CS4_PIN                    GPIO_PIN_0
#define SPI5_CS4_CLK_ENABLE()           __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI5_CS4_CLK_DISABLE()          __HAL_RCC_GPIOG_CLK_DISABLE()
#elif defined(STM32N6DK_C02)
#define SPI5_CLK_PORT                   GPIOE
#define SPI5_CLK_PIN                    GPIO_PIN_15
#define SPI5_CLK_AF                     GPIO_AF5_SPI5
#define SPI5_CLK_CLK_ENABLE()           __HAL_RCC_GPIOE_CLK_ENABLE()
#define SPI5_CLK_CLK_DISABLE()          __HAL_RCC_GPIOE_CLK_DISABLE()

#define SPI5_MISO_PORT                  GPIOH
#define SPI5_MISO_PIN                   GPIO_PIN_8
#define SPI5_MISO_AF                    GPIO_AF5_SPI5
#define SPI5_MISO_CLK_ENABLE()          __HAL_RCC_GPIOH_CLK_ENABLE()
#define SPI5_MISO_CLK_DISABLE()         __HAL_RCC_GPIOH_CLK_DISABLE()

#define SPI5_MOSI_PORT                  GPIOG
#define SPI5_MOSI_PIN                   GPIO_PIN_2
#define SPI5_MOSI_AF                    GPIO_AF5_SPI5
#define SPI5_MOSI_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define SPI5_MOSI_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()

#define SPI5_CS1_PORT                   GPIOA
#define SPI5_CS1_PIN                    GPIO_PIN_3
#define SPI5_CS1_CLK_ENABLE()           __HAL_RCC_GPIOA_CLK_ENABLE()
#define SPI5_CS1_CLK_DISABLE()          __HAL_RCC_GPIOA_CLK_DISABLE()
#endif /* BOARD */

/* USART1: STLINK -------------------*/
#if defined(N6CAM)
#define USART1_RX_PORT                  GPIOG
#define USART1_RX_PIN                   GPIO_PIN_8
#define USART1_RX_AF                    GPIO_AF4_USART1
#define USART1_RX_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define USART1_RX_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()
#elif defined(STM32N6DK_C02)
#define USART1_RX_PORT                  GPIOE
#define USART1_RX_PIN                   GPIO_PIN_6
#define USART1_RX_AF                    GPIO_AF7_USART1
#define USART1_RX_CLK_ENABLE()          __HAL_RCC_GPIOE_CLK_ENABLE()
#define USART1_RX_CLK_DISABLE()         __HAL_RCC_GPIOE_CLK_DISABLE()
#endif /* BOARD */

#define USART1_TX_PORT                  GPIOE
#define USART1_TX_PIN                   GPIO_PIN_5
#define USART1_TX_AF                    GPIO_AF7_USART1
#define USART1_TX_CLK_ENABLE()          __HAL_RCC_GPIOE_CLK_ENABLE()
#define USART1_TX_CLK_DISABLE()         __HAL_RCC_GPIOE_CLK_DISABLE()

/* USART2: APP ----------------------*/
#define USART2_RX_PORT                  GPIOF
#define USART2_RX_PIN                   GPIO_PIN_6
#define USART2_RX_AF                    GPIO_AF7_USART2
#define USART2_RX_CLK_ENABLE()          __HAL_RCC_GPIOF_CLK_ENABLE()
#define USART2_RX_CLK_DISABLE()         __HAL_RCC_GPIOF_CLK_DISABLE()

#if defined(N6CAM)
#define USART2_TX_PORT                  GPIOG
#define USART2_TX_PIN                   GPIO_PIN_3
#define USART2_TX_AF                    GPIO_AF7_USART2
#define USART2_TX_CLK_ENABLE()          __HAL_RCC_GPIOG_CLK_ENABLE()
#define USART2_TX_CLK_DISABLE()         __HAL_RCC_GPIOG_CLK_DISABLE()
#elif defined(STM32N6DK_C02)
#define USART2_TX_PORT                  GPIOD
#define USART2_TX_PIN                   GPIO_PIN_5
#define USART2_TX_AF                    GPIO_AF7_USART2
#define USART2_TX_CLK_ENABLE()          __HAL_RCC_GPIOD_CLK_ENABLE()
#define USART2_TX_CLK_DISABLE()         __HAL_RCC_GPIOD_CLK_DISABLE()
#endif /* BOARD */

/* USB1 -----------------------------*/
#define USB1_PWR_EN_PORT                GPIOA
#define USB1_PWR_EN_PIN                 GPIO_PIN_4
#define USB1_PWR_EN_CLK_ENABLE()        __HAL_RCC_GPIOA_CLK_ENABLE()
#define USB1_PWR_EN_CLK_DISABLE()       __HAL_RCC_GPIOA_CLK_DISABLE()

/* XSPI1: PSRAM ---------------------*/
#define XSPI1_CLK_PORT                  GPIOO
#define XSPI1_CLK_PIN                   GPIO_PIN_4
#define XSPI1_CLK_AF                    GPIO_AF9_XSPIM_P1
#define XSPI1_CLK_CLK_ENABLE()          __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_CLK_CLK_DISABLE()         __HAL_RCC_GPIOO_CLK_DISABLE()

#define XSPI1_DQS0_PORT                 GPIOO
#define XSPI1_DQS0_PIN                  GPIO_PIN_2
#define XSPI1_DQS0_AF                   GPIO_AF9_XSPIM_P1
#define XSPI1_DQS0_CLK_ENABLE()         __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_DQS0_CLK_DISABLE()        __HAL_RCC_GPIOO_CLK_DISABLE()

#define XSPI1_DQS1_PORT                 GPIOO
#define XSPI1_DQS1_PIN                  GPIO_PIN_3
#define XSPI1_DQS1_AF                   GPIO_AF9_XSPIM_P1
#define XSPI1_DQS1_CLK_ENABLE()         __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_DQS1_CLK_DISABLE()        __HAL_RCC_GPIOO_CLK_DISABLE()

#define XSPI1_D0_PORT                   GPIOP
#define XSPI1_D0_PIN                    GPIO_PIN_0
#define XSPI1_D0_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D0_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D0_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D1_PORT                   GPIOP
#define XSPI1_D1_PIN                    GPIO_PIN_1
#define XSPI1_D1_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D1_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D1_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D2_PORT                   GPIOP
#define XSPI1_D2_PIN                    GPIO_PIN_2
#define XSPI1_D2_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D2_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D2_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D3_PORT                   GPIOP
#define XSPI1_D3_PIN                    GPIO_PIN_3
#define XSPI1_D3_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D3_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D3_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D4_PORT                   GPIOP
#define XSPI1_D4_PIN                    GPIO_PIN_4
#define XSPI1_D4_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D4_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D4_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D5_PORT                   GPIOP
#define XSPI1_D5_PIN                    GPIO_PIN_5
#define XSPI1_D5_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D5_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D5_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D6_PORT                   GPIOP
#define XSPI1_D6_PIN                    GPIO_PIN_6
#define XSPI1_D6_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D6_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D6_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D7_PORT                   GPIOP
#define XSPI1_D7_PIN                    GPIO_PIN_7
#define XSPI1_D7_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D7_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D7_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D8_PORT                   GPIOP
#define XSPI1_D8_PIN                    GPIO_PIN_8
#define XSPI1_D8_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D8_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D8_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D9_PORT                   GPIOP
#define XSPI1_D9_PIN                    GPIO_PIN_9
#define XSPI1_D9_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_D9_CLK_ENABLE()           __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D9_CLK_DISABLE()          __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D10_PORT                  GPIOP
#define XSPI1_D10_PIN                   GPIO_PIN_10
#define XSPI1_D10_AF                    GPIO_AF9_XSPIM_P1
#define XSPI1_D10_CLK_ENABLE()          __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D10_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D11_PORT                  GPIOP
#define XSPI1_D11_PIN                   GPIO_PIN_11
#define XSPI1_D11_AF                    GPIO_AF9_XSPIM_P1
#define XSPI1_D11_CLK_ENABLE()          __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D11_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D12_PORT                  GPIOP
#define XSPI1_D12_PIN                   GPIO_PIN_12
#define XSPI1_D12_AF                    GPIO_AF9_XSPIM_P1
#define XSPI1_D12_CLK_ENABLE()          __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D12_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D13_PORT                  GPIOP
#define XSPI1_D13_PIN                   GPIO_PIN_13
#define XSPI1_D13_AF                    GPIO_AF9_XSPIM_P1
#define XSPI1_D13_CLK_ENABLE()          __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D13_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D14_PORT                  GPIOP
#define XSPI1_D14_PIN                   GPIO_PIN_14
#define XSPI1_D14_AF                    GPIO_AF9_XSPIM_P1
#define XSPI1_D14_CLK_ENABLE()          __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D14_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_D15_PORT                  GPIOP
#define XSPI1_D15_PIN                   GPIO_PIN_15
#define XSPI1_D15_AF                    GPIO_AF9_XSPIM_P1
#define XSPI1_D15_CLK_ENABLE()          __HAL_RCC_GPIOP_CLK_ENABLE()
#define XSPI1_D15_CLK_DISABLE()         __HAL_RCC_GPIOP_CLK_DISABLE()

#define XSPI1_CS_PORT                   GPIOO
#define XSPI1_CS_PIN                    GPIO_PIN_0
#define XSPI1_CS_AF                     GPIO_AF9_XSPIM_P1
#define XSPI1_CS_CLK_ENABLE()           __HAL_RCC_GPIOO_CLK_ENABLE()
#define XSPI1_CS_CLK_DISABLE()          __HAL_RCC_GPIOO_CLK_DISABLE()

/* XSPI2: FLASH ---------------------*/
#define XSPI2_CLK_PORT                  GPION
#define XSPI2_CLK_PIN                   GPIO_PIN_6
#define XSPI2_CLK_AF                    GPIO_AF9_XSPIM_P2
#define XSPI2_CLK_CLK_ENABLE()          __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_CLK_CLK_DISABLE()         __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_DQS_PORT                  GPION
#define XSPI2_DQS_PIN                   GPIO_PIN_0
#define XSPI2_DQS_AF                    GPIO_AF9_XSPIM_P2
#define XSPI2_DQS_CLK_ENABLE()          __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_DQS_CLK_DISABLE()         __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D0_PORT                   GPION
#define XSPI2_D0_PIN                    GPIO_PIN_2
#define XSPI2_D0_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D0_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D0_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D1_PORT                   GPION
#define XSPI2_D1_PIN                    GPIO_PIN_3
#define XSPI2_D1_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D1_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D1_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D2_PORT                   GPION
#define XSPI2_D2_PIN                    GPIO_PIN_4
#define XSPI2_D2_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D2_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D2_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D3_PORT                   GPION
#define XSPI2_D3_PIN                    GPIO_PIN_5
#define XSPI2_D3_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D3_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D3_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D4_PORT                   GPION
#define XSPI2_D4_PIN                    GPIO_PIN_8
#define XSPI2_D4_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D4_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D4_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D5_PORT                   GPION
#define XSPI2_D5_PIN                    GPIO_PIN_9
#define XSPI2_D5_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D5_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D5_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D6_PORT                   GPION
#define XSPI2_D6_PIN                    GPIO_PIN_10
#define XSPI2_D6_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D6_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D6_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_D7_PORT                   GPION
#define XSPI2_D7_PIN                    GPIO_PIN_11
#define XSPI2_D7_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_D7_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_D7_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

#define XSPI2_CS_PORT                   GPION
#define XSPI2_CS_PIN                    GPIO_PIN_1
#define XSPI2_CS_AF                     GPIO_AF9_XSPIM_P2
#define XSPI2_CS_CLK_ENABLE()           __HAL_RCC_GPION_CLK_ENABLE()
#define XSPI2_CS_CLK_DISABLE()          __HAL_RCC_GPION_CLK_DISABLE()

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: IO -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_IO_H_ */
