/**
 ******************************************************************************
 * @file    sai_transform_od.c
 * @author  SIANA Systems
 * @date    06/2025
 * @brief   Transform functions for Object Detection Instances
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

/** @addtogroup SIANA
 * @{
 */
/** @addtogroup Controllers
 * @{
 */
/** @addtogroup AI
 * @{
 */
/**
 * @defgroup PUBLIC_Definitions          PUBLIC constants
 * @defgroup PUBLIC_Macros               PUBLIC macros
 * @defgroup PUBLIC_Types                PUBLIC data-types
 * @defgroup PUBLIC_API                  PUBLIC API
 * @defgroup PUBLIC_WEAK                 PUBLIC Weak API
 * @defgroup PRIVATE_TUNABLES            Private compile-time tunables
 * @defgroup PRIVATE_Definitions         Private constants
 * @defgroup PRIVATE_Macros              Private macros
 * @defgroup PRIVATE_Types               Private data-types
 * @defgroup PRIVATE_Data                Private data / variables
 * @defgroup PRIVATE_Functions           Private functions
 * @defgroup PRIVATE_Weak                Private Weak functions
 */

#include <stdlib.h>

#include "camera_task.h"
#include "nn_task.h"
#include "sai_transform.h"
#include "sai_transform_utils.h"

/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_TUNABLES                                              */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Definitions                                           */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Macros                                                */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Types                                                 */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Functions                                             */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

void _init_reference_areas(void);
int8_t _scale_bbox(const t_sai_object * bbox_cwh_rel, t_sai_object * bbox_tlwh_px);

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Data                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/


/* Reference areas ---------------------------------------------------------- */

/* Unit square: reference frame for the original nn box */
t_frect _unit_square = { 0 };

/* NN Camera Pipe: size of the nn input layer */
t_frect _nn_campipe = { 0 };

/* NN Sensor Area: Area of the camera sensor that gets resized 
 * to the _nn_campipe
 */
t_frect _nn_sensorarea = { 0 };

/* Display Sensor Area: Area of the camera sensor that gets resized 
 * to the _disp_campipe
 */
t_frect _disp_sensorarea = { 0 };

/* Display Camera Pipe: size of the display image */
t_frect _disp_campipe = { 0 };


/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_API                                                    */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/**
 * @brief Transform a single Object Detection instance.
 * 
 * @param detect     Pointer to the structure containing the bounding box.
 * @param dst        Pointer to the destination structure.
 * 
 * @retval 0 if successful, 1 otherwise
 */
int8_t sai_transform_od(const t_nn_box *detect, t_sai_object * dst)
{
  int8_t ret = 0;
  
  /* Sanity Check */
  if (detect == NULL) 
  { 
    LERROR(TRACE_DISPLAY, "Invalid detection buffer"); 
    return 1;
  }

  /* Init Reference Areas */
  _init_reference_areas();

  /* Scale bounding box */
  t_sai_object box_cwh_rel = {
    .x = detect->x_center,
    .y = detect->y_center,
    .width = detect->width,
    .height = detect->height,
    .score = detect->conf,
    .class = detect->class_index,
  };
  t_sai_object box_tlwh_px = { 0 };
  
  ret = _scale_bbox(&box_cwh_rel, &box_tlwh_px);
  if (ret != 0) 
  { 
    LERROR(TRACE_DISPLAY, "Failed to display bbox"); 
    return 1; 
  }

  /* Copy to destination */
  dst->x = box_tlwh_px.x;
  dst->y = box_tlwh_px.y;
  dst->width = box_tlwh_px.width;
  dst->height = box_tlwh_px.height;
  dst->score = box_tlwh_px.score;
  dst->class = box_tlwh_px.class;   
  
  /* Return valid display */
  return 0;

}


/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_WEAK                                                   */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Functions                                             */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/**
 * @brief Initialize the reference areas for the display pipeline
 * @param  None
 */
void _init_reference_areas(void)
{
  /* Initialize the reference areas */

  /* Unit square: reference frame for the original nn box */
  _unit_square.x = 0;
  _unit_square.y = 0;
  _unit_square.width = 1;
  _unit_square.height = 1;

  /* NN Camera Pipe: size of the nn input layer */
  _nn_campipe.x = 0;
  _nn_campipe.y = 0;
  _nn_campipe.width = camera.ancillary.width;
  _nn_campipe.height = camera.ancillary.height;

  /* NN Sensor Area: Area of the camera sensor that gets resized 
   * to the _nn_campipe
   */
  _nn_sensorarea.x = camera.ancillary.sensor.x;
  _nn_sensorarea.y = camera.ancillary.sensor.y;
  _nn_sensorarea.width = camera.ancillary.sensor.width;
  _nn_sensorarea.height = camera.ancillary.sensor.height;

  /* Display Sensor Area: Area of the camera sensor that gets resized 
   * to the _disp_campipe
   */
  _disp_sensorarea.x = camera.main.sensor.x; 
  _disp_sensorarea.y = camera.main.sensor.y;
  _disp_sensorarea.width = camera.main.sensor.width;
  _disp_sensorarea.height = camera.main.sensor.height;

  /* Display Camera Pipe: size of the display image */
  _disp_campipe.x = 0;
  _disp_campipe.y = 0;
  _disp_campipe.width = camera.main.width;
  _disp_campipe.height = camera.main.height;

}

/**
 * @brief  Scales a bounding box from relative units to display pixel units. 
 * @param bbox_cwh_rel Original detection - Center-Width-Height format in relative units 
 * @param bbox_tlwh_px Scaled detection - Top-Left-Width-Height format in display pixel units 
 * @return 0 if succesful scaling, 1 otherwise
 */
int8_t _scale_bbox(const t_sai_object * bbox_cwh_rel, t_sai_object * bbox_tlwh_px)
{
  int8_t ret = 0;

  /* Sanity Check */
  if (bbox_cwh_rel == NULL || bbox_tlwh_px == NULL) { LERROR(TRACE_DISPLAY, "Invalid font"); return -1;}

  t_frect bbox = {
    .x = bbox_cwh_rel->x,
    .y = bbox_cwh_rel->y,
    .width = bbox_cwh_rel->width,
    .height = bbox_cwh_rel->height
  };

  /* Convert CWH to TLWH */
  ret = sai_transform_cwh_to_tlwh(&bbox, &bbox);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to convert CWH to TLWH"); return -1; }

  /* Scale & Clip to nn input size */
  ret = sai_transform_scale(&bbox, &bbox, &_unit_square, &_nn_campipe);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to scale bbox to nn input size"); return -1; }

  ret = sai_transform_clip(&bbox, &bbox, &_nn_campipe);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to clip bbox to nn campipe"); return -1; }

  /* Scale from nn input size to nn sensor size */
  ret = sai_transform_scale(&bbox, &bbox, &_nn_campipe, &_nn_sensorarea);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to scale bbox to nn sensor size"); return -1; }

  /* Change reference frame from self-reference to sensor area */
  ret = sai_transform_reframe(&bbox, &bbox, &_nn_campipe, &_nn_sensorarea);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to change reference frame to nn sensor area"); return -1; }

  /* Clip to display sensor area */
  ret = sai_transform_clip(&bbox, &bbox, &_disp_sensorarea);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to clip bbox to display sensor area"); return -1; }

  /* Change reference frame from disp sensor area to display area */
  ret = sai_transform_reframe(&bbox, &bbox, &_disp_sensorarea, &_disp_campipe);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to change reference frame to display area"); return -1; }

  /* Scale back to display resolution */
  ret = sai_transform_scale(&bbox, &bbox, &_disp_sensorarea, &_disp_campipe);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to scale bbox to display resolution"); return -1; }

  /* Clip to display area */
  ret = sai_transform_clip(&bbox, &bbox, &_disp_campipe);
  if (ret != 0) { LERROR(TRACE_DISPLAY, "Failed to clip bbox to display area"); return -1; }

  /* Update output box with scaled box */
  bbox_tlwh_px->x = bbox.x;
  bbox_tlwh_px->y = bbox.y;
  bbox_tlwh_px->width = bbox.width;
  bbox_tlwh_px->height = bbox.height;
  bbox_tlwh_px->score = bbox_cwh_rel->score;
  bbox_tlwh_px->class = bbox_cwh_rel->class;

  return ret;
}




/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Weak                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/** @} */
/*--->> END: Private Functions <<---------------------------------------------*/

/** @} */
/** @} */
/** @} */
