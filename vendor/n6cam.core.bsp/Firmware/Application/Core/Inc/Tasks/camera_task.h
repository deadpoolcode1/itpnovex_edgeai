/**
 ******************************************************************************
 * @file    camera_task.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the camera module.
 *          This module is responsible for:
 *          - Initialize camera and capture buffers
 *          - Provide easy access to capture buffers
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
#ifndef _CAMERA_TASK_H_
#define _CAMERA_TASK_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "camera_config.h"
#include "isp_api.h"
#include "tx_app.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Camera
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* Camera task events */
#define CAMERA_EVT_UNKNOWN          0xFFU       /*!< Unknown event */
#define CAMERA_EVT_VSYNC            BIT(0U)     /*!< VSYNC */
#define CAMERA_EVT_FRAME_DUMP       BIT(1U)     /*!< PIPE0 frame available */
#define CAMERA_EVT_FRAME_MAIN       BIT(2U)     /*!< PIPE1 frame available */
#define CAMERA_EVT_FRAME_ANCILLARY  BIT(3U)     /*!< PIPE2 frame available */

#define CAMERA_GAIN_MIN             0U          /*!< 0[dB] */
#define CAMERA_GAIN_MAX             72000U      /*!< 72[dB] */

#define CAMERA_EXPOSURE_MIN         0U          /*!< 0[msec] */
#define CAMERA_EXPOSURE_MAX         33000U      /*!< 33[msec] */

#define CAMERA_BRIGHTNESS_MIN       30U         /*!< Minimum brightness value */
#define CAMERA_BRIGHTNESS_MAX       90U         /*!< Maximum brightness value */
#define CAMERA_BRIGHTNESS_STEP      5U          /*!< Sensor FPS */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/* Camera bus function prototypes */
typedef int32_t (*t_camera_bus_init_fn)(void);
typedef int32_t (*t_camera_bus_deinit_fn)(void);
typedef int32_t (*t_camera_bus_read_fn)(uint16_t, uint16_t, uint8_t*, uint16_t);
typedef int32_t (*t_camera_bus_write_fn)(uint16_t, uint16_t, uint8_t*, uint16_t);

/* Camera control function prototypes */
typedef int32_t (*t_camera_ctrl_start_fn)(void);
typedef void    (*t_camera_ctrl_power_fn)(bool);
typedef int32_t (*t_camera_ctrl_get_flip_fn)(uint8_t*);
typedef int32_t (*t_camera_ctrl_set_flip_fn)(uint8_t);
typedef int32_t (*t_camera_ctrl_get_brightness_fn)(uint16_t*);
typedef int32_t (*t_camera_ctrl_set_brightness_fn)(uint16_t);

/** Camera mirror */
typedef enum
{
  CAMERA_FLIP_NONE = 0,
  CAMERA_FLIP_H    = BIT(0U),
  CAMERA_FLIP_V    = BIT(1U),
} t_camera_flip;

/** Camera sensor ID */
typedef enum
{
  CAMERA_SENSOR_UNKNOWN = 0,
  CAMERA_SENSOR_IMX335,
  CAMERA_SENSOR_VD55G1,
  CAMERA_SENSOR_VD66GY,
  CAMERA_SENSOR_VD1943,
  CAMERA_SENSOR_MAX
} t_camera_sensor_id;

/** Generic rect */
typedef struct
{
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
} t_rect;

/** Camera IO */
typedef struct
{
  uint16_t                addr;
  t_camera_bus_init_fn    init;
  t_camera_bus_deinit_fn  deinit;
  t_camera_bus_read_fn    read;
  t_camera_bus_write_fn   write;
} t_camera_bus;

/** Camera control */
typedef struct
{
  t_camera_ctrl_start_fn          start;
  t_camera_ctrl_power_fn          power;
  t_camera_ctrl_get_flip_fn       get_flip;
  t_camera_ctrl_set_flip_fn       set_flip;
  t_camera_ctrl_get_brightness_fn get_brightness;
  t_camera_ctrl_set_brightness_fn set_brightness;
} t_camera_ctrl;

/** Camera sensor configuration */
typedef struct
{
  uint32_t      id;         /*!< Sensor ID */
  uint32_t      bpp;        /*!< Sensor BPP */
  uint32_t      fps;        /*!< Sensor FPS */
  uint32_t      width;      /*!< Image width */
  uint32_t      height;     /*!< Image height */
  uint32_t      csi_brate;  /*!< CSI bit rate */
  uint32_t      csi_lanes;  /*!< CSI data lanes */
  uint32_t      csi_dtype;  /*!< CSI data type */
  uint32_t      csi_dfmt;   /*!< CSI data format */
  uint8_t       flip;       /*!< Image flip setting */
  uint8_t       use_isp;    /*!< Use ISP */
  t_camera_bus  bus;        /*!< Sensor bus */
  t_camera_ctrl ctrl;       /*!< Sensor control */
} t_camera_sensor;

/** Camera pipe configuration */
typedef struct
{
  uint32_t id;        /*!< PIPE ID */
  uint32_t bpp;       /*!< Packer BPP */
  uint32_t width;     /*!< Image width */
  uint32_t height;    /*!< Image height */
  uint32_t packer;    /*!< Pixel packer */
  int32_t  gamma;     /*!< Gamma conversion enable */
  int32_t  rbswap;    /*!< Red-Blue swap enable */
  t_rect   sensor;    /*!< Effective sensor area */
} t_camera_pipe;

/** Camera instance */
typedef struct
{
  t_camera_sensor sensor;
  t_camera_pipe   dump;
  t_camera_pipe   main;
  t_camera_pipe   ancillary;
} t_camera;

/** Camera preview */
typedef struct
{
  bool   update;
  t_rect stream;
  t_rect stat_area;
} t_camera_preview;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

extern t_camera         camera;
extern t_camera_preview camera_preview;
extern const char       *camera_name[CAMERA_SENSOR_MAX];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Start the camera task.
 * @return Error code
 */
int32_t camera_task_start(void);

/**
 * @brief Get the pipe's current image buffer.
 *
 * @param pipe Pipe ID.
 * @return Image buffer
 */
uint8_t *camera_get_buffer(ULONG pipe);

/**
 * @brief Wait for a camera event.
 *
 * @param mask Event flag to capture
 * @param clear True to clear flag on read
 */
void camera_wait_event(ULONG mask, bool clear);

/*---------------------------------------------*/
/**
 * @brief Get if the camera uses ISP.
 *
 * @return True if used.
 */
bool camera_use_isp(void);

/**
 * @brief Get the camera name.
 *
 * @return Sensor name.
 */
const char* camera_get_name(void);

/**
 * @brief Cycle through color profiles.
 */
void camera_cycle_color_profile(void);

/**
 * @brief Get the sensor flip.
 * @param value Sensor flip configuration
 * @return 0 success, -1 error, -3 not supported
 */
int32_t camera_get_flip(uint8_t *flip);

/**
 * @brief Set the sensor flip.
 * @param value Sensor flip configuration
 * @return 0 success, -1 ISP error, -2 update not required, -3 not supported
 */
int32_t camera_set_flip(uint8_t flip);

/**
 * @brief Get the AEC configuration.
 * @param value AEC configuration
 * @return 0 success, -1 ISP error, -3 not supported
 */
int32_t camera_get_aec(ISP_AECAlgoTypeDef *value);

/**
 * @brief Set the AEC configuration.
 * @param value AEC configuration
 * @return 0 success, -1 ISP error, -2 update not required, -3 not supported
 */
int32_t camera_set_aec(ISP_AECAlgoTypeDef *value);

/**
 * @brief Get the AWB configuration.
 * @param value AWB configuration
 * @param size Number of profiles
 * @param idx Current profile ID
 * @return 0 success, -1 ISP error, -3 not supported
 */
int32_t camera_get_awb(ISP_AWBAlgoTypeDef *value, uint8_t *size, uint8_t *idx);

/**
 * @brief Set the AWB configuration.
 * @param enable AWB enable
 * @param idx Current profile ID
 * @return 0 success, -1 ISP error, -2 update not required, -3 not supported
 */
int32_t camera_set_awb(uint8_t enable, uint8_t idx);

/**
 * @brief Get the sensor gain.
 * @param value Sensor gain configuration
 * @return 0 success, -1 ISP error, -3 not supported
 */
int32_t camera_get_gain(ISP_SensorGainTypeDef *value);

/**
 * @brief Set the sensor gain.
 * @param value Sensor gain configuration
 * @return 0 success, -1 ISP error, -2 update not required, -3 not supported
 */
int32_t camera_set_gain(ISP_SensorGainTypeDef *value);

/**
 * @brief Get the sensor exposure.
 * @param value Sensor exposure configuration
 * @return 0 success, -1 ISP error, -3 not supported
 */
int32_t camera_get_exposure(ISP_SensorExposureTypeDef *value);

/**
 * @brief Set the sensor exposure.
 * @param value Sensor exposure configuration
 * @return 0 success, -1 ISP error, -2 update not required, -3 not supported
 */
int32_t camera_set_exposure(ISP_SensorExposureTypeDef *value);

/**
 * @brief Get the sensor brightness.
 * @param value Sensor brightness configuration
 * @return 0 success, -1 error, -3 not supported
 */
int32_t camera_get_brightness(uint16_t *value);

/**
 * @brief Set the sensor brightness.
 * @param value Sensor brightness configuration
 * @return 0 success, -1 ISP error, -2 update not required, -3 not supported
 */
int32_t camera_set_brightness(uint16_t value);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Camera -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _CAMERA_TASK_H_ */
