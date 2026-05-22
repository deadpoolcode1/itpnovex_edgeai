/**
 ******************************************************************************
 * @file    camera_vd66gy.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the VD66GY BSP API.
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

#ifdef USE_VD66GY_SENSOR
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

#define VD66GY_CHUNK_SIZE     128U
#define VD66GY_REG_ID         0x0000U
#define VD66GY_CHECK_ID(id)   ((id) == 0x31105603U)

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
  uint32_t reg_min;
  uint32_t reg_max;
  uint32_t mdb_min;
  uint32_t mdb_max;
  uint32_t reg;
  double   linear;
} t_vd66gy_gain;

typedef struct
{
  t_camera_sensor *ctx;
  VD6G_Ctx_t      dev;
  int32_t         gain;
  int32_t         exposure;
  uint8_t         is_init;
  uint8_t         is_started;
} t_vd66gy;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

t_vd66gy vd66gy = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t _vd66gy_init(void);
static int32_t _vd66gy_start(void);
static int32_t _vd66gy_get_flip(uint8_t *flip);
static int32_t _vd66gy_set_flip(uint8_t flip);

/*-->> VD66GY CTX <<-----------------*/
static void _vd66gy_ctx_delay(struct VD6G_Ctx *dev, uint32_t delay);
static void _vd66gy_ctx_log(struct VD6G_Ctx *dev, int level, const char *fmt, va_list args);
static void _vd66gy_ctx_shutdown(struct VD6G_Ctx *dev, int value);
static int  _vd66gy_ctx_read8(struct VD6G_Ctx *dev, uint16_t addr, uint8_t *value);
static int  _vd66gy_ctx_read16(struct VD6G_Ctx *dev, uint16_t addr, uint16_t *value);
static int  _vd66gy_ctx_read32(struct VD6G_Ctx *dev, uint16_t addr, uint32_t *value);
static int  _vd66gy_ctx_write8(struct VD6G_Ctx *dev, uint16_t addr, uint8_t value);
static int  _vd66gy_ctx_write16(struct VD6G_Ctx *dev, uint16_t addr, uint16_t value);
static int  _vd66gy_ctx_write32(struct VD6G_Ctx *dev, uint16_t addr, uint32_t value);
static int  _vd66gy_ctx_write_array(struct VD6G_Ctx *dev, uint16_t addr, uint8_t *data, int size);

/*-->> VD66GY ISP <<-----------------*/
static ISP_StatusTypeDef _isp_get_info(uint32_t instance, ISP_SensorInfoTypeDef *info);
static ISP_StatusTypeDef _isp_get_gain(uint32_t instance, int32_t *value);
static ISP_StatusTypeDef _isp_set_gain(uint32_t instance, int32_t value);
static ISP_StatusTypeDef _isp_get_exposure(uint32_t instance, int32_t *value);
static ISP_StatusTypeDef _isp_set_exposure(uint32_t instance, int32_t value);

/*-->> VD66GY MISC <<----------------*/
static int32_t _vd66gy_get_resolution(const t_camera_sensor* sensor);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t camera_probe_vd66gy(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers)
{
  uint32_t id;
  int32_t  status;

  UNUSED(helpers);

  /* Prepare context */
  sensor->bus.addr        = CAMERA_SENSOR_VD66GY_ADDRESS;
  vd66gy.ctx              = sensor;
  vd66gy.dev.shutdown_pin = _vd66gy_ctx_shutdown;
  vd66gy.dev.read8        = _vd66gy_ctx_read8;
  vd66gy.dev.read16       = _vd66gy_ctx_read16;
  vd66gy.dev.read32       = _vd66gy_ctx_read32;
  vd66gy.dev.write8       = _vd66gy_ctx_write8;
  vd66gy.dev.write16      = _vd66gy_ctx_write16;
  vd66gy.dev.write32      = _vd66gy_ctx_write32;
  vd66gy.dev.write_array  = _vd66gy_ctx_write_array;
  vd66gy.dev.delay        = _vd66gy_ctx_delay;
  vd66gy.dev.log          = _vd66gy_ctx_log;
  status = vd66gy.ctx->bus.init();
  if (status != BSP_OK)
  {
    LERROR(TRACE_CAMERA, "Can't register sensor context");
    return BSP_ERROR_PERIPHERAL;
  }

  /* Validate device ID */
  status = vd66gy.dev.read32(&vd66gy.dev, VD66GY_REG_ID, &id);
  if ((status != BSP_OK) || !VD66GY_CHECK_ID(id))
  {
    LERROR(TRACE_CAMERA, "Can't read sensor ID");
    return BSP_ERROR_COMPONENT;
  }

  /* Configure sensor for pipeline */
  sensor->id        = CAMERA_SENSOR_VD66GY;
  sensor->bpp       = 1U;
  sensor->width     = CAMERA_SENSOR_VD66GY_WIDTH;
  sensor->height    = CAMERA_SENSOR_VD66GY_HEIGHT;
  sensor->csi_brate = DCMIPP_CSI_PHY_BT_800,
  sensor->csi_lanes = DCMIPP_CSI_TWO_DATA_LANES,
  sensor->csi_dtype = DCMIPP_DT_RAW8,
  sensor->csi_dfmt  = DCMIPP_CSI_DT_BPP8,
  sensor->use_isp   = 1U;

  /* Initialize sensor */
  status = _vd66gy_init();
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

static int32_t _vd66gy_init(void)
{
  VD6G_Config_t config = { 0 };
  int32_t       resolution;
  int32_t       status;

  /* Validate */
  if (vd66gy.is_init)
  {
    return BSP_OK;
  }
  resolution = _vd66gy_get_resolution(vd66gy.ctx);
  if (resolution == BSP_ERROR_PARAMETER)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Prepare */
  switch (vd66gy.ctx->csi_dtype)
  {
    case DCMIPP_DT_RAW8:  config.pixel_depth = 8;   break;
    case DCMIPP_DT_RAW10: config.pixel_depth = 10;  break;
    default: return BSP_ERROR_PARAMETER;
  }

  /* Configure */
  config.frame_rate                     = vd66gy.ctx->fps;
  config.ext_clock_freq_in_hz           = 12000000U;
  config.flip_mirror_mode               = VD6G_MIRROR_FLIP_NONE;
  config.flicker                        = VD6G_FLICKER_FREE_NONE;
  config.resolution                     = (VD6G_Res_t)resolution;
  config.patgen                         = VD6G_PATGEN_DISABLE;
  config.out_itf.datalane_nb            = 2;
  config.out_itf.clock_lane_swap_enable = 0;
  config.out_itf.data_lane0_swap_enable = 0;
  config.out_itf.data_lane1_swap_enable = 0;
  config.out_itf.data_lanes_mapping_swap_enable = 0;
  for (size_t idx = 0; idx < VD6G_GPIO_NB; idx++)
  {
    config.gpio_ctrl[idx] = VD6G_GPIO_GPIO_IN;
  }

  /* Initialize */
  status = VD6G_Init(&vd66gy.dev, &config);
  if (status != 0)
  {
    return BSP_ERROR_COMPONENT;
  }
  if (vd66gy.dev.bayer == VD6G_BAYER_NONE)
  {
    return BSP_ERROR_COMPONENT;
  }
  vd66gy.is_init            = 1U;
  vd66gy.ctx->ctrl.start    = _vd66gy_start;
  vd66gy.ctx->ctrl.get_flip = _vd66gy_get_flip;
  vd66gy.ctx->ctrl.set_flip = _vd66gy_set_flip;
  return BSP_OK;
}

static int32_t _vd66gy_start(void)
{
  int32_t status;

  /* Validate */
  if (!vd66gy.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }
  if (vd66gy.is_started)
  {
    return BSP_OK;
  }

  /* Start */
  status = VD6G_Start(&vd66gy.dev);
  if ((status != 0) || (vd66gy.dev.ctx.is_streaming == 0))
  {
    return BSP_ERROR_COMPONENT;
  }
  vd66gy.is_started = 1U;
  return BSP_OK;
}

static int32_t _vd66gy_get_flip(uint8_t *flip)
{
  /* Validate */
  if (!vd66gy.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }

  /* Get flip */
  *flip = vd66gy.ctx->flip;
  return BSP_OK;
}

static int32_t _vd66gy_set_flip(uint8_t flip)
{
  int32_t status;

  /* Validate */
  if (!vd66gy.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }

  /* Set flip */
  vd66gy.ctx->flip = flip;
  switch (vd66gy.ctx->flip)
  {
    case CAMERA_FLIP_NONE:  flip = VD6G_MIRROR_FLIP_NONE; break;
    case CAMERA_FLIP_H:     flip = VD6G_MIRROR;           break;
    case CAMERA_FLIP_V:     flip = VD6G_FLIP;             break;
    default:                flip = VD6G_MIRROR_FLIP;      break;
  }
  status = VD6G_SetFlipMirrorMode(&vd66gy.dev, (VD6G_MirrorFlip_t)flip);
  return (status == 0)? BSP_OK : BSP_ERROR_COMPONENT;
}

/*-->> VD66GY IO <<-----------------*/
static void _vd66gy_ctx_delay(struct VD6G_Ctx *dev, uint32_t delay)
{
  /* Used on camera power ON/OFF. Handled by camera task */
  UNUSED(dev);
  HAL_Delay(delay);
}

static void _vd66gy_ctx_log(struct VD6G_Ctx *dev, int level, const char *fmt, va_list args)
{
  UNUSED(dev);
  UNUSED(level);
  //LINFO(TRACE_CAMERA, fmt, args);
}

static void _vd66gy_ctx_shutdown(struct VD6G_Ctx *dev, int value)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  obj->ctx->ctrl.power(value);
}

static int _vd66gy_ctx_read8(struct VD6G_Ctx *dev, uint16_t addr, uint8_t *value)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  return obj->ctx->bus.read(obj->ctx->bus.addr, addr, value, 1U);
}

static int _vd66gy_ctx_read16(struct VD6G_Ctx *dev, uint16_t addr, uint16_t *value)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  return obj->ctx->bus.read(obj->ctx->bus.addr, addr, (uint8_t*)value, 2U);
}

static int _vd66gy_ctx_read32(struct VD6G_Ctx *dev, uint16_t addr, uint32_t *value)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  return obj->ctx->bus.read(obj->ctx->bus.addr, addr, (uint8_t*)value, 4U);
}

static int _vd66gy_ctx_write8(struct VD6G_Ctx *dev, uint16_t addr, uint8_t value)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  return obj->ctx->bus.write(obj->ctx->bus.addr, addr, &value, 1U);
}

static int _vd66gy_ctx_write16(struct VD6G_Ctx *dev, uint16_t addr, uint16_t value)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  return obj->ctx->bus.write(obj->ctx->bus.addr, addr, (uint8_t*)&value, 2U);
}

static int _vd66gy_ctx_write32(struct VD6G_Ctx *dev, uint16_t addr, uint32_t value)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  return obj->ctx->bus.write(obj->ctx->bus.addr, addr, (uint8_t*)&value, 4U);
}

static int _vd66gy_ctx_write_array(struct VD6G_Ctx *dev, uint16_t addr, uint8_t *data, int size)
{
  t_vd66gy *obj = GET_CONTAINER_OF(dev, t_vd66gy, dev);
  uint16_t chunk;
  int32_t  status;
  while (size)
  {
    chunk  = MIN(size, VD66GY_CHUNK_SIZE);
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

/*-->> VD66GY ISP <<-----------------*/
static ISP_StatusTypeDef _isp_get_info(uint32_t instance, ISP_SensorInfoTypeDef *info)
{
  UNUSED(instance);
  t_vd66gy_gain again = { 0 };
  t_vd66gy_gain dgain = { 0 };
  int32_t       status;

  /* Validate */
  if (!info)
  {
    return ISP_ERR_SENSORINFO;
  }

  /* Get name */
  if (sizeof(info->name) >= strlen(camera_name[CAMERA_SENSOR_VD66GY]) + 1)
  {
    strcpy(info->name, camera_name[CAMERA_SENSOR_VD66GY]);
  }
  else
  {
    return ISP_ERR_SENSORINFO;
  }

  /* Get direct data */
  info->bayer_pattern = vd66gy.dev.bayer - 1;
  info->color_depth   = vd66gy.dev.ctx.config_save.pixel_depth;
  info->width         = VD6G_MAX_WIDTH;
  info->height        = VD6G_MAX_HEIGHT;

  /* Get exposure */
  status = VD6G_GetExposureRegRange(&vd66gy.dev, &info->exposure_min, &info->exposure_max);
  if (status != 0)
  {
    return ISP_ERR_SENSORINFO;
  }

  /* Get gain */
  status = VD6G_GetAnalogGainRegRange(&vd66gy.dev, (uint8_t*)&again.reg_min, (uint8_t*)&again.reg_max);
  if (status != 0)
  {
    return ISP_ERR_SENSORINFO;
  }
  status = VD6G_GetDigitalGainRegRange(&vd66gy.dev, (uint16_t*)&dgain.reg_min, (uint16_t*)&dgain.reg_max);
  if (status != 0)
  {
    return ISP_ERR_SENSORINFO;
  }
  again.mdb_min = (uint32_t)LINEAR_TO_MDB(32.0 / (32.0 - (float)again.reg_min));
  again.mdb_max = (uint32_t)LINEAR_TO_MDB(32.0 / (32.0 - (float)again.reg_min));
  dgain.mdb_min = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(dgain.reg_min));
  dgain.mdb_max = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(dgain.reg_max));
  info->gain_min  = again.mdb_min + dgain.mdb_min;
  info->gain_max  = again.mdb_max + dgain.mdb_max;
  info->again_max = again.mdb_max;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_get_gain(uint32_t instance, int32_t *value)
{
  UNUSED(instance);
  *value = vd66gy.gain;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_set_gain(uint32_t instance, int32_t value)
{
  UNUSED(instance);
  t_vd66gy_gain again = { 0 };
  t_vd66gy_gain dgain = { 0 };
  int32_t       status;

  /* Get gain range */
  status = VD6G_GetAnalogGainRegRange(&vd66gy.dev, (uint8_t*)&again.reg_min, (uint8_t*)&again.reg_max);
  if (status != 0)
  {
    return ISP_ERR_SENSORGAIN;
  }
  status = VD6G_GetDigitalGainRegRange(&vd66gy.dev, (uint16_t*)&dgain.reg_min, (uint16_t*)&dgain.reg_max);
  if (status != 0)
  {
    return ISP_ERR_SENSORGAIN;
  }

  /* Convert to mdb */
  again.mdb_min = (uint32_t)LINEAR_TO_MDB(32.0 / (32.0 - (float)again.reg_min));
  again.mdb_max = (uint32_t)LINEAR_TO_MDB(32.0 / (32.0 - (float)again.reg_max));
  dgain.mdb_min = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(dgain.reg_min));
  dgain.mdb_max = (uint32_t)LINEAR_TO_MDB(FP58_TO_FLOAT(dgain.reg_max));
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
    again.reg = (uint32_t)((32.0 - (32.0 / again.linear)) + 0.5);
    if (again.reg > again.reg_max)
    {
      again.reg = again.reg_max;
    }
  }
  else
  {
    /* Analog saturated, remainder goes to digital */
    again.reg = again.reg_max;
    dgain.linear = MDB_TO_LINEAR((double)(value - again.mdb_max));
  }

  /* Set gain */
  status = VD6G_SetAnalogGain(&vd66gy.dev, again.reg);
  if (status != 0)
  {
    return ISP_ERR_SENSORGAIN;
  }
  status = VD6G_SetDigitalGain(&vd66gy.dev, FLOAT_TO_FP58(dgain.linear));
  if (status != 0)
  {
    return ISP_ERR_SENSORGAIN;
  }
  vd66gy.gain = value;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_get_exposure(uint32_t instance, int32_t *value)
{
  UNUSED(instance);
  *value = vd66gy.exposure;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_set_exposure(uint32_t instance, int32_t value)
{
  UNUSED(instance);
  int32_t status = VD6G_SetExposureTime(&vd66gy.dev, value);
  if (status != 0)
  {
    return ISP_ERR_SENSOREXPOSURE;
  }
  vd66gy.exposure = value;
  return ISP_OK;
}

/*-->> VD66GY MISC <<----------------*/
static int32_t _vd66gy_get_resolution(const t_camera_sensor* sensor)
{
  if ((sensor->width == 320) && (sensor->height == 240))
  {
    return VD6G_RES_QVGA_320_240;
  }
  else if ((sensor->width == 640) && (sensor->height == 480))
  {
    return VD6G_RES_VGA_640_480;
  }
  else if ((sensor->width == 1024) && (sensor->height == 768))
  {
    return VD6G_RES_XGA_1024_768;
  }
  else if ((sensor->width == 1120) && (sensor->height == 720))
  {
    return VD6G_RES_PORTRAIT_1120_720;
  }
  else if ((sensor->width == 1120) && (sensor->height == 1364))
  {
    return VD6G_RES_FULL_1120_1364;
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
#endif /* USE_VD66GY_SENSOR */
