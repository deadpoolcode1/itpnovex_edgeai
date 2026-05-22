/**
 ******************************************************************************
 * @file    camera_imx335.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the IMX335 BSP API.
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

#ifdef USE_IMX335_SENSOR
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

typedef struct
{
  t_camera_sensor *ctx;
  IMX335_Object_t dev;
  int32_t         gain;
  int32_t         exposure;
  uint8_t         is_init;
  uint8_t         is_started;
} t_imx335;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

t_imx335 imx335 = { 0 };

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t _imx335_init(void);
static int32_t _imx335_start(void);
static int32_t _imx335_get_flip(uint8_t *flip);
static int32_t _imx335_set_flip(uint8_t flip);

/*-->> IMX335 CTX <<------------------*/
static int32_t _imx335_ctx_get_tick(void);

/*-->> IMX335 ISP <<-----------------*/
static ISP_StatusTypeDef _isp_get_info(uint32_t instance, ISP_SensorInfoTypeDef *info);
static ISP_StatusTypeDef _isp_get_gain(uint32_t instance, int32_t *value);
static ISP_StatusTypeDef _isp_set_gain(uint32_t instance, int32_t value);
static ISP_StatusTypeDef _isp_get_exposure(uint32_t instance, int32_t *value);
static ISP_StatusTypeDef _isp_set_exposure(uint32_t instance, int32_t value);
static ISP_StatusTypeDef _isp_set_test_pattern(uint32_t instance, int32_t mode);

/*-->> IMX335 MISC <<----------------*/
static int32_t _imx335_get_resolution(const t_camera_sensor* sensor);
static int32_t _imx335_get_pixelformat(const t_camera_sensor* sensor);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t camera_probe_imx335(t_camera_sensor* sensor, ISP_AppliHelpersTypeDef* helpers)
{
  IMX335_IO_t ctx = { 0 };
  uint32_t    id;
  int32_t     status;

  /* Prepare context */
  sensor->bus.addr = CAMERA_SENSOR_IMX335_ADDRESS;
  imx335.ctx       = sensor;
  ctx.Address      = sensor->bus.addr;
  ctx.Init         = sensor->bus.init;
  ctx.DeInit       = sensor->bus.deinit;
  ctx.ReadReg      = sensor->bus.read;
  ctx.WriteReg     = sensor->bus.write;
  ctx.GetTick      = _imx335_ctx_get_tick;
  status = IMX335_RegisterBusIO(&imx335.dev, &ctx);
  if (status != IMX335_OK)
  {
    LERROR(TRACE_CAMERA, "Can't register sensor context");
    return BSP_ERROR_COMPONENT;
  }

  /* Validate device ID */
  status = IMX335_ReadID(&imx335.dev, &id);
  if ((status != IMX335_OK) || (id != IMX335_CHIP_ID))
  {
    LERROR(TRACE_CAMERA, "Can't read sensor ID");
    return BSP_ERROR_COMPONENT;
  }

  /* Configure sensor for pipeline */
  sensor->id          = CAMERA_SENSOR_IMX335;
  sensor->bpp         = 2U;
  sensor->width       = CAMERA_SENSOR_IMX335_WIDTH;
  sensor->height      = CAMERA_SENSOR_IMX335_HEIGHT;
  sensor->csi_brate   = DCMIPP_CSI_PHY_BT_1600;
  sensor->csi_lanes   = DCMIPP_CSI_TWO_DATA_LANES;
  sensor->csi_dtype   = DCMIPP_DT_RAW10;
  sensor->csi_dfmt    = DCMIPP_CSI_DT_BPP10;
  sensor->use_isp     = 1U;

  /* Initialize sensor */
  status = _imx335_init();
  if (status != BSP_OK)
  {
    LERROR(TRACE_CAMERA, "Can't initialize sensor");
    return BSP_ERROR_COMPONENT;
  }

  /* Set ISP helpers */
  helpers->GetSensorInfo        = _isp_get_info;
  helpers->SetSensorGain        = _isp_set_gain;
  helpers->GetSensorGain        = _isp_get_gain;
  helpers->SetSensorExposure    = _isp_set_exposure;
  helpers->GetSensorExposure    = _isp_get_exposure;
  helpers->SetSensorTestPattern = _isp_set_test_pattern;
  return BSP_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static int32_t _imx335_init(void)
{
  int32_t pixel_format;
  int32_t resolution;
  int32_t status;

  /* Validate */
  if (imx335.is_init)
  {
    return BSP_OK;
  }
  pixel_format = _imx335_get_pixelformat(imx335.ctx);
  if (pixel_format == BSP_ERROR_PARAMETER)
  {
    return BSP_ERROR_PARAMETER;
  }
  resolution = _imx335_get_resolution(imx335.ctx);
  if (resolution == BSP_ERROR_PARAMETER)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Initialize */
  status = IMX335_Init(&imx335.dev, resolution, pixel_format);
  if (status != IMX335_OK)
  {
    return BSP_ERROR_COMPONENT;
  }
  status = IMX335_SetFrequency(&imx335.dev, IMX335_INCK_24MHZ);
  if (status != IMX335_OK)
  {
    return BSP_ERROR_COMPONENT;
  }
  status = IMX335_SetFramerate(&imx335.dev, imx335.ctx->fps);
  if (status != IMX335_OK)
  {
    return BSP_ERROR_COMPONENT;
  }
  imx335.is_init            = 1;
  imx335.ctx->ctrl.start    = _imx335_start;
  imx335.ctx->ctrl.get_flip = _imx335_get_flip;
  imx335.ctx->ctrl.set_flip = _imx335_set_flip;
  return BSP_OK;
}

static int32_t _imx335_start(void)
{
  /* Validate */
  if (!imx335.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }
  if (imx335.is_started)
  {
    return BSP_OK;
  }

  /* Start */
  int32_t status = IMX335_Start(&imx335.dev);
  if (status != IMX335_OK)
  {
    return BSP_ERROR_COMPONENT;
  }
  imx335.is_started = 1;
  return BSP_OK;
}

static int32_t _imx335_get_flip(uint8_t *flip)
{
  /* Validate */
  if (!imx335.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }

  /* Get flip */
  *flip = imx335.ctx->flip;
  return BSP_OK;
}

static int32_t _imx335_set_flip(uint8_t flip)
{
  int32_t status;

  /* Validate */
  if (!imx335.is_init)
  {
    return BSP_ERROR_COMPONENT;
  }

  /* Set flip */
  imx335.ctx->flip = flip;
  switch (imx335.ctx->flip)
  {
    case CAMERA_FLIP_NONE:  flip = IMX335_MIRROR_FLIP_NONE; break;
    case CAMERA_FLIP_H:     flip = IMX335_MIRROR;           break;
    case CAMERA_FLIP_V:     flip = IMX335_FLIP;             break;
    default:                flip = IMX335_MIRROR_FLIP;      break;
  }
  status = IMX335_MirrorFlipConfig(&imx335.dev, flip);
  return (status == 0)? BSP_OK : BSP_ERROR_COMPONENT;
}

/*-->> IMX335 CTX <<------------------*/
static int32_t _imx335_ctx_get_tick(void)
{
  return (int32_t)HAL_GetTick();
}

/*-->> IMX335 ISP <<-----------------*/
static ISP_StatusTypeDef _isp_get_info(uint32_t instance, ISP_SensorInfoTypeDef *info)
{
  UNUSED(instance);

  /* Validate */
  if (!info)
  {
    return ISP_ERR_SENSORINFO;
  }

  /* Get name */
  if (sizeof(info->name) >= strlen(camera_name[CAMERA_SENSOR_IMX335]) + 1)
  {
    strcpy(info->name, camera_name[CAMERA_SENSOR_IMX335]);
  }
  else
  {
    return ISP_ERR_SENSORINFO;
  }

  /* Get camera data */
  info->bayer_pattern = IMX335_BAYER_PATTERN;
  info->color_depth   = IMX335_COLOR_DEPTH;
  info->width         = IMX335_WIDTH;
  info->height        = IMX335_HEIGHT;
  info->gain_min      = IMX335_GAIN_MIN;
  info->gain_max      = IMX335_GAIN_MAX;
  info->again_max     = IMX335_AGAIN_MAX;
  info->exposure_min  = IMX335_EXPOSURE_MIN;
  info->exposure_max  = IMX335_EXPOSURE_MAX;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_get_gain(uint32_t instance, int32_t *value)
{
  UNUSED(instance);
  *value = imx335.gain;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_set_gain(uint32_t instance, int32_t value)
{
  UNUSED(instance);
  int32_t status = IMX335_SetGain(&imx335.dev, value);
  if (status != IMX335_OK)
  {
    return ISP_ERR_SENSORGAIN;
  }
  imx335.gain = value;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_get_exposure(uint32_t instance, int32_t *value)
{
  UNUSED(instance);
  *value = imx335.exposure;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_set_exposure(uint32_t instance, int32_t value)
{
  UNUSED(instance);
  int32_t status = IMX335_SetExposure(&imx335.dev, value);
  if (status != IMX335_OK)
  {
    return ISP_ERR_SENSOREXPOSURE;
  }
  imx335.exposure = value;
  return ISP_OK;
}

static ISP_StatusTypeDef _isp_set_test_pattern(uint32_t instance, int32_t mode)
{
  UNUSED(instance);
  int32_t status = IMX335_SetTestPattern(&imx335.dev, mode);
  if (status != IMX335_OK)
  {
    return ISP_ERR_SENSORTESTPATTERN;
  }
  return ISP_OK;
}

/*-->> IMX335 MISC <<----------------*/
static int32_t _imx335_get_resolution(const t_camera_sensor* sensor)
{
  if ((sensor->width == IMX335_WIDTH) && (sensor->height == IMX335_HEIGHT))
  {
    return IMX335_R2592_1944;
  }
  return BSP_ERROR_PARAMETER;
}

static int32_t _imx335_get_pixelformat(const t_camera_sensor* sensor)
{
  if (sensor->csi_dtype == DCMIPP_DT_RAW10)
  {
    return IMX335_RAW_RGGB10;
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
#endif /* USE_IMX335_SENSOR */
