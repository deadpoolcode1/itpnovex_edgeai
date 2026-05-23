/**
 ******************************************************************************
 * @file    main.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Main N6Cam firmware.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT 2024 SIANA Systems</center></h2>
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
 ******************************************************************************
 */
#include <stdio.h>

#include "common.h"
#include "n6cam_core.h"
#include "n6cam_init.h"
#include "n6cam_mcu.h"
#if ENABLE_WATCHDOG == 1U
#include "n6cam_watchdog.h"
#endif /* ENABLE_WATCHDOG */
#include "main.h"

/* Private Tunables ----------------------------------------------------------*/

/* Private Definitions -------------------------------------------------------*/

/* Private Macros ------------------------------------------------------------*/

/* Private Types -------------------------------------------------------------*/

/* Private Data --------------------------------------------------------------*/

char FW_VERSION[VERSION_STR_SIZE];
const char* TRACE_TOPIC_NAME[TRACE_NUM] = {
  "SYSTEM",
  "CAMERA",
  "DISPLAY",
  "FX",
  "JPEG",
  "MODEM",
  "NN",
  "NX",
  "PP",
  "REGISTRY",
  "SCOM",
  "SENSOR_TOF",
  "SHELL",
  "SNAPSHOT",
  "BIST",
  "UX",
};

extern uint32_t _sflash;
extern uint32_t _eflash;
extern uint32_t __uncached_bss_start__;
extern uint32_t __uncached_bss_end__;

/* Private Functions ---------------------------------------------------------*/

static void     MPU_Config(void);
static void     RTOS_Init(void);
extern void     Error_Handler(void);

extern int32_t  sysutils_get_firmware_version_string(uint8_t *buff, size_t size);
extern int32_t  sysutils_get_mcu_uptime_string(uint8_t *buff, size_t size);
extern void     sysutils_mcu_reboot(const t_stream *stream, int32_t reason);

/* Public API ----------------------------------------------------------------*/

/**
 * @brief  Main program
 */
int main(void)
{
  /* Prepare */
  bsp_system_fixes();
  bsp_usb_fixes();

  /* Set back SYS/CPU clock to HSI */
  __HAL_RCC_CPUCLK_CONFIG(RCC_CPUCLKSOURCE_HSI);
  __HAL_RCC_SYSCLK_CONFIG(RCC_SYSCLKSOURCE_HSI);

  /* Configure MPU */
  MPU_Config();

  /* MCU Configuration */
  HAL_Init();

  /* Enable cache */
  bsp_mcu_cache_enable();

  /* Set version string */
  snprintf(FW_VERSION, VERSION_STR_SIZE, "%02lu.%02lu.%lu", (uint32_t)VERSION_MAJOR, (uint32_t)VERSION_MINOR, (uint32_t)VERSION_BUILD);

  /* Initialize ThreadX */
  RTOS_Init();
}

/* Private Functions ---------------------------------------------------------*/

/**
 * @brief Configure MPU.
 * Configures:
 * - R0: Uncached RAM
 * - R1: External flash
 */
static void MPU_Config(void)
{
  uint8_t                     attr_cacheable     = MPU_ATTRIBUTES_NUMBER0;
  uint8_t                     attr_not_cacheable = MPU_ATTRIBUTES_NUMBER1;
  uint32_t                    primask;
  MPU_Attributes_InitTypeDef  attr;
  MPU_Region_InitTypeDef      region;

  /* Enter critical zone */
  primask = __get_PRIMASK();
  __disable_irq();

  /* Disable MPU */
  HAL_MPU_Disable();

  /* Configure attributes */
  attr.Number     = attr_cacheable;
  attr.Attributes = INNER_OUTER(MPU_WRITE_BACK | MPU_NON_TRANSIENT | MPU_RW_ALLOCATE);
  HAL_MPU_ConfigMemoryAttributes(&attr);

  attr.Number     = attr_not_cacheable;
  attr.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);
  HAL_MPU_ConfigMemoryAttributes(&attr);

  /* Configure regions */
  /* R0: FLASH cached */
  region.AttributesIndex  = attr_cacheable;
  region.Number           = MPU_REGION_NUMBER0;
  region.Enable           = MPU_REGION_ENABLE;
  region.BaseAddress      = (uint32_t)&_sflash;
  region.LimitAddress     = (uint32_t)&_eflash - 1U;
  region.AccessPermission = MPU_REGION_ALL_RW;
  region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  HAL_MPU_ConfigRegion(&region);

  /* R1: SRAM uncached */
  region.AttributesIndex  = attr_not_cacheable;
  region.Number           = MPU_REGION_NUMBER1;
  region.Enable           = MPU_REGION_ENABLE;
  region.BaseAddress      = (uint32_t)&__uncached_bss_start__;
  region.LimitAddress     = (uint32_t)&__uncached_bss_end__ - 1U;
  region.AccessPermission = MPU_REGION_ALL_RW;
  region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  HAL_MPU_ConfigRegion(&region);

  /* Enable MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

  /* Exit critical section */
  __set_PRIMASK(primask);
}

/**
 * @brief ThreadX initialization.
 */
static void RTOS_Init(void)
{
  tx_kernel_enter();

  /* We should never get here (since managed on RTOS) */
  Error_Handler();
}

/**
 * @brief This function is executed in case of error occurrence.
 */
void Error_Handler(void)
{
  /* Just in case tracer still works */
  TX_THREAD* thread = tx_thread_identify();
  if (thread)
  {
    LERROR(TRACE_SYSTEM, "FAILURE (%s)!", thread->tx_thread_name);
  }
  else
  {
    LERROR(TRACE_SYSTEM, "FAILURE!");
  }

  #if ENABLE_WATCHDOG == 1U
  /* Release the watchdog */
  bsp_watchdog_release();
  LINFO(TRACE_SYSTEM, "Watchdog released!");
  #endif /* ENABLE_WATCHDOG */

  /* Show blinking LED for error */
  while (1)
  {
    bsp_led_toggle(LED_USER1);
    HAL_Delay(100);
  }
}

/**
 * @brief Gets the firmware version string
 * @param buff String buffer
 * @param size String buffer size
 * @return Size of the string or error code (negative)
 */
int32_t sysutils_get_firmware_version_string(uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Generate version */
  snprintf((char*)buff, size, FW_VERSION);
  return (int32_t)strlen((char*)buff);
}

/**
 * @brief Gets the hardware version string
 * @param buff String buffer
 * @param size String buffer size
 * @return Size of the string or error code (negative)
 */
int32_t sysutils_get_hardware_version_string(uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Generate version */
  snprintf((char*)buff, size, bsp_hw_id_get_string());
  return (int32_t)strlen((char*)buff);
}

/**
 * @brief Gets the MCU uptime string
 * @param buff String buffer
 * @param size String buffer size
 * @return Size of the string or error code (negative)
 */
int32_t sysutils_get_mcu_uptime_string(uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (size == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Generate version */
  snprintf((char*)buff, size, "%lu[msec]", HAL_GetTick());
  return (int32_t)strlen((char*)buff);
}

/**
 * @brief Reboot the MCU
 * @param stream Stream instance
 * @param reason Reboot reason
 */
void sysutils_mcu_reboot(const t_stream *stream, int32_t reason)
{
  UNUSED(stream);
  UNUSED(reason);
  #ifndef DEBUG
  HAL_Delay(100);
  HAL_NVIC_SystemReset();
  #endif /* DEBUG */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 */
void assert_failed(uint8_t * file, uint32_t line)
{
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  Error_Handler();
}
#endif /* USE_FULL_ASSERT */
