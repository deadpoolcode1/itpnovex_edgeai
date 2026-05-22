/**
 ******************************************************************************
 * @file    sensor_tof_task.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the TOF sensor (VL53L7) module.
 *
 * TODO:  This is a simple implementation for testing purposes. Need to include
 *        better RTOS integration and error handling.
 *
 ******************************************************************************
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
 ******************************************************************************  
 */
#include "sensor_tof_task.h"

#if ENABLE_SENSOR_TOF == 1U
#include "n6cam_core.h"
#include "n6cam_i2c.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Sensor_TOF
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

/* Sensor configuration */
#define SENSOR_TOF_RESOLUTION       VL53L7CX_RESOLUTION_8X8
#define SENSOR_TOF_FREQ_HZ          1U                      /* For 8x8: [1;15] */
#define SENSOR_TOF_DATA_NUM         2U

/* Sensor events */
#define SENSOR_TOF_EVT_AVAILABLE    BIT(0U)   /*!< Data is available to read from sensor */
#define SENSOR_TOF_EVT_READY        BIT(1U)   /*!< Data is ready to be consumed */

/* Sensor TOF task tunables
 * TODO: Optimize stack size
 */
#define SENSOR_TOF_TASK_STACK_SIZE  1024U
#define SENSOR_TOF_TASK_PRIO        APP_PRIO_DEFAULT
#define SENSOR_TOF_TASK_TIME_SLICE  TX_NO_TIME_SLICE

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

/** Sentor TOF instance */
typedef struct
{
  bool                    ready;
  uint8_t                 alive;
  size_t                  current;
  VL53L7CX_Configuration  config;
  VL53L7CX_ResultsData    data[SENSOR_TOF_DATA_NUM];
} t_vl53l7cx;

/** Sensor TOF task handler */
typedef struct
{
  TX_EVENT_FLAGS_GROUP    evt;
  TX_THREAD               thread;
  uint8_t                 stack[SENSOR_TOF_TASK_STACK_SIZE];
} t_sensor_tof_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_vl53l7cx         _sensor_tof = {
  .config.platform.address = VL53L7CX_DEFAULT_I2C_ADDRESS,
};
static t_sensor_tof_task  _sensor_tof_task = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void    _sensor_tof_task_run(uint32_t args);
static void    _sensor_tof_task_init(void);

extern uint8_t VL53L7CX_RdByte(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t *value);
extern uint8_t VL53L7CX_WrByte(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t value);
extern uint8_t VL53L7CX_RdMulti(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t *buff, uint32_t size);
extern uint8_t VL53L7CX_WrMulti(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t *buff, uint32_t size);
extern uint8_t VL53L7CX_Reset_Sensor(VL53L7CX_Platform *ctx);
extern uint8_t VL53L7CX_WaitMs(VL53L7CX_Platform *ctx, uint32_t delay);
extern void    VL53L7CX_SwapBuffer(uint8_t *buff, uint16_t size);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t sensor_tof_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_sensor_tof_task.thread, "tx.task.sensor_tof",
    _sensor_tof_task_run, 0,
    _sensor_tof_task.stack, SENSOR_TOF_TASK_STACK_SIZE,
    SENSOR_TOF_TASK_PRIO, SENSOR_TOF_TASK_PRIO,
    SENSOR_TOF_TASK_TIME_SLICE, TX_AUTO_START
  );
}

VL53L7CX_ResultsData *sensor_tof_get_data(void)
{
  /* Validate */
  if (!_sensor_tof.ready)
  {
    return NULL;
  }

  /* Get sample */
  rtos_wait_any_event(&_sensor_tof_task.evt, SENSOR_TOF_EVT_READY, true);
  return &_sensor_tof.data[_sensor_tof.current];
}

void sensor_tof_data_available_callback(void)
{
  rtos_raise_event(&_sensor_tof_task.evt, SENSOR_TOF_EVT_AVAILABLE);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Sensor TOF task entry point.
 * @param args Task arguments
 */
static void _sensor_tof_task_run(uint32_t args)
{
  static size_t current = 0;

  UNUSED(args);

  /* Initialize */
  _sensor_tof_task_init();

  /* Sensor TOF task */
  while (1)
  {
    /* Wait for a new sample */
    rtos_wait_any_event(&_sensor_tof_task.evt, SENSOR_TOF_EVT_AVAILABLE, true);

    /* Get data */
    vl53l7cx_get_ranging_data(&_sensor_tof.config, &_sensor_tof.data[current]);
    _sensor_tof.current = current;
    current = (current + 1U) % SENSOR_TOF_DATA_NUM;
    /* Report data is available */
    rtos_raise_event(&_sensor_tof_task.evt, SENSOR_TOF_EVT_READY);
  }
}

/**
 * @brief Sensor TOF task initialization.
 */
static void _sensor_tof_task_init(void)
{
  int32_t status = 0;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_SENSOR_REQUIRE);

  /*-->> INITIALIZE <<--*/
  status = tx_event_flags_create(&_sensor_tof_task.evt, "tx.evt.sensor_tof");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Init GPIO and reset device */
  bsp_io_init(IO_VL53L7_INT);
  bsp_io_init(IO_VL53L7_LPN);
  VL53L7CX_Reset_Sensor(&_sensor_tof.config.platform);

  /* Prepare sensor */
  status = vl53l7cx_is_alive(&_sensor_tof.config, &_sensor_tof.alive);
  if (status || !_sensor_tof.alive)
  {
    LERROR(TRACE_SENSOR_TOF, "VL53L7CX not detected!");
    Error_Handler();
  }
  status = vl53l7cx_init(&_sensor_tof.config);
  if (status)
  {
    LERROR(TRACE_SENSOR_TOF, "VL53L7CX init failed!");
    Error_Handler();
  }
  status  = vl53l7cx_set_resolution(&_sensor_tof.config, VL53L7CX_RESOLUTION_8X8);
  status |= vl53l7cx_set_ranging_frequency_hz(&_sensor_tof.config, SENSOR_TOF_FREQ_HZ);
  if (status)
  {
    LERROR(TRACE_SENSOR_TOF, "VL53L7CX configuration failed!");
    Error_Handler();
  }

  /* Start ranging */
  status = vl53l7cx_start_ranging(&_sensor_tof.config);
  if (status)
  {
    LERROR(TRACE_SENSOR_TOF, "VL53L7CX start failed!");
    Error_Handler();
  }

  /*-->> READY <<--*/
  _sensor_tof.ready = true;
  LINFO(TRACE_SENSOR_TOF, "Task started");
  task_raise_event(TX_EVT_SENSOR_TOF_READY);
}

uint8_t VL53L7CX_RdByte(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t *value)
{
  return VL53L7CX_RdMulti(ctx, reg, value, 1U);
}

uint8_t VL53L7CX_WrByte(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t value)
{
  return VL53L7CX_WrMulti(ctx, reg, &value, 1U);
}

uint8_t VL53L7CX_RdMulti(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t *buff, uint32_t size)
{
  return (uint8_t)bsp_i2c_read_reg16(I2C_SENSORS, ctx->address, reg, buff, size, 1000U);
}

uint8_t VL53L7CX_WrMulti(VL53L7CX_Platform *ctx, uint16_t reg, uint8_t *buff, uint32_t size)
{
  return (uint8_t)bsp_i2c_write_reg16(I2C_SENSORS, ctx->address, reg, buff, size, 1000U);
}

uint8_t VL53L7CX_Reset_Sensor(VL53L7CX_Platform *ctx)
{
  bsp_io_set_state(IO_VL53L7_LPN, false);
  VL53L7CX_WaitMs(ctx, 100U);
  bsp_io_set_state(IO_VL53L7_LPN, true);
  VL53L7CX_WaitMs(ctx, 100U);
  return 0U;
}

uint8_t VL53L7CX_WaitMs(VL53L7CX_Platform *ctx, uint32_t delay)
{
  UNUSED(ctx);
  tx_thread_sleep(delay);
  return 0U;
}

void VL53L7CX_SwapBuffer(uint8_t *buff, uint16_t size)
{
  uint32_t  *u32_buff = (uint32_t*)buff;
  size_t    u32_size  = size / sizeof(uint32_t);
  while (u32_size-- > 0U)
  {
    *u32_buff = U32_REVERSE_BYTES(*u32_buff);
    u32_buff++;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Sensor_TOF -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_SENSOR_TOF */
