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
#include "main.h"
#include "n6cam_core.h"
#include "n6cam_crc.h"
#include "n6cam_init.h"
#include "n6cam_mcu.h"
#include "n6cam_xspi.h"

/* Private Tunables ----------------------------------------------------------*/

/* Private Definitions -------------------------------------------------------*/

/*-->> BOOT settings <<--------------*/
#define BOOT_HDR_OFFSET           0x00000400U

/* Using header v2.3 */
#define BOOT_HDR_IMGSIZE_OFFSET   108U
#define BOOT_HDR_SIZE             1024U

/* Recovery magic value in TAMP->BKP0R; staged by Application's 'recovery'
 * shell command so FSBL halts before configuring xSPI, allowing SWD flash
 * without the dev-mode boot switch. Must match Application/.../shell_task.c. */
#define FSBL_RECOVERY_MAGIC       0xDEADBEEFU

/* Private Macros ------------------------------------------------------------*/

/* Private Types -------------------------------------------------------------*/

/* Private Data --------------------------------------------------------------*/

char FW_VERSION[VERSION_STR_SIZE];
const char* TRACE_TOPIC_NAME[TRACE_NUM] = {
  "SYSTEM",
  "FT",
};

extern uint32_t _sflash;
extern uint32_t _eflash;
extern uint32_t _app_sram;
extern uint32_t _app_flash;

/* Private Functions ---------------------------------------------------------*/

static void     _boot(void);
static int32_t  _boot_copy(void);
static int32_t  _boot_jump(void);

static void     Clock_Config(void);
static void     BSP_Config(void);
static void     MPU_Config(void);
static void     RTOS_Init(void);
extern void     Error_Handler(void);

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

  /* Configure power */
  bsp_config_pwr();

  /* Recovery hook: if Application staged the magic in TAMP->BKP0R, halt here
   * with xSPI still uninitialized so STM32_Programmer_CLI can flash via SWD
   * without the user toggling the dev-mode boot switch. */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_RTCAPB_CLK_ENABLE();
  if (TAMP->BKP0R == FSBL_RECOVERY_MAGIC)
  {
    TAMP->BKP0R = 0U;   /* clear so next reset is a normal boot */
    while (1)
    {
      __WFI();
    }
  }

  /* Configure system clock */
  Clock_Config();

  /* Update OTP fuses to enable XSPI high speed/low power */
  bsp_config_fuse();

  /* Set all required IPs as secure privileged */
  bsp_config_iac();
  bsp_config_risaf();

  /* Get HW ID */
  bsp_hw_id_init();

  /* Initialize peripherals */
  BSP_Config();

  /* Set version string */
  snprintf(FW_VERSION, VERSION_STR_SIZE, "%02lu.%02lu.%lu", (uint32_t)VERSION_MAJOR, (uint32_t)VERSION_MINOR, (uint32_t)VERSION_BUILD);

  /* Start factory test (if required) */
  if (bsp_btn_get_state(BTN_USER1) == true)
  {
    /* Start threadX to run factory test */
    RTOS_Init();
  }

  /* Jump to application */
  _boot();

  /* We should never get here */
  Error_Handler();
}

/* Private Functions ---------------------------------------------------------*/
/**
 * @brief Boot application.
 */
static void _boot(void)
{
  int32_t status;
  status = _boot_copy();
  if (status == BSP_OK)
  {
    status = _boot_jump();
    if (status != BSP_OK)
    {
      Error_Handler();
    }
  }
}

/**
 * @brief Boot: Copy application.
 */
static int32_t _boot_copy(void)
{
  uint8_t  *dst = (uint8_t*)((uint32_t)&_app_sram);
  uint8_t  *src = (uint8_t*)((uint32_t)&_app_flash);
  uint32_t size = (*(uint32_t*)(src + BOOT_HDR_IMGSIZE_OFFSET)) + BOOT_HDR_SIZE;

  /* Copy application */
  for (uint32_t idx = 0; idx < size; idx++)
  {
    dst[idx] = src[idx];
  }
  return BSP_OK;
}

/**
 * @brief Boot: Jump to application.
 */
static int32_t _boot_jump(void)
{
  typedef  void (*t_jump)(void);

  uint32_t primask;
  uint32_t vtor;
  t_jump   jump;

  /* Prepare */
  HAL_SuspendTick();
  SCB_DisableICache();
  SCB_DisableDCache();

  primask = __get_PRIMASK();
  __disable_irq();

  /* Configure VTOR */
  vtor = (uint32_t)((uint32_t)&_app_sram + BOOT_HDR_OFFSET);
  SCB->VTOR = vtor;
  __DSB();

  jump = (t_jump)(*(__IO uint32_t*)(vtor + 4U));

#if (                                                                     \
  (defined (__ARM_ARCH_8M_MAIN__ ) && (__ARM_ARCH_8M_MAIN__ == 1))     || \
  (defined (__ARM_ARCH_8_1M_MAIN__ ) && (__ARM_ARCH_8_1M_MAIN__ == 1)) || \
  (defined (__ARM_ARCH_8M_BASE__ ) && (__ARM_ARCH_8M_BASE__ == 1))        \
)
  /* on ARM v8m, set MSPLIM before setting MSP to avoid unwanted stack overflow faults */
  __set_MSPLIM(0x00000000U);
#endif  /* __ARM_ARCH_8M_MAIN__ or __ARM_ARCH_8M_BASE__ */

  /* Jump */
  __set_MSP(*(__IO uint32_t*)vtor);
  __set_PRIMASK(primask);
  __enable_irq();
  jump();

  return BSP_OK;
}

/**
 * Configure system clocks:
 *   Sources -----------------------------------
 *
 *   HSI                            = 64     [MHz]
 *   HSE                            = 48     [MHz]
 *   LSE                            = 32.768 [kHz]
 *
 *   PLLs --------------------------------------
 *
 *          SRC   M   N   P1  P2  OUT ((SRC / M) * (N / P1 / P2))
 *   PLL1   HSI   1   25  1   1   1600 [MHz]
 *   PLL2   HSI   8   125 1   1   1000 [MHz]
 *   PLL3   HSI   8   225 2   1    900 [MHz]
 *   PLL4   OFF   -   -   -   -      -
 *
 *   System ------------------------------------
 *
 *                  SRC   DIV   OUT (SRC / DIV)
 *   SYSA (IC1 )    PLL1  2      800     [MHz] > CPUCLK
 *   SYSB (IC2 )    PLL1  4      400     [MHz] > AXI/AHB/APB
 *   SYSC (IC6 )    PLL2  1     1000     [MHz] > NPU
 *   SYSD (IC11)    PLL3  1      900     [MHz] > AXISRAM3/4/5/6
 *
 *   AHB/APB Presc = 2 -> 200 [MHz]
 *   APBX    Presc = 1 -> 200 [MHz]
 *
 *   Peripherals -------------------------------
 *
 *                  SRC   DIV   OUT (SRC / DIV)
 *   I2C1           PCLK1 -      200     [MHz]
 *   MDF    (IC7)   PLL4                       > CHECK BSP
 *   SDMMC2         HCLK  -      200     [MHz]
 *   USART1 (IC14)  PLL1  16     100     [MHz]
 *   XSPI1          HCLK  -      200     [MHz]
 *   XSPI2          HCLK  -      200     [MHz]
 *
 *   -------------------------------------------
 */
static void Clock_Config(void)
{
  RCC_OscInitTypeDef       osc = { 0 };
  RCC_ClkInitTypeDef       clk = { 0 };
  RCC_PeriphCLKInitTypeDef per = { 0 };
  int32_t                  status;

  /* CPU/SYS CLK config done in BootROM (and HSI selected on main)
   * Use this to init:
   * - HSE for MDF/USB
   * - LSE for RTC
   */
  osc.OscillatorType     = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState           = RCC_HSE_ON;
  status = HAL_RCC_OscConfig(&osc);
  if (status != HAL_OK)
  {
    Error_Handler();
  }

  /* PLLs -------------------------------------*/
  /* PLL1 = (HSI / 1) * (25 / 1 / 1) = 1600MHz */
  osc.PLL1.PLLState      = RCC_PLL_ON;
  osc.PLL1.PLLSource     = RCC_PLLSOURCE_HSI;
  osc.PLL1.PLLM          = 1;
  osc.PLL1.PLLN          = 25;
  osc.PLL1.PLLP1         = 1;
  osc.PLL1.PLLP2         = 1;

  /* PLL2 = (HSI / 8) * (125 / 1 / 1) = 1000MHz */
  osc.PLL2.PLLState      = RCC_PLL_ON;
  osc.PLL2.PLLSource     = RCC_PLLSOURCE_HSI;
  osc.PLL2.PLLM          = 8;
  osc.PLL2.PLLN          = 125;
  osc.PLL2.PLLP1         = 1;
  osc.PLL2.PLLP2         = 1;

  /* PLL3 = (HSI / 8) * (225 / 2 / 1) = 900MHz */
  osc.PLL3.PLLState      = RCC_PLL_ON;
  osc.PLL3.PLLSource     = RCC_PLLSOURCE_HSI;
  osc.PLL3.PLLM          = 8;
  osc.PLL3.PLLN          = 225;
  osc.PLL3.PLLP1         = 2;
  osc.PLL3.PLLP2         = 1;

  /* PLL4 = (HSI / 1) * (25 / 2 / 1) = 800MHz */
  osc.PLL4.PLLState      = RCC_PLL_BYPASS;
  osc.PLL4.PLLSource     = RCC_PLLSOURCE_HSI;

  status = HAL_RCC_OscConfig(&osc);
  if (status != HAL_OK)
  {
    Error_Handler();
  }

  /* System -----------------------------------*/
  /* Configure the HCLK, PCLK1, PCLK2, PCLK4 and PCLK5 clocks dividers */
  clk.ClockType = (
    RCC_CLOCKTYPE_CPUCLK  | RCC_CLOCKTYPE_SYSCLK  | RCC_CLOCKTYPE_HCLK  |
    RCC_CLOCKTYPE_PCLK1   | RCC_CLOCKTYPE_PCLK2   | RCC_CLOCKTYPE_PCLK4 |RCC_CLOCKTYPE_PCLK5
  );

  /* Configuring SYS/CPU clocks */
  clk.CPUCLKSource                = RCC_CPUCLKSOURCE_IC1;
  clk.SYSCLKSource                = RCC_SYSCLKSOURCE_IC2_IC6_IC11;

  /* IC1 - SYSA = PLL1 / 2 = 800MHz */
  clk.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC1Selection.ClockDivider   = 2;

  /* IC2 - SYSB = PLL1 / 4 = 400MHz */
  clk.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  clk.IC2Selection.ClockDivider   = 4;

  /* IC6 - SYSC = PLL2 / 1 = 1000MHz */
  clk.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL2;
  clk.IC6Selection.ClockDivider   = 1;

  /* IC11 - SYSD = PLL3 / 1 = 900MHz */
  clk.IC11Selection.ClockSelection= RCC_ICCLKSOURCE_PLL3;
  clk.IC11Selection.ClockDivider  = 1;

  /* HCLK = SYSB / 2 = 200 MHz */
  clk.AHBCLKDivider  = RCC_HCLK_DIV2;

  /* PCLKx = HCLK / 1 = 200 MHz */
  clk.APB1CLKDivider = RCC_APB1_DIV1;
  clk.APB2CLKDivider = RCC_APB2_DIV1;
  clk.APB4CLKDivider = RCC_APB4_DIV1;
  clk.APB5CLKDivider = RCC_APB5_DIV1;

  status = HAL_RCC_ClockConfig(&clk);
  if (status != HAL_OK)
  {
    Error_Handler();
  }

  /* ICs --------------------------------------*/
  /* v = PLL1 / 16 = 100MHz */
  per.ICSelection[RCC_IC14].ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  per.ICSelection[RCC_IC14].ClockDivider    = 16;

  /* Peripherals ------------------------------*/
  /* I2C1 CLK = PCLK1 = 200MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_I2C1;
  per.I2c1ClockSelection      = RCC_I2C1CLKSOURCE_PCLK1;

  /* SDMMC2 CLK = HCLK = 200MHz */
  per.PeriphClockSelection |= RCC_PERIPHCLK_SDMMC2;
  per.Sdmmc2ClockSelection = RCC_SDMMC2CLKSOURCE_HCLK;

  /* USART1 CLK = IC9 = 100MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_USART1;
  per.Usart1ClockSelection    = RCC_USART1CLKSOURCE_IC14;

  /* XSPI1 CLK  = HCLK = 200MHz */
  per.PeriphClockSelection |= RCC_PERIPHCLK_XSPI1;
  per.Xspi1ClockSelection  = RCC_XSPI1CLKSOURCE_HCLK;

  /* XSPI2 CLK  = HCLK = 200MHz */
  per.PeriphClockSelection |= RCC_PERIPHCLK_XSPI2;
  per.Xspi2ClockSelection  = RCC_XSPI2CLKSOURCE_HCLK;

  status = HAL_RCCEx_PeriphCLKConfig(&per);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief Configure BSP.
 */
static void BSP_Config(void)
{
  BSP_XSPI_NOR_Init_t nor_config = {0};
  int32_t             status;

  /* Initialize CRC */
  status = bsp_crc_init();
  if (status != BSP_OK)
  {
    Error_Handler();
  }

  /* Initialize External RAM */
  status = BSP_XSPI_RAM_Init(0);
  if (status != BSP_OK)
  {
    Error_Handler();
  }
  status = BSP_XSPI_RAM_EnableMemoryMappedMode(0);
  if (status != BSP_OK)
  {
    Error_Handler();
  }

  /* Initialize XSPI_NOR */
  nor_config.InterfaceMode = BSP_XSPI_NOR_OPI_MODE;
  nor_config.TransferRate  = BSP_XSPI_NOR_DTR_TRANSFER;
  status = BSP_XSPI_NOR_Init(0, &nor_config);
  if (status != BSP_OK)
  {
    Error_Handler();
  }
  status = BSP_XSPI_NOR_EnableMemoryMappedMode(0);
  if (status != BSP_OK)
  {
    Error_Handler();
  }

  /* Initialize buttons */
  for (t_btn_id btn = BTN_START; btn < BTN_NUM; btn++)
  {
    status = bsp_btn_init(btn);
    if (status != BSP_OK)
    {
      Error_Handler();
    }
  }

  /* Initialize LEDs */
  for (t_led_id led = LED_START; led < LED_NUM; led++)
  {
    status = bsp_led_init(led);
    if (status != BSP_OK)
    {
      Error_Handler();
    }
  }
  /* By default, turn on the power LED */
  bsp_led_on(LED_POWER);
}

/**
 * @brief Configure MPU.
 * Configures:
 * - R0: Uncached RAM
 */
static void MPU_Config(void)
{
  uint8_t                     attr_cacheable     = MPU_ATTRIBUTES_NUMBER0;
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
  TX_THREAD* thread = tx_thread_identify();

  /* Just in case tracer still works */
  if (thread)
  {
    LERROR(TRACE_SYSTEM, "FAILURE (%s)!", thread->tx_thread_name);
  }
  else
  {
    LERROR(TRACE_SYSTEM, "FAILURE!");
  }

  /* Show blinking LED for error */
  while (1)
  {
    bsp_led_toggle(LED_USER1);
    HAL_Delay(100);
  }
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

