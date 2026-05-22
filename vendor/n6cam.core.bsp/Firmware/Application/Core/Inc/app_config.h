/**
 ******************************************************************************
 * @file    app_config.h
 * @author  SIANA Systems
 * @date    03/2026
 * @brief   Defines the API for the <DOC_TBD> module.
 *          This module is responsible for:
 *              - <DOC_TBD>
 *
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2026 SIANA Systems</center></h2>
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
#ifndef _INC_APP_CONFIG_H_
#define _INC_APP_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup SIANA
 * @{
 */
/** @addtogroup Application
 * @{
 */
/** @addtogroup AI
 * @{
 */

#include <stdbool.h>
#include "arm_math.h"

/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Definitions                                            */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

#define POSTPROCESS_TYPE                          POSTPROCESS_OD_YOLO_V8_UF

/* Postprocessing YOLOv8 object detection configuration */

/**  Number of classes in the detection model. */
#define AI_OD_YOLOV8_PP_NB_CLASSES               (1)

/**  Total number of boxes predicted by the model.
 * These are evaluated by the post-processing algorithm.
 */
#define AI_OD_YOLOV8_PP_TOTAL_BOXES              (756)

/** Maximum number of boxes per class to be considered after post-processing. */
#define AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT          (20)

/** Confidence threshold for filtering detections. High confidence helps filtering out low-confidence detections (False positives),
 * However, it is essential to balance the threshold value to ensure that you do not miss too many true positives.
 *
 * This value is set following the evaluation curves obtained from Ultralytics YOLOv8 model training. */
#define AI_OD_YOLOV8_PP_CONF_THRESHOLD           (0.3f)

/** Intersection over Union (IoU) threshold for Non-Maximum Suppression (NMS).
 * A high IoU threshold means that more overlapping will be allowed between boxes, while a lower threshold will allow less boxes to be retained.
 *
 * This value is set following the evaluation curves obtained from Ultralytics YOLOv8 model training. */
#define AI_OD_YOLOV8_PP_IOU_THRESHOLD            (0.7f)

/** Total number of classes recognized by the model.
 * Abstracted definition from the post-processing dependent definition.
 */
#define SAI_CLASS_NB                              AI_OD_YOLOV8_PP_NB_CLASSES

/** Total number of boxes predicted by the model.
  * Abstracted definition from the post-processing dependent definition.
  */
#define SAI_CLASS_TOTAL_PREDICTIONS               AI_OD_YOLOV8_PP_TOTAL_BOXES

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Types                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/**
 * @brief Model class definition for expose it to the application.
  */
typedef struct
{
  const char  *name;  /* Class name */
  uint32_t    color; /* ARGB8888 format */
} t_sai_class;


/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Data                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** Generic Name table for the selected classes from the classes recognized 
 *  by the model. 
 * 
 *  Usage example:
 * 
 *    ....
 *    #define SAI_CLASS_NB 15
 *
 *    static const t_sai_class SAI_CLASSES[SAI_CLASS_NB] = {
 *      [0]  = { "person" , 0xFFFFFFFF },
 *      --- All unselected classes here (1 - 9) are {NULL, 0} 
 *      [10] = { "dog"    , 0xFF00FFFF },
 *      --- All unselected classes here (11 - 14) are {NULL, 0} 
 *    };
 *    ...
 */
static const t_sai_class SAI_CLASSES[SAI_CLASS_NB] = {
  [0]  = { "person" , 0xFFFFFFFF }
};


/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_API                                                    */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_WEAK                                                   */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/** @} */
/*--->> END: PUBLIC API <<----------------------------------------------------*/

/** @} */
/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif   //  _INC_APP_CONFIG_H_
