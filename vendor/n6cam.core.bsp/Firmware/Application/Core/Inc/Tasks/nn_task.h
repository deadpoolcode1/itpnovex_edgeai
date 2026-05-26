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

/* NN detection boxes. Kept small (published set is clamped to this in
 * _pp_publish_objects) because the shell `frame run` handler stacks a
 * t_nn_box buf[NN_BOXES_MAX_NUM] on its 2 KB task stack. Plenty for a
 * people/vehicle counter — the count saturates here per frame. */
#define NN_BOXES_MAX_NUM        (20)

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
/* PP static-param type must match the selected POSTPROCESS_TYPE so the
 * single _pp_params instance in nn_task.c is sized/laid-out correctly. */
#if POSTPROCESS_TYPE == POSTPROCESS_OD_SSD_UF
typedef od_ssd_pp_static_param_t      t_nn_params;
#elif POSTPROCESS_TYPE == POSTPROCESS_OD_YOLO_V8_UF
typedef od_yolov8_pp_static_param_t   t_nn_params;
#else
#error "t_nn_params: unsupported POSTPROCESS_TYPE"
#endif
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

/**
 * @brief Enable/disable runtime detection (SoW §3.1 detect start/stop).
 *        When disabled, the NN task still drains camera frames but skips
 *        the inference + post-processing path. Cheap CPU gating.
 */
void nn_task_detect_set(bool enable);
bool nn_task_detect_get(void);

/**
 * @brief Set the SoW §4.2 'action_msk' for on-detection side effects.
 *        bit0 = save to SD (W12), bit1 = report cellular (W11/W13).
 *        Checked on each new-object edge. 0 = no side effects.
 */
void nn_task_action_set(uint8_t mask);

/**
 * @brief Set the SoW §4.2 'det_msk' (proposal W5/W6 class filter).
 *        bit0 = people, bit1 = vehicles. Detections whose class index
 *        falls outside the mask are dropped after post-processing.
 *        With the current people-only model nb_classes=1 so all
 *        detections are people; bit1 has no effect until a multi-class
 *        model is in place.
 */
void nn_task_det_set(uint8_t mask);

/**
 * @brief Inject a synthetic detection edge. Used to test the
 *        detect -> snapshot -> SD -> notify chain when the camera
 *        optics are subpar (out of focus, dirty lens, etc.) so real
 *        NN detection isn't reliable. Fires exactly like a real
 *        detection: edge-trigger, snapshot if action_msk bit0 set,
 *        +OBJDET trace, +SDVRNTF notification.
 *
 * @param boxes Number of synthetic boxes to claim were detected.
 *              Pass >= 1 to trigger; 0 to clear the edge.
 */
void nn_task_simulate_detection(uint32_t boxes);

/**
 * @brief Inject a real test image into the NN pipeline.
 *        The NN task uses `frame` as the input instead of the camera's
 *        ancillary buffer until the override is cleared (NULL). Buffer
 *        layout must match CAMERA_ANCILLARY_BUFFER_SIZE (192*192*3 RGB).
 *        Lets the user prove the detection algorithm works on a known
 *        scene while the camera optics are being tuned.
 *
 *        Cache-flush of the supplied buffer is the caller's responsibility.
 *
 * @param frame Buffer pointer, or NULL to revert to live camera.
 */
void nn_task_set_test_frame(uint8_t *frame);

/**
 * @brief Return the most recent post-processed detection count (for
 *        querying after a 'frame run').
 */
uint32_t nn_task_get_box_count(void);

/**
 * @brief Debug: copy out a snapshot of the most recent NN output tensor.
 *        First 16 floats + last 16 floats + total byte count. Used by
 *        the 'nn dump' shell command to diagnose detection failures.
 */
void nn_task_dump_output(float *head_out, float *tail_out, uint32_t *bytes_out);

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
