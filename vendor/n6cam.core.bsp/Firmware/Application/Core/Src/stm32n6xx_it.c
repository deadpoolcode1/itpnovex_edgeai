/**
  ******************************************************************************
  * @file    stm32n6xx_it.c 
  * @author  MCD Application Team
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
#include "common.h"
#include "n6cam_core.h"
#include "n6cam_sdio.h"
#include "stm32n6xx_hal.h"
#include "stm32n6xx_it.h"

#if ENABLE_SENSOR_TOF == 1U
  #include "sensor_tof_task.h"
#endif /* ENABLE_SENSOR_TOF */

#if defined(N6CAM_WIFI_MURATA)
  #include "stm32_cyhal_gpio_ex.h"
  #include "stm32_cyhal_sdio_ex.h"
#endif /* N6CAM_WIFI_MURATA */

/* Private typedef -----------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/* Private macros ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

extern DCMIPP_HandleTypeDef hdcmipp;
extern PCD_HandleTypeDef    husb1;

/* Private function prototypes -----------------------------------------------*/

extern void CSI_IRQHandler(void);
extern void DCMIPP_IRQHandler(void);
extern void SDMMC1_IRQHandler(void);
extern void USB1_OTG_HS_IRQHandler(void);

extern void bsp_io_irq_handler(t_io_id id, bool rising);
extern void bsp_io_unknown_irq_handler(uint16_t gpio, bool rising);

/* Private functions ---------------------------------------------------------*/
/******************************************************************************/
/*            Cortex-M55 Processor Exceptions Handlers                        */
/******************************************************************************/
/**
 * @brief  This function handles Bus Fault exception.
 */
void BusFault_Handler(void)
{
  Error_Handler();
}

/**
 * @brief  This function handles Debug Monitor exception.
 */
void DebugMon_Handler(void)
{
  Error_Handler();
}

/**
 * @brief  This function handles Hard Fault exception.
 */
void HardFault_Handler(void)
{
  Error_Handler();
}

/**
 * @brief   This function handles IAC violations.
 */
void IAC_IRQHandler(void)
{
  Error_Handler();
}

/**
 * @brief  This function handles NMI exception.
 */
void NMI_Handler(void)
{
  Error_Handler();
}

/**
 * @brief  This function handles Memory Manage exception.
 */
void MemManage_Handler(void)
{
  Error_Handler();
}

/**
 * @brief  This function handles Secure Manage exception.
 */
void SecureFault_Handler(void)
{
  Error_Handler();
}

/**
 * @brief  This function handles SVCall exception.
 */
void SVC_Handler(void)
{
  Error_Handler();
}

/**
 * @brief  This function handles Usage Fault exception.
 */
void UsageFault_Handler(void)
{
  Error_Handler();
}

/******************************************************************************/
/*                 STM32N6xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32n6xxxx.s).                                             */
/******************************************************************************/

/**
 * @brief This function handles CSI interrupt request.
 */
void CSI_IRQHandler(void)
{
  HAL_DCMIPP_CSI_IRQHandler(&hdcmipp);
}

/**
 * @brief This function handles DCMIPP interrupt request.
 */
void DCMIPP_IRQHandler(void)
{
  HAL_DCMIPP_IRQHandler(&hdcmipp);
}

/**
 * @brief This function handles SDMMC1 interrupt request.
 */
void SDMMC1_IRQHandler(void)
{
  #if defined(N6CAM_WIFI_MURATA)
  stm32_cyhal_sdio_irq_handler();
  #endif /* N6CAM_WIFI_MURATA */
}

/**
 * @brief This function handles USB1 interrupt request.
 */
void USB1_OTG_HS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&husb1);
}

/**
 * @brief IO IRQ callback
 * @param id      IO ID
 * @param rising  true if rising edge, false if falling edge
 */
void bsp_io_irq_handler(t_io_id id, bool rising)
{
  UNUSED(rising);

  switch (id)
  {
    case IO_SD_DETECT:
      bsp_sdio_detect_callback(bsp_sdio_is_detected());
      break;

    #if ENABLE_SENSOR_TOF == 1U
    case IO_VL53L7_INT:
      sensor_tof_data_available_callback();
      break;
    #endif /* ENABLE_SENSOR_TOF */

    default:
      /* Do nothing */
      break;
  }
}

/**
 * @brief Unknown IO IRQ callback
 * @param pin     GPIO pin number
 * @param rising  true if rising edge, false if falling edge
 */
void bsp_io_unknown_irq_handler(uint16_t pin, bool rising)
{
  #if defined(N6CAM_WIFI_MURATA)
  if (rising && (pin == GPIO_PIN_1))
  {
    stm32_cyhal_gpio_irq_handler(pin);
  }
  #endif /* N6CAM_WIFI_MURATA */
}
