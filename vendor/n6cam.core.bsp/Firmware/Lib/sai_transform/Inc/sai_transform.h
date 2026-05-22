/**
 ******************************************************************************
 * @file    sai_transform.h
 * @author  SIANA Systems
 * @date    06/2025
 * @brief   Defines the API for the SAI Transform module.
 *          This module is responsible for scaling NN detections
 *          to coordinates relative to display frame.
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
#ifndef _INC_SAI_TRANSFORM_H_
#define _INC_SAI_TRANSFORM_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup SIANA
 * @{
 */
/** @addtogroup Controllers
 * @{
 */
/** @addtogroup AI
 * @{
 */

#include <stdbool.h>
#include "nn_task.h"

/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Definitions                                            */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Types                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** Generic NN detection box compatible with CWH, TLWH formats */
typedef struct 
{
    float   x;     
    float   y;     
    float   width; 
    float   height;
    float   score;  
    int32_t class;  
    int32_t id;     
} t_sai_object;

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Data                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_API                                                    */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/**
 * @brief Transform a single Detection instance.
 * 
 * @param detect     Pointer to the structure containing the bounding box.
 * @param dst        Pointer to the destination structure.
 * 
 * @retval 0 if successful, 1 otherwise
 */
int8_t sai_transform(const t_nn_box *detect, t_sai_object * dst);

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

#endif   //  _INC_SAI_TRANSFORM_H_
