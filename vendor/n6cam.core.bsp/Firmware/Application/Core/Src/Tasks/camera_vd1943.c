/**
 ******************************************************************************
 * @file    camera_vd1943.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the VD1943 BSP API.
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

#ifdef USE_VD1943_SENSOR
#include "n6cam_error.h"

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define VD1943_CHUNK_SIZE     128U
#define VD1943_REG_ID         0x0000U
#define VD1943_CHECK_ID(id)   ((id == 0x53393430U) || (id == 0x53393431U) || (id == 0x53393432U))

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

#ifndef MDB_TO_LINEAR
  #define MDB_TO_LINEAR(value)    (pow(10.0f, ((value) / 1000.0f) / 20.0f))
#endif
#ifndef LINEAR_TO_MDB
  #define LINEAR_TO_MDB(value)    (1000 * (20.0f * log10(value)))
#endif
#ifndef FP58_TO_FLOAT
  #define FP58_TO_FLOAT(value)    (((value) >> 8) + ((0xFF & (value)) / 256.0f))
#endif
#ifndef FLOAT_TO_FP58
  #define FLOAT_TO_FP58(value)    (((uint16_t)(value) << 8) | (0xFF & (uint16_t)(256.0f * ((value) - (uint16_t)(value)))))
#endif

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

typedef struct
{
  uint32_t mdb_min;
  uint32_t mdb_max;
  uint32_t reg;
  double   linear;
} t_vd1943_gain;

typedef struct
{
  t_camera_sensor *ctx;
  VD1943_Ctx_t    dev;
  int32_t         gain;
  int32_t         exposure;
  uint8_t         is_init;
  uint8_t         is_started;
} t_vd1943;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

t_vd1943 vd1943 = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t _vd1943_init(void);
static int32_t _vd1943_start(void);
static int32_t _vd1943_get_flip(uint8_t *flip);
static int32_t _vd1943_set_flip(uint8_t flip);

/*-->> VD1943 CTX <<-----------------*/
static void _vd1943_ctx_delay(VD1943_Ctx_t *dev, uint32_t delay);
static void _vd1943_ctx_log(VD1943_Ctx_t *dev, int level, const char *fmt, va_list args);
static void _vd1943_ctx_shutdown(VD1943_Ctx_t *dev, int value);
static int  _vd1943_ctx_read8(VD1943_Ctx_t *dev, uint16_t addr, uint8_t *value);
static int  _vd1943_ctx_read16(VD1943_Ctx_t *dev, uint16_t addr, uint16_t *value);
static int  _vd1943_ctx_read32(VD1943_Ctx_t *dev, uint16_t addr, uint32_t *value);
static int  _vd1943_ctx_write8(VD1943_Ctx_t *dev, uint16_t addr, uint8_t value);
static int  _vd1943_ctx_write16(VD1943_Ctx_t *dev, uint16_t addr, uint16_t value);
static int  _vd1943_ctx_write32(VD1943_Ctx_t *dev, uint16_t addr, uint32_t value);
static int  _vd1943_ctx_write_array(VD1943_Ctx_t *dev, uint16_t addr, uint8_t *data, int size);

/*-->> VD1943 ISP <<-----------------*/
static ISP_StatusTypeDef _isp_get_info(uint32_t instance, ISP_SensorInfoTypeDef *info);
static ISP_StatusTypeDef _isp_get_gain(uint32_t instance, int32_t *value);
static ISP_StatusTypeDef _isp_set_gain(uint32_t instance, int32_t value);
static ISP_StatusTypeDef _isp_get_exposure(uint32_t instance, int32_t *value);
static ISP_StatusTypeDef _isp_set_exposure(uint32_t instance, int32_t value);

/*-->> VD1943 MISC <<----------------*/
static int32_t _vd1943_get_resolution(const t_camera_sensor* sensor);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t camera_probe_vd1943(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers)
{
  uint32_t id;
  int32_t  status;

  UNUSED(helpers);

  /* Prepare context */
  sensor->bus.addr        = CAMERA_SENSOR_VD1943_ADDRESS;
  vd1943.ctx              = sensor;
  vd1943.dev.shutdown_pin = _vd1943_ctx_shutdown;
  vd1943.dev.read8        = _vd1943_ctx_read8;
  vd1943.dev.read16       = _vd1943_ctx_read16;
  vd1943.dev.read32       = _vd1943_ctx_read32;
  vd1943.dev.write8       = _vd1943_ctx_write8;
  vd1943.dev.write16      = _vd1943_ctx_write16;
  vd1943.dev.write32      = _vd1943_ctx_write32;
  vd1943.dev.write_array  = _vd1943_ctx_write_array;
  vd1943.dev.delay        = _vd1943_ctx_delay;
  vd1943.dev.log          = _vd1943_ctx_log;
  status = vd1943.ctx->bus.init();
  if (status != BSP_OK)
  {
    LERROR(TRACE_CAMERA, "Can't register sensor context");
    return BSP_ERROR_PERIPHERAL;
  }

  /* Validate device ID */
  status = vd1943.dev.read32(&vd1943.dev, VD1943_REG_ID, &id);
  if ((status != BSP_OK) || !VD1943_CHECK_ID(id))
  {
    LERROR(TRACE_CAMERA, "Can't read sensor ID");
    return BSP_ERROR_COMPONENT;
  }

  /* Configure sensor for pipeline */
  sensor->id        = CAMERA_SENSOR_VD1943;
  sensor->bpp       = 2U;
  sensor->width     = CAMERA_SENSOR_VD1943_WIDTH;
  sensor->height    = CAMERA_SENSOR_VD1943_HEIGHT;
  sensor->csi_brate = DCMIPP_CSI_PHY_BT_1300,
  sensor->csi_lanes = DCMIPP_CSI_TWO_DATA_LANES,
  sensor->csi_dtype = DCMIPP_DT_RAW10,
  sensor->csi_dfmt  = DCMIPP_CSI_DT_BPP10,
  sensor->use_isp   = 1U;

  /* Initialize sensor */
  status = _vd1943_init();
  if (status != BSP_OK)
  {
    LERROR(TRACE_CAMERA, "Can't initialize sensor");
    return BSP_ERROR_COMPONENT;
  }

  /* Set ISP helpers */
  helpers->GetSensorInfo      = _isp_get_info;
  helpers->SetSensorGain      = _isp_set_gain;
  helpers->GetSensorGain      = _isp_get_gain;
  helpers->SetSensorExposure  = _isp_set_exposure;
  helpers->GetSensorExposure  = _isp_get_exposure;
  return BSP_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t _vd1943_init(void)
{
  VD1943_Config_t config = { 0 };
  int32_t         resolution;
  int32_t         status;
  int32_t         swap = 0U;

  #if defined(STM32N6DK_C02)
  swap = 1U;
  #endif /* BOARD */

  /* Validate */
  if (vd1943.is_init)
  {
    return BSP_OK;
  }
  resolution = _vd1943_get_resolution(vd1943.ctx);
  if (resolution == BSP_ERROR_PARAMETER)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Prepare */
  VD1943_MODE_t mode;
  switch (vd1943.ctx->csi_dtype)
  {
    case DCMIPP_DT_RAW8:  mode = VD1943_RS_SDR_RGB_8; break;
    case DCMIPP_DT_RAW10: mode = VD1943_RS_SDR_RGB_10; break;
    case DCMIPP_DT_RAW12: mode = VD1943_RS_SDR_RGB_12; break;
    default: return BSP_ERROR_PARAMETER;
  }

  /* Configure */
  config.sync_mode                      = VD1943_MASTER;
  config.frame_rate                     = vd1943.ctx->fps;
  config.ext_clock_freq_in_hz           = 25000000U;
  config.image_processing_mode          = mode;
  config.flip_mirror_mode               = VD1943_MIRROR_FLIP_NONE;
  config.resolution                     = (VD1943_Res_t)resolution;
  config.patgen                         = VD1943_PATGEN_DISABLE;
  config.out_itf.data_rate_in_mps       = VD1943_DEFAULT_DATARATE;
  config.out_itf.datalane_nb            = 2U;
  config.out_itf.clock_lane_swap_enable = swap;
  for (size_t idx = 0; idx < 4U; idx++)
  {
    config.out_itf.physical_lane_swap_enable[idx]= swap;
    config.out_itf.logic_lane_mapping[idx] = idx;
  }
  for (size_t idx = 0; idx < VD1943_GPIO_NB; idx++)
  {
    config.gpios[idx].gpio_ctrl = VD1943_GPIO_IN;
    config.gpios[idx].enable    = 0U;
  }

  /* Initialize */
  status = VD1943_Init(&vd1943.dev, &config);
  if (status != 0)
  {
    return BSP_ERROR_COMPONENT;
  }
  if (vd1943.dev.bayer == VD1943_BAYER_NONE)
  {
    return BSP_ERROR_COMPONENT;
  }
  vd1943.is_init            = 1U;
  vd1943.ctx->ctrl.start    = _vd1943_start;
  vd1943.ctx->ctrl.get_flip = _vd1943_get_flip;
  vd1943.ctx->ctrl.set_flip = _vd1943_set_flip;
  return BSP_OK;
}

static int32_t _vd1943_start(void)
{
  int32_t status;

  /* Validate */
  if (!vd1943.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }
  if (vd1943.is_started)
  {
    return BSP_OK;
  }

  /* Start */
  status = VD1943_Start(&vd1943.dev);
  if ((status != 0) || (vd1943.dev.ctx.state == VD1943_ST_IDLE))
  {
    return BSP_ERROR_COMPONENT;
  }
  vd1943.is_started = 1U;
  return BSP_OK;
}

static int32_t _vd1943_get_flip(uint8_t *flip)
{
  /* Validate */
  if (!vd1943.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }

  /* Get flip */
  *flip = vd1943.ctx->flip;
  return BSP_OK;
}

static int32_t _vd1943_set_flip(uint8_t flip)
{
  /* TODO: IMPLEMENT */
  return BSP_ERROR_NOT_SUPPORTED;
}

/*-->> VD1943 IO <<-----------------*/
static void _vd1943_ctx_delay(VD1943_Ctx_t *dev, uint32_t delay)
{
  /* Used on camera power ON/OFF. Handled by camera task */
  UNUSED(dev);
  HAL_Delay(delay);
}

static void _vd1943_ctx_log(VD1943_Ctx_t *dev, int level, const char *fmt, va_list args)
{
  UNUSED(dev);
  UNUSED(level);
  //LINFO(TRACE_CAMERA, fmt, args);
}

static void _vd1943_ctx_shutdown(VD1943_Ctx_t *dev, int value)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  obj->ctx->ctrl.power(value);
}

static int _vd1943_ctx_read8(VD1943_Ctx_t *dev, uint16_t addr, uint8_t *value)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  return obj->ctx->bus.read(obj->ctx->bus.addr, addr, value, 1U);
}

static int _vd1943_ctx_read16(VD1943_Ctx_t *dev, uint16_t addr, uint16_t *value)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  return obj->ctx->bus.read(obj->ctx->bus.addr, addr, (uint8_t*)value, 2U);
}

static int _vd1943_ctx_read32(VD1943_Ctx_t *dev, uint16_t addr, uint32_t *value)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  return obj->ctx->bus.read(obj->ctx->bus.addr, addr, (uint8_t*)value, 4U);
}

static int _vd1943_ctx_write8(VD1943_Ctx_t *dev, uint16_t addr, uint8_t value)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  return obj->ctx->bus.write(obj->ctx->bus.addr, addr, &value, 1U);
}

static int _vd1943_ctx_write16(VD1943_Ctx_t *dev, uint16_t addr, uint16_t value)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  return obj->ctx->bus.write(obj->ctx->bus.addr, addr, (uint8_t*)&value, 2U);
}

static int _vd1943_ctx_write32(VD1943_Ctx_t *dev, uint16_t addr, uint32_t value)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  return obj->ctx->bus.write(obj->ctx->bus.addr, addr, (uint8_t*)&value, 4U);
}

static int _vd1943_ctx_write_array(VD1943_Ctx_t *dev, uint16_t addr, uint8_t *data, int size)
{
  t_vd1943 *obj = GET_CONTAINER_OF(dev, t_vd1943, dev);
  uint16_t chunk;
  int32_t  status;
  while (size)
  {
    chunk  = MIN(size, VD1943_CHUNK_SIZE);
    status = obj->ctx->bus.write(obj->ctx->bus.addr, addr, data, chunk);
    if (status != 0)
    {
      return status;
    }
    size -= chunk;
    addr += chunk;
    data += chunk;
  }
  return 0;
}

/*-->> VD1943 ISP <<-----------------*/
static ISP_StatusTypeDef _isp_get_info(uint32_t instance, ISP_SensorInfoTypeDef *info)
{
  UNUSED(instance);
  t_vd1943_gain again = { 0 };
  t_vd1943_gain dgain = { 0 };
  int32_t       status;

  /* Validate */
  if (!info)
  {
    return ISP_ERR_SENSORINFO;
  }

  /* Get name */
  if (sizeof(info->name) >= strlen(camera_name[CAMERA_SENSOR_VD1943]) + 1)
  {
    strcpy(info->name, camera_name[CAMERA_SENSOR_VD1943]);
  }
  else
  {
    return ISP_ERR_SENSORINFO;
  }

  /* Get direct data */
  info->bayer_pattern = vd1943.dev.bayer - 1;
  info->width         = VD1943_MAX_WIDTH;
  info->height        = VD1943_MAX_HEIGHT;

  /* Get pixel depth */
  status = VD1943_GetPixelDepth(&vd1943.dev, (unsigned int*)&info->color_depth);
  if (status != 0)
  {
    /* Set invalid */
    info->color_depth = 0;
  }

  /* Get exposure */
  status = VD1943_GetExposureRange(&vd1943.dev, (unsigned int*)&info->exposure_min, (unsigned int*)&info->exposure_max);
  if (status != 0)
  {
    /* Set invalid */
    info->exposure_max = 0x00U;
    info->exposure_min = 0xFFU;
  }

  /* Get gain */
  again.mdb_min   = (uint32_t)LINEAR_TO_MDB(16.0 / (16.0 - (float)VD1943_ANALOG_GAIN_MIN));
  again.mdb_max   = (uint32_t)LINEAR_TO_MDB(16.0 / (16.0 - (float)VD1943_ANALOG_GAIN_MAX));
  dgain.mdb_min   = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MIN));
  dgain.mdb_max   = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MAX));
  info->gain_min  = again.mdb_min + dgain.mdb_min;
  info->gain_max  = again.mdb_max + dgain.mdb_max;
  info->again_max = again.mdb_max;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_get_gain(uint32_t instance, int32_t *value)
{
  UNUSED(instance);
  *value = vd1943.gain;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_set_gain(uint32_t instance, int32_t value)
{
  UNUSED(instance);
  t_vd1943_gain again = { 0 };
  t_vd1943_gain dgain = { 0 };
  int32_t       status;

  /* Convert to mdb */
  again.mdb_min   = (uint32_t)LINEAR_TO_MDB(16.0 / (16.0 - (float)VD1943_ANALOG_GAIN_MIN));
  again.mdb_max   = (uint32_t)LINEAR_TO_MDB(16.0 / (16.0 - (float)VD1943_ANALOG_GAIN_MAX));
  dgain.mdb_min   = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MIN));
  dgain.mdb_max   = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(VD1943_DIGITAL_GAIN_MAX));
  if ((value < (again.mdb_min + dgain.mdb_min)) || (value > (again.mdb_max + dgain.mdb_max)))
  {
    return ISP_ERR_SENSORGAIN;
  }

  /* Compute gain */
  if (value <= again.mdb_max)
  {
    /* Use analog and keep digital at minimum */
    again.linear = MDB_TO_LINEAR((double)(value - dgain.mdb_min));
    dgain.linear = MDB_TO_LINEAR((double)dgain.mdb_min);

    /* Take care of rounding */
    again.reg = (uint32_t)((16.0 - (16.0 / again.linear)) + 0.5);
    if (again.reg > VD1943_ANALOG_GAIN_MAX)
    {
      again.reg = VD1943_ANALOG_GAIN_MAX;
    }
  }
  else
  {
    /* Analog saturated, remainder goes to digital */
    again.reg = VD1943_ANALOG_GAIN_MAX;
    dgain.linear = MDB_TO_LINEAR((double)(value - again.mdb_max));
  }

  /* Set gain */
  status = VD1943_SetAnalogGain(&vd1943.dev, again.reg);
  if (status != 0)
  {
    return ISP_ERR_SENSORGAIN;
  }
  status = VD1943_SetDigitalGain(&vd1943.dev, FLOAT_TO_FP58(dgain.linear));
  if (status != 0)
  {
    return ISP_ERR_SENSORGAIN;
  }
  vd1943.gain = value;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_get_exposure(uint32_t instance, int32_t *value)
{
  UNUSED(instance);
  *value = vd1943.exposure;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_set_exposure(uint32_t instance, int32_t value)
{
  UNUSED(instance);
  int32_t status = VD1943_SetExpo(&vd1943.dev, (unsigned int)value);
  if (status != 0)
  {
    return ISP_ERR_SENSOREXPOSURE;
  }
  vd1943.exposure = value;
  return ISP_OK;
}

/*-->> VD1943 MISC <<----------------*/
static int32_t _vd1943_get_resolution(const t_camera_sensor* sensor)
{
  if ((sensor->width == 320) && (sensor->height == 240))
  {
    return VD1943_RES_QVGA_320_240;
  }
  else if ((sensor->width == 640) && (sensor->height == 480))
  {
    return VD1943_RES_VGA_640_480;
  }
  else if ((sensor->width == 1024) && (sensor->height == 768))
  {
    return VD1943_RES_XGA_1024_768;
  }
  else if ((sensor->width == 1280) && (sensor->height == 720))
  {
    return VD1943_RES_720P_1280_720;
  }
  else if ((sensor->width == 1280) && (sensor->height == 1024))
  {
    return VD1943_RES_SXGA_1280_1024;
  }
  else if ((sensor->width == 1920) && (sensor->height == 1080))
  {
    return VD1943_RES_1080P_1920_1080;
  }
  else if ((sensor->width == 2048) && (sensor->height == 1536))
  {
    return VD1943_RES_QXGA_2048_1536;
  }
  else if ((sensor->width == 2560) && (sensor->height == 1920))
  {
    return VD1943_RES_2560_1920;
  }
  else if ((sensor->width == 2560) && (sensor->height == 1984))
  {
    return VD1943_RES_FULL_2560_1984;
  }
  return BSP_ERROR_PARAMETER;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Camera -->
*//*--------------------------------------------------------------------------*/
#endif /* USE_VD1943_SENSOR */
