/**
 ******************************************************************************
 * @file    system_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the system module.
 *          This module is responsible for:
 *          - Initialize platform
 *          - Blinking status LED
 *          - Generate status message
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2024 SIANA Systems</center></h2>
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
#include "camera_task.h"
#include "flash.h"
#include "fx_app.h"
#include "n6cam_core.h"
#include "n6cam_crc.h"
#include "n6cam_i2c.h"
#include "n6cam_init.h"
#include "n6cam_rtc.h"
#include "n6cam_uart.h"
#if ENABLE_WATCHDOG == 1U
#include "n6cam_watchdog.h"
#endif /* ENABLE_WATCHDOG */
#include "n6cam_xspi.h"
#if defined(N6CAM_WIFI_MURATA)
#include "nx_app.h"
#endif /* N6CAM_WIFI_MURATA */
#include "slib32_tick.h"
#include "snapshot_task.h"
#include "system_task.h"
#include "ux_app.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup System
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

/* System task tunables
 * TODO: Optimize stack size
 */
#define SYSTEM_TASK_STACK_SIZE  (2U * 1024U)
#define SYSTEM_TASK_PRIO        APP_PRIO_BACKGROUND
#define SYSTEM_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/* Internals */
#define SYSTEM_PERIOD           50U               /*!< System task period in [ms] */
#define SYSTEM_STATUS_PERIOD    1000U             /*!< Blink status LED@0.5Hz */
#define SYSTEM_FPS_PERIOD       5000U             /*!< Print camera FPS@0.2Hz */

#define TRACE_UART              UART_STLINK       /*!< Trace UART */
#define TRACE_OUT_SIZE          1024U             /*!< Trace output buffer size */

#define BTN_DEBOUNCE            50U               /*!< Button debounce time in [ms] */
#define BTN_PRESS_SHORT         750U              /*!< Short press time in [ms] */
#define BTN_PRESS_LONG          1000U             /*!< Long press time in [ms] */

#if defined(N6CAM)
#define BLINK_LED               LED_USER3         /*!< Status LED */
#elif defined(STM32N6DK_C02)
#define BLINK_LED               LED_USER1         /*!< Status LED */
#endif /* BOARD */

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

/** Led instance */
typedef struct
{
  uint8_t  state;       /*!< Current state */
  int32_t  blink;       /*!< Blink mode: -1: continuous, 0: off, >0: N blinks */
  uint32_t start;       /*!< Blink start time */
  uint32_t uptime;      /*!< Uptime in [ms] */
  uint32_t period;      /*!< Blink period in [ms] */
} t_led;

/** Button instance */
typedef struct
{
  bool      update;     /*!< Update flag */
  bool      pressed;    /*!< Current pressed state */
  uint32_t  press;      /*!< Time of press */
  uint32_t  event;      /*!< Time of last event */
} t_btn;

/** System task handler */
typedef struct
{
  TX_THREAD thread;
  uint8_t   stack[SYSTEM_TASK_STACK_SIZE];
} t_system_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_system_task      _system_task  = { 0 };
static volatile t_btn     _btn[BTN_NUM] = { 0 };

static TX_MUTEX           _stat_mtx;
static t_stat_entry       _stat_entry[STAT_ENTRY_NUM];

static TX_MUTEX           _trace_mtx;
static uint8_t            _trace_out[TRACE_OUT_SIZE];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _system_task_run(uint32_t args);
static void _system_task_init(void);

static void _system_config_bsp(void);
static void _system_config_camera(void);
static void _system_config_clocks(void);
static void _system_config_extras(void);
static void _system_config_io(void);
static void _system_config_memory(void);
static void _system_config_sensors(void);
static void _system_config_stat(void);
static void _system_config_trace(void);

static void _system_btn_user_update(void);
static void _system_led_update(t_led_id id, t_led *led);

extern void bsp_btn_irq_handler(t_btn_id id, bool rising);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t system_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_system_task.thread, "tx.task.system",
    _system_task_run, 0,
    _system_task.stack, SYSTEM_TASK_STACK_SIZE,
    SYSTEM_TASK_PRIO, SYSTEM_TASK_PRIO,
    SYSTEM_TASK_TIME_SLICE, TX_AUTO_START
  );
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief System task entry point.
 * @param args Task arguments
 */
static void _system_task_run(uint32_t args)
{
  /* Periodic tasks */
  uint32_t led1_start = 0U;
  uint32_t info_start = 0U;

  UNUSED(args);

  /* Initialize */
  _system_task_init();

  /* System task */
  while (1)
  {
    /* Update buttons */
    _system_btn_user_update();

    /* Update status LED */
    if (tick_is_expired(led1_start, SYSTEM_STATUS_PERIOD))
    {
      led1_start = tx_time_get();
      bsp_led_toggle(LED_USER1);
    }

    /* Update status traces */
    if (tick_is_expired(info_start, SYSTEM_FPS_PERIOD))
    {
      info_start = tx_time_get();
      stat_freq_update(STAT_FREQ_CAMERA);
      LINFO(TRACE_SYSTEM, "FPS: Camera(%3lu Hz)", (uint32_t)stat_value(STAT_FREQ_CAMERA));
    }

    /* Wait a while */
    tx_thread_sleep(SYSTEM_PERIOD);
  }
}

/**
 * @brief System task initialization.
 */
static void _system_task_init(void)
{
  /*-->> DEPENDENCIES <<--*/
  /* No dependencies */

  /*-->> INITIALIZE <<--*/
  /* Configure platform */
  _system_config_bsp();

  /* Wait for sensor configuration */
  task_wait_event(TX_EVT_SENSOR_PREPARE);
  _system_config_camera();
  _system_config_sensors();
  task_raise_event(TX_EVT_SENSOR_REQUIRE);
  task_wait_event(TX_EVT_SENSOR_READY);

  /* Configure systems */
  fx_app_init();
  #if defined(N6CAM_WIFI_MURATA)
  nx_app_init();
  #endif /* N6CAM_WIFI_MURATA */
  ux_app_init();

  /*-->> READY <<--*/
  #if ENABLE_WATCHDOG == 1U
  /* Wait for watchdog */
  LINFO(TRACE_SYSTEM, "%s", bsp_watchdog_reboot()? "Watchdog boot" : "Clean boot");
  #endif /* ENABLE_WATCHDOG */
  LINFO(TRACE_SYSTEM, "** N6Cam.Application - v%s **", FW_VERSION);
  task_raise_event(TX_EVT_SYSTEM_READY);
}

/**
 * @brief Configure BSP.
 */
static void _system_config_bsp(void)
{
  /* Configure power */
  bsp_config_pwr();

  /* Configure system clock */
  _system_config_clocks();

  /* Update OTP fuses */
  bsp_config_fuse();

  /* Configure security */
  bsp_config_iac();
  bsp_config_risaf();

  /* Get HW ID */
  bsp_hw_id_init();

  /* Configure BSP */
  _system_config_io();
  _system_config_memory();
  _system_config_extras();

  /* Configure utilities */
  _system_config_stat();
  _system_config_trace();

  /*-->> READY <<--*/
  task_raise_event(TX_EVT_BSP_READY);
}

/**
 * @brief Configure camera HW.
 */
static void _system_config_camera(void)
{
  int32_t status;

  #if defined(STM32N6DK_C02)
  /* Don't use reset pin */
  bsp_io_init(IO_PWR_CAMERA_RST);
  bsp_io_set_state(IO_PWR_CAMERA_RST, false);
  tx_thread_sleep(100U);
  #endif /* BOARD */

  /* Enable power (active high) */
  bsp_io_init(IO_PWR_CAMERA_EN);
  bsp_io_set_state(IO_PWR_CAMERA_EN, false);
  tx_thread_sleep(200U);
  bsp_io_set_state(IO_PWR_CAMERA_EN, true);
  tx_thread_sleep(200U);

  /* Configure I2C */
  status = bsp_i2c_init(I2C_CAMERA, 100000U);
  if (status != BSP_OK)
  {
    Error_Handler();
  }
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
 *   CSI    (IC18)  PLL1  80      20     [MHz]
 *   DCMIPP (IC17)  PLL2  3      333.333 [MHz]
 *   I2C1           PCLK1 -      200     [MHz]
 *   I2C2           PCLK1 -      200     [MHz]
 *   I2C3           PCLK1 -      200     [MHz]
 *   RTC            LSE   -       32.768 [kHz]
 *   SDMMC2         HCLK  -      200     [MHz]
 *   SPI3   (IC9)   PLL1  20      80     [MHz]
 *   SPI4   (IC9)   PLL1  20      80     [MHz]
 *   SPI5   (IC9)   PLL1  20      80     [MHz]
 *   USART1 (IC14)  PLL1  16     100     [MHz]
 *   USART2 (IC14)  PLL1  16     100     [MHz]
 *   USB1           HSE   -       48     [MHz]
 *   XSPI1          HCLK  -      200     [MHz]
 *   XSPI2          HCLK  -      200     [MHz]
 *
 *   -------------------------------------------
 */
static void _system_config_clocks(void)
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
  osc.OscillatorType     = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSE;
  osc.HSEState           = RCC_HSE_ON;
  osc.LSEState           = RCC_LSE_ON;
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
  /* IC9 = PLL1 / 20 = 80MHz */
  per.ICSelection[RCC_IC9].ClockSelection   = RCC_ICCLKSOURCE_PLL1;
  per.ICSelection[RCC_IC9].ClockDivider     = 20;

  /* IC14 = PLL1 / 16 = 100MHz */
  per.ICSelection[RCC_IC14].ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  per.ICSelection[RCC_IC14].ClockDivider    = 16;

  /* IC17 = PLL2 / 3 = 333.333MHz */
  per.ICSelection[RCC_IC17].ClockSelection  = RCC_ICCLKSOURCE_PLL2;
  per.ICSelection[RCC_IC17].ClockDivider    = 3;

  /* IC18 = PLL1 / 80 = 20MHz */
  per.ICSelection[RCC_IC18].ClockSelection  = RCC_ICCLKSOURCE_PLL1;
  per.ICSelection[RCC_IC18].ClockDivider    = 80;

  /* Peripherals ------------------------------*/
  /* CSI CLK = IC18 = 20MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_CSI;

  /* DCMIPP CLK = IC17 = 333MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_DCMIPP;
  per.DcmippClockSelection    = RCC_DCMIPPCLKSOURCE_IC17;

  /* I2C1 CLK = PCLK1 = 200MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_I2C1;
  per.I2c1ClockSelection      = RCC_I2C1CLKSOURCE_PCLK1;

  /* I2C2 CLK = PCLK1 = 200MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_I2C2;
  per.I2c2ClockSelection      = RCC_I2C2CLKSOURCE_PCLK1;

  /* I2C4 CLK = PCLK1 = 200MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_I2C4;
  per.I2c4ClockSelection      = RCC_I2C4CLKSOURCE_PCLK1;

  /* RTC CLK = LSE = 32.768kHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_RTC;
  per.RTCClockSelection       = RCC_RTCCLKSOURCE_LSE;

  /* SDMMC2 CLK = HCLK = 200MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_SDMMC2;
  per.Sdmmc2ClockSelection    = RCC_SDMMC2CLKSOURCE_HCLK;

  /* SPI3 CLK = IC9 = 80MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_SPI3;
  per.Spi3ClockSelection      = RCC_SPI3CLKSOURCE_IC9;

  /* SPI4 CLK = IC9 = 80MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_SPI4;
  per.Spi4ClockSelection      = RCC_SPI4CLKSOURCE_IC9;

  /* SPI5 CLK = IC9 = 80MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_SPI5;
  per.Spi5ClockSelection      = RCC_SPI5CLKSOURCE_IC9;

  /* USART1 CLK = IC14 = 100MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_USART1;
  per.Usart1ClockSelection    = RCC_USART1CLKSOURCE_IC14;

  /* USART2 CLK = IC14 = 100MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_USART2;
  per.Usart2ClockSelection    = RCC_USART2CLKSOURCE_IC14;

  /* USB1 CLK = HSE = 48MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_USBOTGHS1;
  per.UsbOtgHs1ClockSelection = RCC_USBOTGHS1CLKSOURCE_HSE_DIRECT;
  per.PeriphClockSelection   |= RCC_PERIPHCLK_USBPHY1;
  per.UsbPhy1ClockSelection   = RCC_USBPHY1CLKSOURCE_HSE_DIRECT;

  /* XSPI1 CLK  = HCLK = 200MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_XSPI1;
  per.Xspi1ClockSelection     = RCC_XSPI1CLKSOURCE_HCLK;

  /* XSPI2 CLK  = HCLK = 200MHz */
  per.PeriphClockSelection   |= RCC_PERIPHCLK_XSPI2;
  per.Xspi2ClockSelection     = RCC_XSPI2CLKSOURCE_HCLK;

  status = HAL_RCCEx_PeriphCLKConfig(&per);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief Configure extra HW blocks.
 */
static void _system_config_extras(void)
{
  int32_t status;

  /* Initialize CRC */
  status = bsp_crc_init();
  if (status != BSP_OK)
  {
    Error_Handler();
  }

  /* Initialize RTC */
  status = bsp_rtc_init();
  if (status != BSP_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief Configure IOs.
 */
static void _system_config_io(void)
{
  int32_t status;

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

  #if defined(N6CAM)
  /* By default, turn on the power LED */
  bsp_led_on(LED_POWER);
  #endif /* N6CAM */
}

/**
 * @brief Configure memory.
 */
static void _system_config_memory(void)
{
  BSP_XSPI_NOR_Init_t nor_config = {0};
  int32_t             status;

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
  flash_init();
}

/**
 * @brief Configure sensors HW.
 */
static void _system_config_sensors(void)
{
#if (\
  ENABLE_SENSOR_TOF == 1U \
)
  /* Configure I2C
   * TODO: Adjust speed (if needed) and mode (DMA, by default).
   */
  int32_t status = bsp_i2c_init(I2C_SENSORS, 400000U);
  if (status != BSP_OK)
  {
    Error_Handler();
  }
#endif /* ENABLE_SENSOR_TOF == 1U */
}

/**
 * @brief Configure statistics utility.
 */
static void _system_config_stat(void)
{
  int32_t status;

  /* Create statistics mutex */
  status = tx_mutex_create(&_stat_mtx, "tx.mtx.stat", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize statistics */
  status = stat_init(_stat_entry, STAT_ENTRY_NUM);
  if (status != SLIB32_OK)
  {
    Error_Handler();
  }
  status = stat_init_rtos((t_rtos_lock_fn)rtos_mutex_acquire, (void*)&_stat_mtx);
  if (status != SLIB32_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief Configure tracing utility.
 */
static void _system_config_trace(void)
{
  int32_t status;

  /* Initialize UART */
  status = bsp_uart_init(TRACE_UART, 115200, false);
  if (status != BSP_OK)
  {
    Error_Handler();
  }

  /* Create tracer mutex */
  status = tx_mutex_create(&_trace_mtx, "tx.mtx.trace", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize Tracer */
  status = trace_init(bsp_uart_get_stream(TRACE_UART), _trace_out, TRACE_OUT_SIZE, TRACE_DEFAULT_LEVEL, TRACE_DEFAULT_TOPIC);
  if (status != SLIB32_OK)
  {
    Error_Handler();
  }
  status = trace_init_rtos((t_rtos_lock_fn)rtos_mutex_acquire, (void*)&_trace_mtx);
  if (status != SLIB32_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief Update USER button state.
 */
static void _system_btn_user_update(void)
{
  static bool   active = false;
  static t_led  led = {
    .uptime = 100,
    .period = 200,
  };

  /* Handle updates */
  t_btn volatile *btn = &_btn[BTN_USER1];
  if (!active && btn->update && btn->pressed)
  {
    /* Enable on press */
    btn->update = false;
    active      = true;
  }

  /* Handle active */
  if (active)
  {
    /* Short press: released before time */
    if (btn->update && !btn->pressed && ((btn->event - btn->press) < BTN_PRESS_SHORT))
    {
      active    = false;
      led.blink = 1;
      camera_cycle_color_profile();
    }
    /* Long press: pressed for more than time */
    else if (btn->pressed && tick_is_expired(btn->press, BTN_PRESS_LONG))
    {
      active = false;
      if (snapshot_trigger())
      {
        led.blink = 2;
      }
    }
    btn->update = false;
  }

  /* Run LED update */
  _system_led_update(BLINK_LED, &led);
}

/**
 * @brief Update LED state.
 * @param id LED ID
 * @param led LED instance
 */
static void _system_led_update(t_led_id id, t_led *led)
{
  switch (led->state)
  {
    case 0:   /* LED ON */
      /* Validate */
      if (led->blink == 0U)
      {
        return;
      }
      /* Start cycle */
      led->state = 1U;
      led->start = tx_time_get();
      bsp_led_on(id);
      break;

    case 1:   /* Keep ON */
      if (tick_is_expired(led->start, led->uptime))
      {
        led->state = 2U;
        bsp_led_off(id);
      }
      break;

    default:  /* Keep OFF */
      if (tick_is_expired(led->start, led->period))
      {
        led->state = 0U;
        led->blink = (led->blink > 0)? (led->blink - 1) : led->blink;
      }
      break;
  }
}

/**
 * @brief Button handler callback.
 * @param id Button ID
 * @param rising True if rising edge, false if falling edge
 */
void bsp_btn_irq_handler(t_btn_id id, bool rising)
{
  /* Handle debounce here
   * Note:
   * - Button active always on rising edge
   * - Button always use rising/falling ITs
   * - On falling edge, compute pressed time by (event - rise)
   */
  uint32_t        event = tx_time_get();
  volatile t_btn  *btn  = &_btn[id];
  if ((event - btn->event) > BTN_DEBOUNCE)
  {
    btn->update  = true;
    btn->pressed = rising;
    btn->event   = event;
    if (rising)
    {
      btn->press = event;
    }
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: System -->
*//*--------------------------------------------------------------------------*/
