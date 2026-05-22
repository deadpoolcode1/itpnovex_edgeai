/**
 ******************************************************************************
 * @file    camera_task.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the camera module.
 *          This module is responsible for:
 *          - Initialize camera and capture buffers
 *          - Provide easy access to capture buffers
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
#include "camera_task.h"
#include "isp_param_conf.h"
#include "isp_services.h"
#include "n6cam_core.h"
#include "n6cam_i2c.h"
#ifndef ISP_MW_TUNING_TOOL_SUPPORT
#include "registry_task.h"
#endif /* !ISP_MW_TUNING_TOOL_SUPPORT */
#include "slib32_core.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Camera
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

/* Camera task tunables
 * TODO: Optimize stack size
 */
#define CAMERA_TASK_STACK_SIZE      (3U * 1024U)
#define CAMERA_TASK_PRIO            APP_PRIO_IMPORTANT
#define CAMERA_TASK_TIME_SLICE      APP_TIME_SLICE_DEFAULT

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#ifdef ISP_MW_TUNING_TOOL_SUPPORT
  #define CAMERA_DUMP_TIMEOUT     1000U
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */

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

/** Camera task handler */
typedef struct
{
  TX_EVENT_FLAGS_GROUP  evt;
  TX_THREAD             thread;
  uint8_t               stack[CAMERA_TASK_STACK_SIZE];
} t_camera_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/* Task handler */
static t_camera_task        _camera_task = { 0 };

/* Internals */
static ISP_StatAreaTypeDef  _camera_stat_area;
static uint8_t              _camera_decimation = 1U;
static uint8_t              _camera_main_buffer[CAMERA_MAIN_BUFFER_NUM][CAMERA_MAIN_BUFFER_SIZE] DMA_ALIGN IN_PSRAM;
static uint8_t              _camera_ancillary_buffer[CAMERA_ANCILLARY_BUFFER_NUM][CAMERA_ANCILLARY_BUFFER_SIZE] DMA_ALIGN IN_PSRAM;

/* Peripheral */
DCMIPP_HandleTypeDef  hdcmipp;
ISP_HandleTypeDef     hisp;

/* Global */
uint32_t              camera_awb_idx = 0;
uint32_t              camera_awb_max = 0;

t_camera              camera = {
  .sensor = {
    .fps    = CAMERA_SENSOR_FPS,
  },
  .dump = {
    .id     = DCMIPP_PIPE0,
  },
  .main = {
    .id     = DCMIPP_PIPE1,
    .bpp    = CAMERA_MAIN_BPP,
    .width  = CAMERA_MAIN_WIDTH,
    .height = CAMERA_MAIN_HEIGHT,
    .packer = CAMERA_MAIN_PACKER,
    .gamma  = CAMERA_MAIN_GAMMA,
    .rbswap = CAMERA_MAIN_RBSWAP,
  },
  .ancillary = {
    .id     = DCMIPP_PIPE2,
    .bpp    = CAMERA_ANCILLARY_BPP,
    .width  = CAMERA_ANCILLARY_WIDTH,
    .height = CAMERA_ANCILLARY_HEIGHT,
    .packer = CAMERA_ANCILLARY_PACKER,
    .gamma  = CAMERA_ANCILLARY_GAMMA,
    .rbswap = CAMERA_ANCILLARY_RBSWAP,
  },
};

t_camera_preview      camera_preview = { 0 };

const char            *camera_name[CAMERA_SENSOR_MAX] = {
  "Unknown",
  "IMX335",
  "VD55G1",
  "VD66GY",
  "VD1943",
};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void    _camera_task_run(uint32_t args);
static void    _camera_task_init(void);
static int32_t _camera_sensor_init(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers);
static int32_t _camera_sensor_probe(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers);

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
static void    _camera_init_registry(void);
#endif /* !ISP_MW_TUNING_TOOL_SUPPORT */

/*-->> UPDATE <<---------------------*/
static void _camera_preview_update(void);

#ifdef ISP_MW_TUNING_TOOL_SUPPORT
static void _camera_stat_area_update(void);
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */

/*-->> PIPE <<-----------------------*/
static ISP_StatusTypeDef _camera_pipe_configure_ipplug(DCMIPP_HandleTypeDef *phdcmipp, uint32_t p0_wdt, uint32_t p0_bpp, uint32_t p1_wdt, uint32_t p1_bpp, uint32_t p2_wdt, uint32_t p2_bpp);
static ISP_StatusTypeDef _camera_pipe_configure(DCMIPP_HandleTypeDef *phdcmipp, ISP_DumpCfgTypeDef dump, uint32_t pipe);
static ISP_StatusTypeDef _camera_pipe_start(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe);
static ISP_StatusTypeDef _camera_pipe_stop(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe);
static ISP_StatusTypeDef _camera_pipe_suspend(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe);
static ISP_StatusTypeDef _camera_pipe_resume(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe);

static ISP_StatusTypeDef _camera_pipe_get_dump_configuration(DCMIPP_PipeConfTypeDef *config, ISP_DumpCfgTypeDef dump);
static ISP_StatusTypeDef _camera_pipe_get_main_configuration(DCMIPP_PipeConfTypeDef *config, ISP_DumpCfgTypeDef dump);
static ISP_StatusTypeDef _camera_pipe_get_ancillary_configuration(DCMIPP_PipeConfTypeDef *config, ISP_DumpCfgTypeDef dump);

static int32_t           _camera_pipe_get_decimation_ratio(float *ratio, uint8_t is_vertical);
static ISP_StatusTypeDef _camera_pipe_get_scale_config(DCMIPP_CropConfTypeDef *crop, DCMIPP_DecimationConfTypeDef *dec, DCMIPP_DownsizeTypeDef *down, uint32_t pipe);

/*-->> IQTune <<---------------------*/
#ifdef ISP_MW_TUNING_TOOL_SUPPORT
static ISP_StatusTypeDef _camera_dump_frame(void *phdcmipp, uint32_t pipe, ISP_DumpCfgTypeDef config, uint32_t **buffer, ISP_DumpFrameMetaTypeDef *meta);
static ISP_StatusTypeDef _camera_dump_frame_internal(void *phdcmipp, uint32_t pipe, ISP_DumpCfgTypeDef config, uint32_t **buffer, ISP_DumpFrameMetaTypeDef *meta);
static ISP_StatusTypeDef _camera_preview_start(void *phdcmipp);
static ISP_StatusTypeDef _camera_preview_stop(void *phdcmipp);
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */

/*-->> BUS <<------------------------*/
static int32_t _camera_bus_init(void);
static int32_t _camera_bus_deinit(void);
static int32_t _camera_bus_read(uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size);
static int32_t _camera_bus_write(uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size);

/*-->> CTRL <<-----------------------*/
static void _camera_ctrl_power(bool value);

/*-->> DCMIPP <<---------------------*/
static void DCMIPP_MspInit(DCMIPP_HandleTypeDef *phdcmipp);
static void DCMIPP_MspDeInit(DCMIPP_HandleTypeDef *phdcmipp);

extern void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe);
extern void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe);

/*-->> Sensors <<--------------------*/
#ifdef USE_IMX335_SENSOR
extern int32_t camera_probe_imx335(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers);
#endif /* USE_IMX335_SENSOR */
#ifdef USE_VD55G1_SENSOR
extern int32_t camera_probe_vd55g1(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers);
#endif /* USE_VD55G1_SENSOR */
#ifdef USE_VD66GY_SENSOR
extern int32_t camera_probe_vd66gy(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers);
#endif /* USE_VD66GY_SENSOR */
#ifdef USE_VD1943_SENSOR
extern int32_t camera_probe_vd1943(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers);
#endif /* USE_VD1943_SENSOR */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t camera_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_camera_task.thread, "tx.task.camera",
    _camera_task_run, 0,
    _camera_task.stack, CAMERA_TASK_STACK_SIZE,
    CAMERA_TASK_PRIO, CAMERA_TASK_PRIO,
    CAMERA_TASK_TIME_SLICE, TX_AUTO_START
  );
}

uint8_t *camera_get_buffer(ULONG pipe)
{
  switch (pipe)
  {
    case DCMIPP_PIPE0: return (uint8_t*)DCMIPP->P0STM0AR;
    case DCMIPP_PIPE1: return (uint8_t*)DCMIPP->P1STM0AR;
    case DCMIPP_PIPE2: return (uint8_t*)DCMIPP->P2STM0AR;
    default:           return NULL;
  }
}

void camera_wait_event(ULONG mask, bool clear)
{
  rtos_wait_any_event(&_camera_task.evt, mask, clear);
}

/*---------------------------------------------*/
bool camera_use_isp(void)
{
  return (bool)camera.sensor.use_isp;
}

const char* camera_get_name(void)
{
  return camera_name[camera.sensor.id];
}

void camera_cycle_color_profile(void)
{
  ISP_IQParamTypeDef *config;
  uint16_t           next;

  /* Update color profile */
  if (camera_use_isp())
  {
    /* Change AWB profile */
    config = ISP_SVC_IQParam_Get(&hisp);
    next   = config->AWBAlgo.enable ? 0U : (uint16_t)(camera_awb_idx + 1U);
    if (next > camera_awb_max)
    {
      camera_set_awb(1U, 0U);
    }
    else
    {
      camera_set_awb(0U, next);
    }
  }
  else
  {
    /* Change brightness */
    camera_get_brightness(&next);
    next = (uint16_t)(next + CAMERA_BRIGHTNESS_STEP);
    next = (next > CAMERA_BRIGHTNESS_MAX) ? CAMERA_BRIGHTNESS_MIN : next;
    camera_set_brightness(next);
  }
}

int32_t camera_get_flip(uint8_t *flip)
{
  /* If function not set, not supported */
  if (camera.sensor.ctrl.get_flip)
  {
    return (camera.sensor.ctrl.get_flip(flip) == BSP_OK)? 0 : -1;
  }
  return -3;
}

int32_t camera_set_flip(uint8_t flip)
{
  int32_t status = 0;

  /* If function not set, not supported */
  if (camera.sensor.ctrl.set_flip)
  {
    status = camera.sensor.ctrl.set_flip(flip);
    if (status != BSP_OK)
    {
      return -1;
    }

    #ifndef ISP_MW_TUNING_TOOL_SUPPORT
    /* Acquire registry */
    t_registry_data *registry = registry_acquire();
    if (!registry)
    {
      LERROR(TRACE_SHELL, "Registry not available");
      Error_Handler();
    }

    /* Save if required */
    if (registry->camera_flip != flip)
    {
      registry->camera_flip = flip;
      registry_request_save();
    }
    else
    {
      /* No update required */
      status = -2;
    }

    /* Release registry */
    registry_release();
    return status;

    #endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  }
  return -3;
}

int32_t camera_get_aec(ISP_AECAlgoTypeDef* value)
{
  ISP_IQParamTypeDef *config;

  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Get AEC */
  config = ISP_SVC_IQParam_Get(&hisp);
  memcpy(value, &config->AECAlgo, sizeof(ISP_AECAlgoTypeDef));
  return ISP_OK;
}

int32_t camera_set_aec(ISP_AECAlgoTypeDef* value)
{
  ISP_IQParamTypeDef  *config;
  int32_t             status = 0;

  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Set AEC */
  config = ISP_SVC_IQParam_Get(&hisp);
  memcpy(&config->AECAlgo, value, sizeof(ISP_AECAlgoTypeDef));

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
  ISP_SensorGainTypeDef     gain;
  ISP_SensorExposureTypeDef exposure;

  /* Get gain/exposure */
  ISP_SVC_Sensor_GetGain(&hisp, &gain);
  ISP_SVC_Sensor_GetExposure(&hisp, &exposure);

  /* Acquire registry */
  t_registry_data *registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_SHELL, "Registry not available");
    Error_Handler();
  }

  /* Save if required */
  if (
    (value->enable != registry->camera_aec_enable) ||
    (value->exposureCompensation != registry->camera_aec_ev) ||
    (gain.gain != registry->camera_gain) ||
    (exposure.exposure != registry->camera_exposure)
  )
  {
    /* Apply changes */
    registry->camera_aec_enable = value->enable;
    registry->camera_aec_ev     = value->exposureCompensation;
    registry->camera_gain       = gain.gain;
    registry->camera_exposure   = exposure.exposure;
    registry_request_save();
  }
  else
  {
    /* No update required */
    status = -2;
  }

  /* Release registry */
  registry_release();

#endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  return status;
}

int32_t camera_get_awb(ISP_AWBAlgoTypeDef *value, uint8_t *size, uint8_t *idx)
{
  ISP_IQParamTypeDef *config;

  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Get AWB */
  config = ISP_SVC_IQParam_Get(&hisp);
  memcpy(value, &config->AWBAlgo, sizeof(ISP_AWBAlgoTypeDef));
  if (size)
  {
    *size = (uint8_t)camera_awb_max;
  }
  if (idx)
  {
    *idx  = (uint8_t)camera_awb_idx;
  }
  return ISP_OK;
}

int32_t camera_set_awb(uint8_t enable, uint8_t idx)
{
  ISP_IQParamTypeDef *config;
  int32_t            status = 0;

  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Configure */
  config = ISP_SVC_IQParam_Get(&hisp);
  enable = (enable != 0U)? ISP_AWB_ENABLE_RECONFIGURE : 0U;
  if (enable == 0U)
  {
    /* Validate index */
    if (idx > camera_awb_max)
    {
      return -1;
    }

    /* Configure ISP */
    camera_awb_idx                  = idx;
    config->ispGainStatic.enable    = 1U;
    config->ispGainStatic.ispGainR  = config->AWBAlgo.ispGainR[idx];
    config->ispGainStatic.ispGainG  = config->AWBAlgo.ispGainG[idx];
    config->ispGainStatic.ispGainB  = config->AWBAlgo.ispGainB[idx];
    config->colorConvStatic.enable  = 1U;
    memcpy(config->colorConvStatic.coeff, config->AWBAlgo.coeff[idx], sizeof(config->colorConvStatic.coeff));
    ISP_SVC_ISP_SetGain(&hisp, &config->ispGainStatic);
    ISP_SVC_ISP_SetColorConv(&hisp, &config->colorConvStatic);
  }
  config->AWBAlgo.enable = enable;

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
  /* Acquire registry */
  t_registry_data *registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_SHELL, "Registry not available");
    Error_Handler();
  }

  /* Save if required */
  if (
    (enable != registry->camera_awb_enable) ||
    (idx != registry->camera_awb_profile)
  )
  {
    registry->camera_awb_enable  = enable;
    registry->camera_awb_profile = idx;
    registry_request_save();
  }
  else
  {
    /* No update required */
    status = -2;
  }

  /* Release registry */
  registry_release();
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  return status;
}

int32_t camera_get_gain(ISP_SensorGainTypeDef* value)
{
  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Get gain */
  return (int32_t)ISP_SVC_Sensor_GetGain(&hisp, value);
}

int32_t camera_set_gain(ISP_SensorGainTypeDef* value)
{
  ISP_IQParamTypeDef *config;
  int32_t            status;

  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Set gain and update ISP */
  config = ISP_SVC_IQParam_Get(&hisp);
  status = ISP_SVC_Sensor_SetGain(&hisp, value);
  if (status != ISP_OK)
  {
    return -1;
  }
  config->sensorGainStatic = *value;

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
  /* Update registry (only if AEC is disabled) */
  if (!config->AECAlgo.enable)
  {
    /* Acquire registry */
    t_registry_data *registry = registry_acquire();
    if (!registry)
    {
      LERROR(TRACE_SHELL, "Registry not available");
      Error_Handler();
    }

    /* Save if required */
    if (registry->camera_gain != config->sensorGainStatic.gain)
    {
      registry->camera_gain = config->sensorGainStatic.gain;
      registry_request_save();
    }
    else
    {
      /* No update required */
      status = -2;
    }

    /* Release registry */
    registry_release();
  }
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  return status;
}

int32_t camera_get_exposure(ISP_SensorExposureTypeDef* value)
{
  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Get exposure */
  return (int32_t)ISP_SVC_Sensor_GetExposure(&hisp, value);
}

int32_t camera_set_exposure(ISP_SensorExposureTypeDef* value)
{
  ISP_IQParamTypeDef *config;
  int32_t            status;

  /* Validate */
  if (!camera_use_isp())
  {
    /* Not supported */
    return -3;
  }

  /* Set exposure and update ISP */
  config = ISP_SVC_IQParam_Get(&hisp);
  status = ISP_SVC_Sensor_SetExposure(&hisp, value);
  if (status != ISP_OK)
  {
    return -1;
  }
  config->sensorExposureStatic = *value;

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
  /* Update registry (only if AEC is disabled) */
  if (!config->AECAlgo.enable)
  {
    /* Acquire registry */
    t_registry_data *registry = registry_acquire();
    if (!registry)
    {
      LERROR(TRACE_SHELL, "Registry not available");
      Error_Handler();
    }

    /* Save if required */
    if (registry->camera_exposure != config->sensorExposureStatic.exposure)
    {
      registry->camera_exposure = config->sensorExposureStatic.exposure;
      registry_request_save();
    }
    else
    {
      /* No update required */
      status = -2;
    }

    /* Release registry */
    registry_release();
  }
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  return status;
}

int32_t camera_get_brightness(uint16_t *value)
{
  /* If function not set, not supported */
  if (camera.sensor.ctrl.get_brightness)
  {
    return (camera.sensor.ctrl.get_brightness(value) == BSP_OK)? 0 : -1;
  }
  return -3;
}

int32_t camera_set_brightness(uint16_t value)
{
  int32_t status = 0;

  /* If function not set, not supported */
  if (camera.sensor.ctrl.set_brightness)
  {
    status = camera.sensor.ctrl.set_brightness(value);
    if (status != BSP_OK)
    {
      return -1;
    }

    #ifndef ISP_MW_TUNING_TOOL_SUPPORT
    /* Acquire registry */
    t_registry_data *registry = registry_acquire();
    if (!registry)
    {
      LERROR(TRACE_SHELL, "Registry not available");
      Error_Handler();
    }

    /* Save if required */
    if (registry->camera_brightness != value)
    {
      registry->camera_brightness = value;
      registry_request_save();
    }
    else
    {
      /* No update required */
      status = -2;
    }

    /* Release registry */
    registry_release();
    return status;

    #endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  }
  return -3;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Camera task entry point.
 * @param args Task arguments
 */
static void _camera_task_run(uint32_t args)
{
  int32_t status;

  UNUSED(args);

  /* Initialize task */
  _camera_task_init();

  /* Camera task */
  while (1)
  {
    #ifdef ISP_MW_TUNING_TOOL_SUPPORT
    /* Run continuously */
    tx_thread_sleep(1);
    #else
    /* Wait VSync */
    rtos_wait_any_event(&_camera_task.evt, CAMERA_EVT_VSYNC, true);
    #endif /* ISP_MW_TUNING_TOOL_SUPPORT */

    /* Run ISP (if supported) */
    if (!camera_use_isp())
    {
      continue;
    }
    status = ISP_BackgroundProcess(&hisp);
    switch (status)
    {
      case ISP_ERR_SENSORGAIN:
        ISP_SensorGainTypeDef gain = { .gain = 10000U, };
        camera_set_gain(&gain);
        break;

      case ISP_ERR_SENSOREXPOSURE:
        ISP_SensorExposureTypeDef exposure = { .exposure = 10000U, };
        camera_set_exposure(&exposure);
        break;

      default:
        if (status != ISP_OK)
        {
          LWARNING(TRACE_CAMERA, "ISP Background process error %d (%s)", status, ERROR_MESSAGE(status));
        }
        break;
    }

    #ifdef ISP_MW_TUNING_TOOL_SUPPORT
    /* Update stat area */
    _camera_stat_area_update();
    #endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  }
}

/**
 * @brief Camera task initialization.
 */
static void _camera_task_init(void)
{
  int32_t                 status;
  ISP_AppliHelpersTypeDef isp_helpers;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_SENSOR_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Prepare events */
  status = tx_event_flags_create(&_camera_task.evt, "tx.evt.camera");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize DCMIPP */
  hdcmipp.Instance = DCMIPP;
  DCMIPP_MspInit(&hdcmipp);
  status = HAL_DCMIPP_Init(&hdcmipp);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't init DCMIPP");
    Error_Handler();
  }

  /* Initialize camera */
  status = _camera_sensor_init(&camera.sensor, &isp_helpers);
  if (status != 0x00)
  {
    LERROR(TRACE_CAMERA, "Can't configure camera");
    Error_Handler();
  }
  #ifdef ISP_MW_TUNING_TOOL_SUPPORT
  if (!camera_use_isp())
  {
    LERROR(TRACE_CAMERA, "Sensor not supported by ISP!");
    Error_Handler();
  }
  #endif /* ISP_MW_TUNING_TOOL_SUPPORT */

  /* Configure stat area (at start, half size of frame, centered) */
  _camera_stat_area.X0    = camera.sensor.width  / 4U;
  _camera_stat_area.Y0    = camera.sensor.height / 4U;
  _camera_stat_area.XSize = camera.sensor.width  / 2U;
  _camera_stat_area.YSize = camera.sensor.height / 2U;

  /* Propagate preview settings */
  _camera_preview_update();

  /* Include IQTune helpers */
  #ifdef ISP_MW_TUNING_TOOL_SUPPORT
  isp_helpers.DumpFrame     = _camera_dump_frame;
  isp_helpers.StartPreview  = _camera_preview_start;
  isp_helpers.StopPreview   = _camera_preview_stop;
  #endif /* ISP_MW_TUNING_TOOL_SUPPORT */

  /* Initialize ISP */
  if (camera_use_isp())
  {
    status = ISP_Init(&hisp, &hdcmipp, 0, &isp_helpers, ISP_IQParamCacheInit[camera.sensor.id]);
    if (status != ISP_OK)
    {
      LERROR(TRACE_CAMERA, "Can't init ISP (error %d)", status);
      Error_Handler();
    }
    status = ISP_SetStatArea(&hisp, &_camera_stat_area);
    if (status != ISP_OK)
    {
      LERROR(TRACE_CAMERA, "Can't set stat area (error %d)", status);
      Error_Handler();
    }
  }

  /* Adjust IPP */
  #ifdef ISP_MW_TUNING_TOOL_SUPPORT
  status = _camera_pipe_configure_ipplug(&hdcmipp, 0, 0, camera.main.width, camera.main.bpp, 0, 0);
  #else
  status = _camera_pipe_configure_ipplug(&hdcmipp, 0, 0, camera.main.width, camera.main.bpp, camera.ancillary.width, camera.ancillary.bpp);
  #endif /* ISP_MW_TUNING_TOOL_SUPPORT */
  if (status != ISP_OK)
  {
    LERROR(TRACE_CAMERA, "Failed to configure IPPPlug (error %d)", status);
    Error_Handler();
  }

  /* Start pipes */
  status = _camera_pipe_start(&hdcmipp, camera.main.id);
  if (status != ISP_OK)
  {
    LERROR(TRACE_CAMERA, "Failed to start camera (PIPE1)");
    Error_Handler();
  }
  #ifndef ISP_MW_TUNING_TOOL_SUPPORT
  status = _camera_pipe_start(&hdcmipp, camera.ancillary.id);
  if (status != ISP_OK)
  {
    LERROR(TRACE_CAMERA, "Failed to start camera (PIPE2)");
    Error_Handler();
  }
  #endif /* !ISP_MW_TUNING_TOOL_SUPPORT */

  #ifndef ISP_MW_TUNING_TOOL_SUPPORT
  /* Configure */
  _camera_init_registry();
  #endif /* !ISP_MW_TUNING_TOOL_SUPPORT */

  /* Configure and start ISP */
  if (camera_use_isp())
  {
    /* Start */
    status = ISP_Start(&hisp);
    if (status != ISP_OK)
    {
      LERROR(TRACE_CAMERA, "Can't start ISP (error %d)", status);
      Error_Handler();
    }
  }

  /*-->> READY <<--*/
  LINFO(TRACE_CAMERA, "Task started (%s)", camera_get_name());
  task_raise_event(TX_EVT_CAMERA_READY);
}

static int32_t _camera_sensor_init(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers)
{
  HAL_StatusTypeDef status;
  DCMIPP_CSI_ConfTypeDef csi_config = {
    .DataLaneMapping= DCMIPP_CSI_PHYSICAL_DATA_LANES,
  };
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_config = {
    .DataTypeMode   = DCMIPP_DTMODE_DTIDA,
    .DataTypeIDB    = 0,
  };

  /* Configure */
  camera.sensor.bus.init   = _camera_bus_init;
  camera.sensor.bus.deinit = _camera_bus_deinit;
  camera.sensor.bus.read   = _camera_bus_read;
  camera.sensor.bus.write  = _camera_bus_write;
  camera.sensor.ctrl.power = _camera_ctrl_power;

  /* Initialize sensor */
  status = _camera_sensor_probe(sensor, helpers);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't initialize sensor");
    return HAL_ERROR;
  }
  /* Set dump pipe */
  camera.dump.bpp    = sensor->bpp;
  camera.dump.width  = sensor->width;
  camera.dump.height = sensor->height;

  #ifdef ISP_MW_TUNING_TOOL_SUPPORT
  /* On IQTune, dump and ancillary have the same size */
  camera.ancillary.width  = camera.dump.width;
  camera.ancillary.height = camera.dump.height;
  #endif /* ISP_MW_TUNING_TOOL_SUPPORT */

  /* Validate pipes */
  if (
    (camera.main.width > camera.sensor.width) || (camera.main.height > camera.sensor.height) ||
    (camera.ancillary.width > camera.sensor.width) || (camera.ancillary.height > camera.sensor.height)
  )
  {
    LERROR(TRACE_CAMERA, "Pipe size exceeds sensor size");
    return HAL_ERROR;
  }

  /* Configure CSI */
  csi_config.NumberOfLanes    = camera.sensor.csi_lanes;
  csi_config.PHYBitrate       = camera.sensor.csi_brate;
  csi_pipe_config.DataTypeIDA = camera.sensor.csi_dtype;
  status = HAL_DCMIPP_CSI_SetConfig(&hdcmipp, &csi_config);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure CSI");
    return HAL_ERROR;
  }
  status = HAL_DCMIPP_CSI_SetVCConfig(&hdcmipp, DCMIPP_VIRTUAL_CHANNEL0, camera.sensor.csi_dfmt);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure CSI virtual channel");
    return HAL_ERROR;
  }

  /* Pre-initialize CSI for all pipes */
  for (uint8_t pipe = DCMIPP_PIPE0; pipe <= DCMIPP_PIPE2; pipe++)
  {
    status = HAL_DCMIPP_CSI_PIPE_SetConfig(&hdcmipp, pipe, &csi_pipe_config);
    if (status != HAL_OK)
    {
      LERROR(TRACE_CAMERA, "Can't configure pipe (%d) CSI", pipe);
      return HAL_ERROR;
    }
  }

  /* Assume ISP share is required */
  status = HAL_DCMIPP_PIPE_CSI_EnableShare(&hdcmipp, DCMIPP_PIPE2);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't enable ISP share");
    return HAL_ERROR;
  }

  /* Start sensor */
  if (!camera.sensor.ctrl.start)
  {
    LERROR(TRACE_CAMERA, "Sensor start function not defined");
    return HAL_ERROR;
  }
  status = camera.sensor.ctrl.start();
  if (status != BSP_OK)
  {
    LERROR(TRACE_CAMERA, "Can't start sensor");
    return HAL_ERROR;
  }

  /* Configure pipes */
  status = _camera_pipe_configure(&hdcmipp, ISP_DUMP_CFG_DEFAULT, DCMIPP_PIPE0);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure dump pipe");
    return HAL_ERROR;
  }
  status = _camera_pipe_configure(&hdcmipp, ISP_DUMP_CFG_DEFAULT, DCMIPP_PIPE1);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure main pipe");
    return HAL_ERROR;
  }
  status = _camera_pipe_configure(&hdcmipp, ISP_DUMP_CFG_DEFAULT, DCMIPP_PIPE2);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure ancillary pipe");
    return HAL_ERROR;
  }

  return HAL_OK;
}

static int32_t _camera_sensor_probe(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers)
{
  int32_t status;

  /* Probe sensor */
  #ifdef USE_IMX335_SENSOR
  status = camera_probe_imx335(sensor, helpers);
  if (status == 0x00)
  {
    return HAL_OK;
  }
  #endif /* USE_IMX335_SENSOR */
  #ifdef USE_VD55G1_SENSOR
  status = camera_probe_vd55g1(sensor, helpers);
  if (status == 0x00)
  {
    return HAL_OK;
  }
  #endif /* USE_VD55G1_SENSOR */
  #ifdef USE_VD66GY_SENSOR
  status = camera_probe_vd66gy(sensor, helpers);
  if (status == 0x00)
  {
    return HAL_OK;
  }
  #endif /* USE_VD66GY_SENSOR */
  #ifdef USE_VD1943_SENSOR
  status = camera_probe_vd1943(sensor, helpers);
  if (status == 0x00)
  {
    return HAL_OK;
  }
  #endif /* USE_VD1943_SENSOR */

  /* No sensor found */
  return HAL_ERROR;
}

#ifndef ISP_MW_TUNING_TOOL_SUPPORT
static void _camera_init_registry(void)
{
  ISP_IQParamTypeDef *config;
  t_registry_data    *registry;

  /* Read registry */
  registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_SHELL, "Registry not available");
    Error_Handler();
  }

  if (camera_use_isp())
  {
    config = ISP_SVC_IQParam_Get(&hisp);

    /* Configure AEC */
    config->AECAlgo.enable                = registry->camera_aec_enable;
    config->AECAlgo.exposureCompensation  = registry->camera_aec_ev;
    config->sensorGainStatic.gain         = registry->camera_gain;
    config->sensorExposureStatic.exposure = registry->camera_exposure;
    ISP_SVC_Sensor_SetGain(&hisp, &config->sensorGainStatic);
    ISP_SVC_Sensor_SetExposure(&hisp, &config->sensorExposureStatic);

    /* Define MAX AWB profile ID */
    size_t count;
    for (count = 0; count < ISP_AWB_COLORTEMP_REF; count++)
    {
      if ((config->AWBAlgo.label[count][0U] == 0U) || (config->AWBAlgo.referenceColorTemp[count] == 0U))
      {
        break;
      }
    }
    camera_awb_max = MIN(count, ISP_AWB_COLORTEMP_REF) - 1U;

    /* Configure AWB */
    config->AWBAlgo.enable = registry->camera_awb_enable;
    if (config->AWBAlgo.enable == 0U)
    {
      camera_awb_idx                  = MIN(registry->camera_awb_profile, camera_awb_max);
      config->ispGainStatic.enable    = 1U;
      config->ispGainStatic.ispGainR  = config->AWBAlgo.ispGainR[camera_awb_idx];
      config->ispGainStatic.ispGainG  = config->AWBAlgo.ispGainG[camera_awb_idx];
      config->ispGainStatic.ispGainB  = config->AWBAlgo.ispGainB[camera_awb_idx];
      config->colorConvStatic.enable  = 1U;
      memcpy(config->colorConvStatic.coeff, config->AWBAlgo.coeff[camera_awb_idx], sizeof(config->colorConvStatic.coeff));
      ISP_SVC_ISP_SetGain(&hisp, &config->ispGainStatic);
      ISP_SVC_ISP_SetColorConv(&hisp, &config->colorConvStatic);
    }
  }

  /* Configure flip */
  if (camera.sensor.ctrl.set_flip)
  {
    camera.sensor.ctrl.set_flip(registry->camera_flip);
  }

  /* Configure brightness */
  if (camera.sensor.ctrl.set_brightness)
  {
    camera.sensor.ctrl.set_brightness(registry->camera_brightness);
  }

  /* Release registry */
  registry_release();
}
#endif /* !ISP_MW_TUNING_TOOL_SUPPORT */

/*-->> UPDATE <<---------------------*/
static void _camera_preview_update(void)
{
  /* Trigger update (display) */
  camera_preview.update         = false;

  /* Stream update: main proportion + centered */
  camera_preview.stream.width   = camera.main.width / _camera_decimation;
  camera_preview.stream.height  = camera.main.height / _camera_decimation;
  camera_preview.stream.x       = (camera.main.width - camera_preview.stream.width) / 2U;
  camera_preview.stream.y       = (camera.main.height - camera_preview.stream.height) / 2U;

  /* Stats update: Adjust on sensor, then scale */
  if ((_camera_stat_area.XSize > 0U) && (_camera_stat_area.YSize > 0U))
  {
    /* Clamp in base coordinates */
    int32_t x       = MAX(camera.main.sensor.x, MIN(_camera_stat_area.X0, camera.main.sensor.x + camera.main.sensor.width));
    int32_t y       = MAX(camera.main.sensor.y, MIN(_camera_stat_area.Y0, camera.main.sensor.y + camera.main.sensor.height));
    int32_t width   = MAX(0, MIN(_camera_stat_area.XSize, (camera.main.sensor.x + camera.main.sensor.width) - x));
    int32_t height  = MAX(0, MIN(_camera_stat_area.YSize, (camera.main.sensor.y + camera.main.sensor.height) - y));

    /* Translate to scaled coordinates */
    float scale_x   = (float)camera.main.width / (float)camera.main.sensor.width / (float)_camera_decimation;
    float scale_y   = (float)camera.main.height / (float)camera.main.sensor.height / (float)_camera_decimation;
    camera_preview.stat_area.x      = (uint32_t)((float)(x - camera.main.sensor.x) * scale_x) + camera_preview.stream.x;
    camera_preview.stat_area.y      = (uint32_t)((float)(y - camera.main.sensor.y) * scale_y) + camera_preview.stream.y;
    camera_preview.stat_area.width  = (uint32_t)((float)width * scale_x);
    camera_preview.stat_area.height = (uint32_t)((float)height * scale_y);

    /* Update preview */
    camera_preview.update = true;
  }
}

#ifdef ISP_MW_TUNING_TOOL_SUPPORT
static void _camera_stat_area_update(void)
{
  ISP_StatAreaTypeDef area;
  ISP_StatusTypeDef   status;

  /* Get statistics area */
  status = ISP_GetStatArea(&hisp, &area);
  if (status != ISP_OK)
  {
    LERROR(TRACE_CAMERA, "Can't get statistics area. Error %d (%s)", status, ERROR_MESSAGE(status));
    return;
  }

  /* Validate and update */
  if (memcmp(&_camera_stat_area, &area, sizeof(ISP_StatAreaTypeDef)) == 0)
  {
    return;
  }
  memcpy(&_camera_stat_area, &area, sizeof(ISP_StatAreaTypeDef));

  /* Update display */
  _camera_preview_update();
}
#endif /* ISP_MW_TUNING_TOOL_SUPPORT */

/*-->> PIPE <<-----------------------*/
static ISP_StatusTypeDef _camera_pipe_configure_ipplug(DCMIPP_HandleTypeDef *phdcmipp, uint32_t p0_wdt, uint32_t p0_bpp, uint32_t p1_wdt, uint32_t p1_bpp, uint32_t p2_wdt, uint32_t p2_bpp)
{
  DCMIPP_IPPlugConfTypeDef ipplug_conf = {0};
  uint32_t pc[DCMIPP_NUM_OF_PIPES] = {DCMIPP_CLIENT1, DCMIPP_CLIENT2, DCMIPP_CLIENT5};     /* Dump Pipe, Main Pipe in RGB, and Ancillary pipe*/
  uint32_t pb[DCMIPP_NUM_OF_PIPES] = {p0_wdt * p0_bpp, p1_wdt * p1_bpp, p2_wdt * p2_bpp};  /* Peak Bandwidth proportion for each pipe */
  uint32_t pb_all                  = pb[0] + pb[1] + pb[2];

  /* Validate */
  if (!phdcmipp)
  {
    return ISP_ERR_EINVAL;
  }

  /* Prepare IPPlug common configuration */
  ipplug_conf.MemoryPageSize= DCMIPP_MEMORY_PAGE_SIZE_256BYTES;
  ipplug_conf.Traffic       = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
  ipplug_conf.WLRURatio     = 10;

  /* Configure IPPlug for each pipe */
  for (uint8_t idx = 0; idx < DCMIPP_NUM_OF_PIPES; idx++)
  {
    ipplug_conf.Client                    = pc[idx];
    ipplug_conf.MaxOutstandingTransactions= ((16 * pb[idx]/pb_all) >= 1)? (16 * pb[idx]/pb_all) - 1 : 0;
    ipplug_conf.DPREGStart                = ipplug_conf.DPREGEnd? ipplug_conf.DPREGEnd + 1 : 0;
    ipplug_conf.DPREGEnd                  = MIN((ipplug_conf.DPREGStart + 640 * pb[idx]/pb_all), 639);
    if (HAL_DCMIPP_SetIPPlugConfig(phdcmipp, &ipplug_conf) != HAL_OK)
    {
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }
  }
  return ISP_OK;
}

static ISP_StatusTypeDef _camera_pipe_configure(DCMIPP_HandleTypeDef *phdcmipp, ISP_DumpCfgTypeDef dump, uint32_t pipe)
{
  DCMIPP_DecimationConfTypeDef  dec    = { 0 };
  DCMIPP_DownsizeTypeDef        down   = { 0 };
  DCMIPP_CropConfTypeDef        crop   = { 0 };
  DCMIPP_PipeConfTypeDef        config = { 0 };
  t_camera_pipe                 *hpipe  = NULL;
  UINT                          status;

  /* Validate */
  if (!phdcmipp || (pipe > DCMIPP_PIPE2))
  {
    return ISP_ERR_EINVAL;
  }

  /* Get PIPE configuration */
  switch (pipe)
  {
    case DCMIPP_PIPE0:
      hpipe  = &camera.dump;
      status = _camera_pipe_get_dump_configuration(&config, dump);
      break;

    case DCMIPP_PIPE1:
      hpipe  = &camera.main;
      status = _camera_pipe_get_main_configuration(&config, dump);
      break;

    case DCMIPP_PIPE2:
      hpipe  = &camera.ancillary;
      status = _camera_pipe_get_ancillary_configuration(&config, dump);
      break;

    default:
      status = ISP_ERR_EINVAL;
      break;
  }
  if (!hpipe || (status != ISP_OK))
  {
    LERROR(TRACE_CAMERA, "Can't get pipe (%d) config", pipe);
    return ISP_ERR_EINVAL;
  }

  /* PIPE0: Dump. No complex configuration needed */
  if (pipe == DCMIPP_PIPE0)
  {
    status = HAL_DCMIPP_PIPE_SetConfig(phdcmipp, pipe, &config);
    if (status != HAL_OK)
    {
      LERROR(TRACE_CAMERA, "Can't configure pipe (%d)", pipe);
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }
    return ISP_OK;
  }

  /* PIPE1/2: Main/Ancillary */
  status = _camera_pipe_get_scale_config(&crop, &dec, &down, pipe);
  if (status != ISP_OK)
  {
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }

  /* Configure cropping */
  if ((crop.HSize != 0) || (crop.VSize != 0))
  {
    status  = HAL_DCMIPP_PIPE_SetCropConfig(phdcmipp, pipe, &crop);
    status += HAL_DCMIPP_PIPE_EnableCrop(phdcmipp, pipe);
  }
  else
  {
    status  = HAL_DCMIPP_PIPE_DisableCrop(phdcmipp, pipe);
  }
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure pipe (%d) crop", pipe);
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }

  /* Configure decimation */
  if ((dec.HRatio != 0) || (dec.VRatio != 0))
  {
    status = HAL_DCMIPP_PIPE_SetDecimationConfig(phdcmipp, pipe, &dec);
    status += HAL_DCMIPP_PIPE_EnableDecimation(phdcmipp, pipe);
  }
  else
  {
    status = HAL_DCMIPP_PIPE_DisableDecimation(phdcmipp, pipe);
  }
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure pipe (%d) decimation", pipe);
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }

  /* Configure downsize */
  status  = HAL_DCMIPP_PIPE_SetDownsizeConfig(phdcmipp, pipe, &down);
  status += HAL_DCMIPP_PIPE_EnableDownsize(phdcmipp, pipe);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure pipe (%d) downsize", pipe);
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }

  /* Enable R/B swap */
  status = (hpipe->rbswap == 1) \
    ? HAL_DCMIPP_PIPE_EnableRedBlueSwap(phdcmipp, pipe)
    : HAL_DCMIPP_PIPE_DisableRedBlueSwap(phdcmipp, pipe);
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure RB swap (%d) on pipe (%d)", hpipe->rbswap, pipe);
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }

  /* Enable gamma: If -1, let ISP decide */
  if (hpipe->gamma >= 0)
  {
    status = (hpipe->gamma == 1) \
      ? HAL_DCMIPP_PIPE_EnableGammaConversion(phdcmipp, pipe)
      : HAL_DCMIPP_PIPE_DisableGammaConversion(phdcmipp, pipe);
    if (status != HAL_OK)
    {
      LERROR(TRACE_CAMERA, "Can't configure gamma (%d) on pipe (%d)", hpipe->gamma, pipe);
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }
  }

  /* Configure pipe */
  if (phdcmipp->PipeState[pipe] == HAL_DCMIPP_PIPE_STATE_RESET)
  {
    /* First time. Configure */
    status = HAL_DCMIPP_PIPE_SetConfig(phdcmipp, pipe, &config);

  }
  else
  {
    /* Already started. Just adjust settings */
    status  = HAL_DCMIPP_PIPE_SetPixelPackerFormat(phdcmipp, pipe, config.PixelPackerFormat);
    status += HAL_DCMIPP_PIPE_SetPitch(phdcmipp, pipe, config.PixelPipePitch);
  }
  if (status != HAL_OK)
  {
    LERROR(TRACE_CAMERA, "Can't configure pipe (%d)", pipe);
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }
  return ISP_OK;
}

static ISP_StatusTypeDef _camera_pipe_start(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe)
{
  uint8_t           *pbuff1 = NULL;
  uint8_t           *pbuff2 = NULL;
  uint8_t           single  = 1U;
  uint32_t          cmode;
  HAL_StatusTypeDef status;

  /* Validate */
  if (!phdcmipp || (pipe > DCMIPP_PIPE2))
  {
    return ISP_ERR_EINVAL;
  }

  /* Handle already started */
  if (phdcmipp->PipeState[pipe] == HAL_DCMIPP_PIPE_STATE_BUSY)
  {
    return ISP_OK;
  }

  /* Handle overruns */
  if ((phdcmipp->ErrorCode != 0) || (phdcmipp->State != HAL_DCMIPP_STATE_READY))
  {
    LWARNING(TRACE_CAMERA, "Overrun detected before pipe (%d) start", pipe);
    phdcmipp->State     = HAL_DCMIPP_STATE_READY;
    phdcmipp->ErrorCode = 0;
  }

  /* Start */
  switch (pipe)
  {
    case DCMIPP_PIPE1:
      cmode  = DCMIPP_MODE_CONTINUOUS;
      pbuff1 = _camera_main_buffer[0];
      #if CAMERA_MAIN_BUFFER_NUM > 1U
      pbuff2 = _camera_main_buffer[1];
      single = 0U;
      #endif
      break;

    #ifdef ISP_MW_TUNING_TOOL_SUPPORT
    case DCMIPP_PIPE0:
    case DCMIPP_PIPE2:
      cmode  = DCMIPP_MODE_SNAPSHOT;
    #else
    case DCMIPP_PIPE2:
      cmode  = DCMIPP_MODE_CONTINUOUS;
    #endif /* ISP_MW_TUNING_TOOL_SUPPORT */
      pbuff1 = _camera_ancillary_buffer[0];
      #if CAMERA_ANCILLARY_BUFFER_NUM > 1U
      pbuff2 = _camera_ancillary_buffer[1];
      single = 0U;
      #endif
      break;

    default:
      /* Pipe unexpected */
      return ISP_ERR_DCMIPP_CONFIGPIPE;
  }

  /* Run start logic */
  status = single \
    ? HAL_DCMIPP_CSI_PIPE_Start(phdcmipp, pipe, DCMIPP_VIRTUAL_CHANNEL0, (uint32_t)pbuff1, cmode)
    : HAL_DCMIPP_CSI_PIPE_DoubleBufferStart(phdcmipp, pipe, DCMIPP_VIRTUAL_CHANNEL0, (uint32_t)pbuff1, (uint32_t)pbuff2, cmode);

  /* Return */
  if (status == HAL_OK)
  {
    return ISP_OK;
  }
  return ISP_ERR_DCMIPP_START;
}

static ISP_StatusTypeDef _camera_pipe_stop(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe)
{
  /* Validate */
  if (!phdcmipp || (pipe > DCMIPP_PIPE2))
  {
    return ISP_ERR_EINVAL;
  }

  /* Stop */
  if (HAL_DCMIPP_PIPE_Stop(phdcmipp, pipe) != HAL_OK)
  {
    return ISP_ERR_DCMIPP_STOP;
  }

  /* Handle overrun */
  if ((phdcmipp->ErrorCode != 0) || (phdcmipp->State != HAL_DCMIPP_STATE_READY))
  {
    LWARNING(TRACE_CAMERA, "Overrun detected before pipe (%d) stop", pipe);
    phdcmipp->State     = HAL_DCMIPP_STATE_READY;
    phdcmipp->ErrorCode = 0;
  }
  return ISP_OK;
}

static ISP_StatusTypeDef _camera_pipe_suspend(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe)
{
  /* Validate */
  if (!phdcmipp || (pipe > DCMIPP_PIPE2))
  {
    return ISP_ERR_EINVAL;
  }

  /* Handle already suspended */
  if (phdcmipp->PipeState[pipe] == HAL_DCMIPP_PIPE_STATE_SUSPEND)
  {
    return ISP_OK;
  }

  /* Suspend */
  if (phdcmipp->PipeState[pipe] > HAL_DCMIPP_PIPE_STATE_READY)
  {
    if (HAL_DCMIPP_PIPE_Suspend(phdcmipp, pipe) != HAL_OK)
    {
      return ISP_ERR_DCMIPP_STOP;
    }
  }

  /* Handle overrun */
  if ((phdcmipp->ErrorCode != 0) || (phdcmipp->State != HAL_DCMIPP_STATE_READY))
  {
    LWARNING(TRACE_CAMERA, "Overrun detected before pipe (%d) stop", pipe);
    phdcmipp->State     = HAL_DCMIPP_STATE_READY;
    phdcmipp->ErrorCode = 0;
  }
  return ISP_OK;
}

static ISP_StatusTypeDef _camera_pipe_resume(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe)
{
  /* Validate */
  if (!phdcmipp || (pipe > DCMIPP_PIPE2))
  {
    return ISP_ERR_EINVAL;
  }

  /* Handle already started */
  if (phdcmipp->PipeState[pipe] == HAL_DCMIPP_PIPE_STATE_BUSY)
  {
    return ISP_OK;
  }

  /* Handle overruns */
  if ((phdcmipp->ErrorCode != 0) || (phdcmipp->State != HAL_DCMIPP_STATE_READY))
  {
    LWARNING(TRACE_CAMERA, "Overrun detected before pipe (%d) start", pipe);
    phdcmipp->State     = HAL_DCMIPP_STATE_READY;
    phdcmipp->ErrorCode = 0;
  }

  /* Resume */
  if (phdcmipp->PipeState[pipe] > HAL_DCMIPP_PIPE_STATE_BUSY)
  {
    if (HAL_DCMIPP_PIPE_Resume(phdcmipp, pipe) != HAL_OK)
    {
      return ISP_ERR_DCMIPP_START;
    }
  }
  return ISP_OK;
}

static int32_t _camera_pipe_get_decimation_ratio(float *ratio, uint8_t is_vertical)
{
  int dec_ratio = 1;
  while (*ratio >= 8)
  {
    dec_ratio *= 2;
    *ratio    /= 2;
  }
  switch (dec_ratio)
  {
    case 1:   return is_vertical ? DCMIPP_VDEC_ALL     : DCMIPP_HDEC_ALL;
    case 2:   return is_vertical ? DCMIPP_VDEC_1_OUT_2 : DCMIPP_HDEC_1_OUT_2;
    case 4:   return is_vertical ? DCMIPP_VDEC_1_OUT_4 : DCMIPP_HDEC_1_OUT_4;
    case 8:   return is_vertical ? DCMIPP_VDEC_1_OUT_8 : DCMIPP_HDEC_1_OUT_8;
    default:  Error_Handler();
  }
  return is_vertical ? DCMIPP_VDEC_ALL : DCMIPP_HDEC_ALL;
}

static ISP_StatusTypeDef _camera_pipe_get_scale_config(DCMIPP_CropConfTypeDef *crop, DCMIPP_DecimationConfTypeDef *dec, DCMIPP_DownsizeTypeDef *down, uint32_t pipe)
{
  t_camera_pipe *hpipe;

  /* Validate */
  switch (pipe)
  {
    case DCMIPP_PIPE1: hpipe = &camera.main; break;
    case DCMIPP_PIPE2: hpipe = &camera.ancillary; break;
    default:           hpipe = NULL; break;
  }
  if (!pipe || !crop || !dec || !down)
  {
    return ISP_ERR_EINVAL;
  }

  /* Crop settings */
  float ratiox = (float)camera.sensor.width / (float)hpipe->width;
  float ratioy = (float)camera.sensor.height / (float)hpipe->height;
  float ratio  = MIN(ratiox, ratioy);
  if (!((ratio >= 1) && (ratio < 64)))
  {
    LERROR(TRACE_CAMERA, "Camera scale unsupported");
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }

  crop->HSize   = (uint32_t)MIN(camera.sensor.width, ratio * hpipe->width);
  crop->VSize   = (uint32_t)MIN(camera.sensor.height, ratio * hpipe->height);
  crop->HStart  = (camera.sensor.width - crop->HSize + 1) / 2;
  crop->VStart  = (camera.sensor.height - crop->VSize + 1) / 2;
  crop->PipeArea= DCMIPP_POSITIVE_AREA;

  /* Save Pipe Effective Sensor Area */
  hpipe->sensor.x      = crop->HStart;
  hpipe->sensor.y      = crop->VStart;
  hpipe->sensor.width  = crop->HSize;
  hpipe->sensor.height = crop->VSize;

  /* Decimation settings */
  ratiox = (float)crop->HSize / hpipe->width;
  ratioy = (float)crop->VSize / hpipe->height;

  dec->HRatio = _camera_pipe_get_decimation_ratio(&ratiox, 0);
  dec->VRatio = _camera_pipe_get_decimation_ratio(&ratioy, 1);

  /* Downscaling settings */
  down->HRatio     = (uint32_t) (8192 * ratiox);
  down->VRatio     = (uint32_t) (8192 * ratioy);
  down->HDivFactor = (1024 * 8192 - 1) / down->HRatio;
  down->VDivFactor = (1024 * 8192 - 1) / down->VRatio;
  down->HSize      = hpipe->width;
  down->VSize      = hpipe->height;
  return ISP_OK;
}

static ISP_StatusTypeDef _camera_pipe_get_dump_configuration(DCMIPP_PipeConfTypeDef *config, ISP_DumpCfgTypeDef dump)
{
  UNUSED(dump);

  /* Validate input */
  if (!config)
  {
    return ISP_ERR_EINVAL;
  }

  /* Prepare dump configuration */
  memset(config, 0x00U, sizeof(DCMIPP_PipeConfTypeDef));
  config->FrameRate = DCMIPP_FRAME_RATE_ALL;
  return ISP_OK;
}

static ISP_StatusTypeDef _camera_pipe_get_main_configuration(DCMIPP_PipeConfTypeDef *config, ISP_DumpCfgTypeDef dump)
{
  UNUSED(dump);

  /* Validate input */
  if (!config)
  {
    return ISP_ERR_EINVAL;
  }

  #ifdef ISP_MW_TUNING_TOOL_SUPPORT
  /* Update pipe configuration for tuning tool */
  float rate = (float)camera.sensor.height / (float)camera.sensor.width;
  if (rate < 0.9f)
  {
    camera.main.width  = CAMERA_MAIN_WIDTH;
    camera.main.height = (uint32_t)(0.75f * CAMERA_MAIN_HEIGHT);
  }
  else if (rate > 1.1f)
  {
    camera.main.width  = (uint32_t)(0.75f * CAMERA_MAIN_WIDTH);
    camera.main.height = CAMERA_MAIN_HEIGHT;
  }
  #endif /* ISP_MW_TUNING_TOOL_SUPPORT */

  /* Prepare main configuration */
  memset(config, 0x00U, sizeof(DCMIPP_PipeConfTypeDef));
  config->FrameRate         = DCMIPP_FRAME_RATE_ALL;
  config->PixelPackerFormat = camera.main.packer;
  config->PixelPipePitch    = camera.main.width * camera.main.bpp;

  /* Adjust pitch to be multiple of 16 (HW constraint) */
  config->PixelPipePitch /= _camera_decimation;
  config->PixelPipePitch += 15;
  config->PixelPipePitch &= (uint32_t) ~15;

  return ISP_OK;
}

static ISP_StatusTypeDef _camera_pipe_get_ancillary_configuration(DCMIPP_PipeConfTypeDef *config, ISP_DumpCfgTypeDef dump)
{
  #ifndef ISP_MW_TUNING_TOOL_SUPPORT
  UNUSED(dump);
  #endif /* ISP_MW_TUNING_TOOL_SUPPORT */

  /* Validate input */
  if (!config)
  {
    return ISP_ERR_EINVAL;
  }

  #ifdef ISP_MW_TUNING_TOOL_SUPPORT
  /* Update pipe configuration for tuning tool */
  switch (dump)
  {
    case ISP_DUMP_CFG_FULLSIZE_RGB888:
      camera.ancillary.width  = camera.dump.width;
      camera.ancillary.height = camera.dump.height;
      break;

    case ISP_DUMP_CFG_DEFAULT:
      camera.ancillary.width  = camera.main.width;
      camera.ancillary.height = camera.main.height;
      break;

    default:
      /* Not implemented */
      return ISP_ERR_EINVAL;
  }
  #endif /* ISP_MW_TUNING_TOOL_SUPPORT */

  /* Prepare ancillary configuration */
  memset(config, 0x00U, sizeof(DCMIPP_PipeConfTypeDef));
  config->FrameRate         = DCMIPP_FRAME_RATE_ALL;
  config->PixelPackerFormat = camera.ancillary.packer;
  config->PixelPipePitch    = camera.ancillary.width * camera.ancillary.bpp;

  /* Adjust pitch to be multiple of 16 (HW constraint) */
  config->PixelPipePitch /= _camera_decimation;
  config->PixelPipePitch += 15;
  config->PixelPipePitch &= (uint32_t) ~15;

  return ISP_OK;
}

/*-->> IQTune <<---------------------*/
#ifdef ISP_MW_TUNING_TOOL_SUPPORT

static ISP_StatusTypeDef _camera_dump_frame(void *phdcmipp, uint32_t pipe, ISP_DumpCfgTypeDef config, uint32_t **buffer, ISP_DumpFrameMetaTypeDef *meta)
{
  DCMIPP_HandleTypeDef  *handler = (DCMIPP_HandleTypeDef*)phdcmipp;
  ISP_StatusTypeDef     status;

  /* Validate */
  if ((phdcmipp == NULL) || (buffer == NULL) || (meta == NULL))
  {
    return ISP_ERR_EINVAL;
  }

  /* Stop PIPE0 stream */
  if (_camera_pipe_suspend(handler, camera.main.id) != ISP_OK)
  {
    return ISP_ERR_DCMIPP_STOP;
  }

  /* Perform dump */
  status = _camera_dump_frame_internal(phdcmipp, pipe, config, buffer, meta);

  /* Restore PIPE0 and IPPlug */
  if (_camera_pipe_configure_ipplug(handler, 0, 0, camera.main.width, camera.main.bpp, 0, 0) != ISP_OK)
  {
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }
  if(_camera_pipe_resume(handler, camera.main.id) != ISP_OK)
  {
    return ISP_ERR_DCMIPP_START;
  }
  return status;
}

static ISP_StatusTypeDef _camera_dump_frame_internal(void *phdcmipp, uint32_t pipe, ISP_DumpCfgTypeDef config, uint32_t **buffer, ISP_DumpFrameMetaTypeDef *meta)
{
  DCMIPP_HandleTypeDef  *handler = (DCMIPP_HandleTypeDef*)phdcmipp;
  uint32_t              last_id;
  uint32_t              timeout = 0;

  /* Validate */
  if ((phdcmipp == NULL) || (buffer == NULL) || (meta == NULL))
  {
    return ISP_ERR_EINVAL;
  }

  /* Handle dumps */
  if (pipe == DCMIPP_PIPE2)
  {
    /* Configure pipe */
    if (_camera_pipe_configure(handler, config, camera.ancillary.id) != ISP_OK)
    {
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }

    /* Maximize IPPlug */
    if (_camera_pipe_configure_ipplug(handler, 0, 0, 0, 0, camera.ancillary.width, camera.ancillary.bpp) != ISP_OK)
    {
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }

    /* Dump frame */
    last_id = ISP_GetAncillaryFrameId(&hisp);
    if (_camera_pipe_start(handler, camera.ancillary.id) != ISP_OK)
    {
      return ISP_ERR_DCMIPP_START;
    }
    while (ISP_GetAncillaryFrameId(&hisp) == last_id)
    {
      HAL_Delay(1);
      if (++timeout > CAMERA_DUMP_TIMEOUT)
      {
        return ISP_ERR_DCMIPP_DUMPTIMEOUT;
      }
    }

    /* Set output parameters */
    *buffer       = (uint32_t*)_camera_ancillary_buffer[0];
    /* Get frame configuration from hardware registers (no HAL API for that) */
    meta->width   = (handler->Instance->P2DSSZR & DCMIPP_P2DSSZR_HSIZE) >> DCMIPP_P2DSSZR_HSIZE_Pos;
    meta->height  = (handler->Instance->P2DSSZR & DCMIPP_P2DSSZR_VSIZE) >> DCMIPP_P2DSSZR_VSIZE_Pos;
    meta->pitch   = (handler->Instance->P2PPM0PR & DCMIPP_P2PPM0PR_PITCH) >> DCMIPP_P2PPM0PR_PITCH_Pos;
    meta->size    = meta->height * meta->pitch;
    meta->format  = ISP_FORMAT_RGB888;
  }
  else if (pipe == DCMIPP_PIPE0)
  {
    /* Dump on PIPE0 */
    if (config != ISP_DUMP_CFG_DUMP_PIPE_SENSOR)
    {
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }

    /* Maximize IPPlug */
    if (_camera_pipe_configure_ipplug(handler, camera.dump.width, camera.dump.bpp, 0, 0, 0, 0) != ISP_OK)
    {
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }

    /* Dump frame */
    last_id = ISP_GetDumpFrameId(&hisp);
    if (_camera_pipe_start(handler, camera.dump.id) != ISP_OK)
    {
      return ISP_ERR_DCMIPP_START;
    }
    while (ISP_GetDumpFrameId(&hisp) == last_id)
    {
      HAL_Delay(1);
      if (++timeout > CAMERA_DUMP_TIMEOUT)
      {
        return ISP_ERR_DCMIPP_DUMPTIMEOUT;
      }
    }

    /* Set output parameters */
    *buffer       = (uint32_t*)_camera_ancillary_buffer[0];
    meta->width   = camera.dump.width;
    meta->height  = camera.dump.height;
    meta->pitch   = meta->width * camera.dump.bpp;
    meta->size    = meta->height * meta->pitch;
    meta->format  = (ISP_FormatTypeDef)(camera.sensor.csi_dtype - DCMIPP_DT_RAW8 + 1U);
  }
  else
  {
    /* Not supported */
    return ISP_ERR_DCMIPP_CONFIGPIPE;
  }
  return ISP_OK;
}

static ISP_StatusTypeDef _camera_preview_start(void *phdcmipp)
{
  return _camera_pipe_resume((DCMIPP_HandleTypeDef*)phdcmipp, camera.main.id);
}

static ISP_StatusTypeDef _camera_preview_stop(void *phdcmipp)
{
  return _camera_pipe_suspend((DCMIPP_HandleTypeDef*)phdcmipp, camera.main.id);
}

#endif /* ISP_MW_TUNING_TOOL_SUPPORT */

/*-->> BUS <<------------------------*/
static int32_t _camera_bus_init(void)
{
  /* This is handled elsewhere */
  return BSP_OK;
}

static int32_t _camera_bus_deinit(void)
{
  /* This is handled elsewhere */
  return BSP_OK;
}

static int32_t _camera_bus_read(uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size)
{
  return bsp_i2c_read_reg16(I2C_CAMERA, addr, reg, buff, size, 1000U);
}

static int32_t _camera_bus_write(uint16_t addr, uint16_t reg, uint8_t *buff, uint16_t size)
{
  return bsp_i2c_write_reg16(I2C_CAMERA, addr, reg, buff, size, 1000U);
}

/*-->> CTRL <<-----------------------*/
static void _camera_ctrl_power(bool value)
{
  /* This is handled elsewhere */
  UNUSED(value);
}

/*-->> DCMIPP <<---------------------*/
/**
 * @brief  Initializes the DCMIPP MSP.
 * @param  phdcmipp DCMIPP device handle
 */
static void DCMIPP_MspInit(DCMIPP_HandleTypeDef *phdcmipp)
{
  UNUSED(phdcmipp);

  /* Enable DCMIPP clock */
  __HAL_RCC_DCMIPP_CLK_ENABLE();
  __HAL_RCC_DCMIPP_CLK_SLEEP_ENABLE();
  __HAL_RCC_DCMIPP_FORCE_RESET();
  __HAL_RCC_DCMIPP_RELEASE_RESET();

  /* NVIC configuration for DCMIPP transfer complete interrupt */
  HAL_NVIC_SetPriority(DCMIPP_IRQn, 0x07, 0);
  HAL_NVIC_EnableIRQ(DCMIPP_IRQn);

  /* Enable CSI clock */
  __HAL_RCC_CSI_CLK_ENABLE();
  __HAL_RCC_CSI_CLK_SLEEP_ENABLE();
  __HAL_RCC_CSI_FORCE_RESET();
  __HAL_RCC_CSI_RELEASE_RESET();

  /* NVIC configuration for CSI transfer complete interrupt */
  HAL_NVIC_SetPriority(CSI_IRQn, 0x07, 0);
  HAL_NVIC_EnableIRQ(CSI_IRQn);
}

/**
 * @brief  DeInitializes the DCMIPP MSP.
 * @param  phdcmipp DCMIPP device handle
 */
static void DCMIPP_MspDeInit(DCMIPP_HandleTypeDef *phdcmipp)
{
  UNUSED(phdcmipp);

  __HAL_RCC_DCMIPP_FORCE_RESET();
  __HAL_RCC_DCMIPP_RELEASE_RESET();

  /* Disable NVIC  for DCMIPP transfer complete interrupt */
  HAL_NVIC_DisableIRQ(DCMIPP_IRQn);

  /* Disable DCMIPP clock */
  __HAL_RCC_DCMIPP_CLK_DISABLE();

  __HAL_RCC_CSI_FORCE_RESET();
  __HAL_RCC_CSI_RELEASE_RESET();

  /* Disable NVIC  for DCMIPP transfer complete interrupt */
  HAL_NVIC_DisableIRQ(CSI_IRQn);

  /* Disable DCMIPP clock */
  __HAL_RCC_CSI_CLK_DISABLE();
}

/**
 * @brief  Frame Event callback on pipe
 * @param  phdcmipp DCMIPP device handle
 * @param  pipe     Pipe receiving the callback
 */
extern void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe)
{
  UNUSED(phdcmipp);

  uint32_t event;

  /* Handle event */
  switch (pipe)
  {
    case DCMIPP_PIPE0:
      ISP_IncDumpFrameId(&hisp);
      event = CAMERA_EVT_FRAME_DUMP;
      break;

    case DCMIPP_PIPE1:
      stat_freq_count(STAT_FREQ_CAMERA, 1U);
      ISP_IncMainFrameId(&hisp);
      event = CAMERA_EVT_FRAME_MAIN;
      break;

    case DCMIPP_PIPE2:
      ISP_IncAncillaryFrameId(&hisp);
      event = CAMERA_EVT_FRAME_ANCILLARY;
      break;

    default:
      event = CAMERA_EVT_UNKNOWN;
      break;
  }

  /* Propagate event */
  if (event != CAMERA_EVT_UNKNOWN)
  {
    rtos_raise_event(&_camera_task.evt, event);
  }
}

/**
 * @brief  Vsync Event callback on pipe
 * @param  phdcmipp DCMIPP device handle
 * @param  pipe     Pipe receiving the callback
 */
extern void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *phdcmipp, uint32_t pipe)
{
  UNUSED(phdcmipp);

  uint32_t event;

  /* Handle event */
  switch (pipe)
  {
    case DCMIPP_PIPE1:
      event = CAMERA_EVT_VSYNC;
      if (camera_use_isp())
      {
        ISP_GatherStatistics(&hisp);
        ISP_OutputMeta(&hisp);
      }
      break;

    default:
      event = CAMERA_EVT_UNKNOWN;
      break;
  }
  /* Propagate event */
  if (event != CAMERA_EVT_UNKNOWN)
  {
    rtos_raise_event(&_camera_task.evt, event);
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Camera -->
*//*--------------------------------------------------------------------------*/
