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
#define AI_OD_YOLOV8_PP_NB_CLASSES               (80)

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
#define AI_OD_YOLOV8_PP_CONF_THRESHOLD           (0.30f)

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

/** Output dequantize zero-point correction.
 *
 * stedgeai 4.0's `DequantizeLinear` epoch at the model's output does
 * `float = int8 * scale` and drops the `- zero_point` step. For the
 * relu30 model the int8 output has scale=0.0131 and zero_point=-118,
 * so every channel comes out shifted low by `-zero_point * scale =
 * 118 * 0.0131 = 1.5495`. Without this correction the post-proc sees
 * bbox values around (-1.2, -1.0, -0.9, -0.7) instead of normalized
 * cx/cy/w/h, NMS can't deduplicate (no valid overlap math) and the
 * class confidences are pulled below the detection threshold.
 *
 * Determined empirically by comparing kit `frame dump` head with the
 * host TFLite output of the same model on the same input. The
 * constant matches `-Dequantize_581_x_zero_point * Dequantize_581_
 * x_scale` from the compiled `network.c` weight metadata. Re-derive
 * if you swap the model. */
#define AI_OD_YOLOV8_DEQUANT_BIAS                 (1.5495f)

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
  [0]  = { "person"     , 0x00FF00FFu },
  [2]  = { "car"        , 0x0000FFFFu },
  [3]  = { "motorcycle" , 0x00FFFFFFu },
  [4]  = { "airplane"   , 0x00C0C0FFu },  /* sometimes a misclassified car */
  [5]  = { "bus"        , 0xFFFF00FFu },
  [6]  = { "train"      , 0x80C0FFFFu },  /* sometimes a misclassified car */
  [7]  = { "truck"      , 0xFF7F00FFu },
  [8]  = { "boat"       , 0x40A0FFFFu },
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
