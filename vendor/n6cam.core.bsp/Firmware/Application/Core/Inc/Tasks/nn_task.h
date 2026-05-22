/**
 ******************************************************************************
 * @file    nn_task.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the NN module.
 *          This module is responsible for:
 *          - Initialize NN model and PP
 *          - Run inference
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
#ifndef _NN_TASK_H_
#define _NN_TASK_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "app_postprocess.h"
#include "app_config.h"
#include "tx_app.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup NN
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* NN detection boxes */
#define NN_BOXES_MAX_NUM        AI_OD_YOLOV8_PP_MAX_BOXES_LIMIT

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

/** NN detection box */
typedef od_pp_outBuffer_t             t_nn_box;
typedef od_pp_out_t                   t_nn_boxes;
typedef od_yolov8_pp_static_param_t   t_nn_params;
/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Start the NN task.
 * @return Error code
 */
int32_t nn_task_start(void);

/**
 * @brief Get a NN detection box by index
 *
 * @param box_buff   Detected object
 * @return Box count
 */
uint32_t nn_get_detections(t_nn_box* box_buff);

/**
 * @brief Suspend the NN task. While suspended it stops consuming camera
 *        frames and stops reading model weights from xSPI NOR — required
 *        before disabling xSPI memory-mapped mode (e.g., self-updater).
 *        Returns ThreadX status.
 */
uint32_t nn_task_suspend_thread(void);

/**
 * @brief Resume a previously suspended NN task. Not strictly required by
 *        the self-updater (we reset right after), but kept for symmetry.
 */
uint32_t nn_task_resume_thread(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: NN -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _NN_TASK_H_ */
