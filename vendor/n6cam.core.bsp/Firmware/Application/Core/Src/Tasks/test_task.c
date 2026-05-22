/**
 ******************************************************************************
 * @file    test_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the TEST (Built-In Self-Test) module.
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
#include "test_task.h"

#if ENABLE_TEST == 1U
#include "n6cam_core.h"
#include "n6cam_i2c.h"
#include "slib32_sysutils.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup TEST
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

/* TEST task tunables
 * TODO: Optimize stack size
 */
#define TEST_TASK_STACK_SIZE  (2U * 1024U)
#define TEST_TASK_PRIO        APP_PRIO_BACKGROUND
#define TEST_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define TEST_OK                 "OK!"
#define TEST_ERROR              "ERROR!"
#define TEST_ERROR_ID           " Cannot read ID!"
#define TEST_ERROR_REACH        " Cannot reach!"
#define TEST_FMT_START          "Test %s:"
#define TEST_FMT_ENTRY          "  %-12s "

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Test start message */
#define TEST_START(name) \
  LINFO(TRACE_TEST, TEST_FMT_START, name)

/** Test entry message */
#define TEST_ENTRY(entry, fmt, ...) \
  LINFO(TRACE_TEST, TEST_FMT_ENTRY fmt, entry, ##__VA_ARGS__)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** TEST task handler */
typedef struct
{
  TX_THREAD thread;
  uint8_t   stack[TEST_TASK_STACK_SIZE];
} t_test_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_test_task _test_task = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _test_task_run(uint32_t args);
static void _test_task_init(void);

/* Automatic */
static void _test_camera(void);
static void _test_memory(void);
static void _test_sensors(void);
static void _test_system(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t test_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_test_task.thread, "tx.task.test",
    _test_task_run, 0,
    _test_task.stack, TEST_TASK_STACK_SIZE,
    TEST_TASK_PRIO, TEST_TASK_PRIO,
    TEST_TASK_TIME_SLICE, TX_AUTO_START
  );
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief TEST task entry point.
 * @param args Task arguments
 */
static void _test_task_run(uint32_t args)
{
  UNUSED(args);

  /* Initialize */
  _test_task_init();

  /* TEST task */
  /* Automatic */
  _test_system();
  _test_memory();
  _test_camera();
  _test_sensors();

  /* Exit and set ready */
  LINFO(TRACE_TEST, "All tests completed!");
  task_raise_event(TX_EVT_TEST_READY);
}

/**
 * @brief TEST task initialization.
 */
static void _test_task_init(void)
{
  uint8_t buff[32U] = { 0 };

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_TEST_REQUIRE);
  LINFO(TRACE_TEST, "Starting Built-In Self-Test (BIST)...");

  /* Print UID */
  sysutils_get_mcu_uid_string(buff, 32U);
  LINFO(TRACE_TEST, "MCU UID: %s", (char*)buff);

  /*-->> INITIALIZE <<--*/
  /* Not required */

  /*-->> READY <<--*/
  /* Ready on task exit */
}

/*-->> Automatic <<----------------------------*/
/**
 * @brief Test camera is reachable
 */
static void _test_camera(void)
{
  uint32_t  id    = 0;
  uint8_t   ready = 0;
  int32_t   status;

  TEST_START("CAMERA");

  #if defined(STM32N6DK_C02)
  /* Enable camera */
  bsp_io_init(IO_PWR_CAMERA_RST);
  bsp_io_set_state(IO_PWR_CAMERA_RST, false);
  tx_thread_sleep(100U);
  #endif /* BOARD */

  /* Enable camera power (active high) */
  bsp_io_init(IO_PWR_CAMERA_EN);
  bsp_io_set_state(IO_PWR_CAMERA_EN, false);
  tx_thread_sleep(200U);
  bsp_io_set_state(IO_PWR_CAMERA_EN, true);
  tx_thread_sleep(200U);

  /* Initialize camera I2C */
  status = bsp_i2c_init(I2C_CAMERA, 100000U);
  if (status != BSP_OK)
  {
    TEST_ENTRY("I2C_INIT", TEST_ERROR);
    Error_Handler();
  }

  /* Check camera ---------*/
  /* IMX335
   * - Addr8b : 0x34U
   * - ID_REG : 0x3912U
   * - ID     : 0x00U
   */
  status = bsp_i2c_read_reg16(I2C_CAMERA, 0x34U, 0x3912U, (uint8_t*)&id, 1U, 1000U);
  if ((status == BSP_OK) && (id == 0x00U))
  {
    TEST_ENTRY("SENSOR", "IMX335");
    ready = 1;
  }

  /* VD55G1
   * - Addr8b : 0x20U
   * - ID_REG : 0x0000U
   * - ID     : 0x53354731U
   */
  status = bsp_i2c_read_reg16(I2C_CAMERA, 0x20U, 0x0000U, (uint8_t*)&id, 4U, 1000U);
  if ((status == BSP_OK) && (id == 0x53354731U))
  {
    TEST_ENTRY("SENSOR", "VD55G1");
    ready = 1;
  }

  /* VD66GY
   * - Addr8b : 0x20U
   * - ID_REG : 0x0000U
   * - ID     : 0x31105603U
   */
  status = bsp_i2c_read_reg16(I2C_CAMERA, 0x20U, 0x0000U, (uint8_t*)&id, 4U, 1000U);
  if ((status == BSP_OK) && (id == 0x31105603U))
  {
    TEST_ENTRY("SENSOR", "VD66GY");
    ready = 1;
  }

  /* VD1943 (Maneki)
   * - Addr8b : 0x20U
   * - ID_REG : 0x0000U
   * - ID     : 0x53393430U > v1.3
   *            0x53393431U > v1.4
   */
  status = bsp_i2c_read_reg16(I2C_CAMERA, 0x20U, 0x0000U, (uint8_t*)&id, 4U, 1000U);
  if ((status == BSP_OK) && ((id == 0x53393430U) || (id == 0x53393431U) || (id == 0x53393432U)))
  {
    TEST_ENTRY("SENSOR", "VD1943");
    ready = 1;
  }

  /* Check camera ready */
  if (ready == 0)
  {
    TEST_ENTRY("SENSOR", TEST_ERROR TEST_ERROR_ID);
    Error_Handler();
  }

  /* Deinit camera I2C */
  status = bsp_i2c_deinit(I2C_CAMERA);
  if (status != BSP_OK)
  {
    TEST_ENTRY("I2C_DEINIT", "NOT FOUND!");
    Error_Handler();
  }
  TEST_ENTRY("CAMERA", TEST_OK);

  /* Turn OFF camera power */
  bsp_io_set_state(IO_PWR_CAMERA_EN, false);
  bsp_io_deinit(IO_PWR_CAMERA_EN);

  #if defined(STM32N6DK_C02)
  /* Disable camera */
  bsp_io_set_state(IO_PWR_CAMERA_RST, true);
  bsp_io_deinit(IO_PWR_CAMERA_RST);
  #endif /* BOARD */
}

/**
 * @brief Test access to external memory: SDRAM, FLASH
 */
static void _test_memory(void)
{
  TEST_START("MEMORIES");

  /* XSPI1 - SDRAM - RW */
  uint32_t sdram_test = 0xDEADBEEF;
  uint32_t *sdram_ptr = (uint32_t*)XSPI1_BASE;
  *sdram_ptr = sdram_test;
  if (*sdram_ptr != sdram_test)
  {
    TEST_ENTRY("SDRAM R/W", TEST_ERROR);
    Error_Handler();
  }
  TEST_ENTRY("SDRAM", TEST_OK);

  /* XSPI2 - FLASH - R */
  /* This assumes the FSBL is signed and flashed */
  uint8_t flash_test[3U] = "STM";
  uint8_t *flash_ptr = (uint8_t*)XSPI2_BASE;
  if (memcmp(flash_ptr, flash_test, 3U) != 0)
  {
    TEST_ENTRY("FLASH R", TEST_ERROR);
    Error_Handler();
  }
  TEST_ENTRY("FLASH", TEST_OK);
}

/**
 * @brief Test sensors are reachable: IMU, Presence, TOF
 */
static void _test_sensors(void)
{
  #if defined(N6CAM)
  uint8_t id;
  int32_t status;

  TEST_START("SENSORS");

  /* Initialize sensors I2C */
  status = bsp_i2c_init(I2C_SENSORS, 100000U);
  if (status != BSP_OK)
  {
    TEST_ENTRY("I2C_INIT", TEST_ERROR);
    Error_Handler();
  }

  /* Check sensors --------*/
  /* IMU-6DOF: LSM6DO32
   * - Addr8b : 0xD7
   * - ID_REG : 0x0F
   * - ID     : 0x6C
   */
  status = bsp_i2c_read_reg8(I2C_SENSORS, 0xD7, 0x0F, &id, 1U, 1000U);
  if ((status != BSP_OK) || (id != 0x6C))
  {
    TEST_ENTRY("6DOF", TEST_ERROR TEST_ERROR_ID);
    Error_Handler();
  }
  TEST_ENTRY("6DOF", TEST_OK);

  /* Presence: STHS34PF80
   * - Addr8b : 0xB5
   * - ID_REG : 0x0F
   * - ID     : 0xD3
   */
  status = bsp_i2c_read_reg8(I2C_SENSORS, 0xB5, 0x0F, &id, 1U, 1000U);
  if ((status != BSP_OK) || (id != 0xD3))
  {
    TEST_ENTRY("Presence", TEST_ERROR TEST_ERROR_ID);
    Error_Handler();
  }
  TEST_ENTRY("Presence", TEST_OK);

  /* TOF: VL53L7CHV0GC1
   * - Addr8b : 0x52
   * - ID_REG : -
   * - ID     : -
   */
  status = bsp_i2c_is_ready(I2C_SENSORS, 0x52, 1, 1000);
  if (status != BSP_OK)
  {
    TEST_ENTRY("TOF", TEST_ERROR TEST_ERROR_REACH);
    Error_Handler();
  }
  TEST_ENTRY("TOF", TEST_OK);

  /* Deinit I2C */
  status = bsp_i2c_deinit(I2C_SENSORS);
  if (status != BSP_OK)
  {
    TEST_ENTRY("I2C_DEINIT", TEST_ERROR);
    Error_Handler();
  }
  #endif /* BOARD */
}

/**
 * @brief Test system initialization: HSE
 */
static void _test_system(void)
{
  TEST_START("SYSTEM");

  /* Check HSE */
  if (((RCC->CR & RCC_CCR_HSEONC_Msk) == 0U) || ((RCC->SR & RCC_SR_HSERDY_Msk) == 0U))
  {
    TEST_ENTRY("HSE_CLK", TEST_ERROR);
    Error_Handler();
  }
  TEST_ENTRY("HSE_CLK", TEST_OK);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: TEST -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_TEST */
