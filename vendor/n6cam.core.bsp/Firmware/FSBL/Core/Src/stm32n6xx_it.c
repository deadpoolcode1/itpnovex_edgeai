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

/* Private typedef -----------------------------------------------------------*/

/* Private defines -----------------------------------------------------------*/

/* Private macros ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

extern void bsp_io_irq_handler(t_io_id id, bool rising);

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
 * @brief This function handle registered IO rise/fall
 * @param id IO ID
 * @param True if rising edge, false if falling edge
 */
extern void bsp_io_irq_handler(t_io_id id, bool rising)
{
  UNUSED(rising);

  switch (id)
  {
    case IO_SD_DETECT:
      bsp_sdio_detect_callback(bsp_sdio_is_detected());
      break;

    default:
      /* Do nothing */
      break;
  }
}
