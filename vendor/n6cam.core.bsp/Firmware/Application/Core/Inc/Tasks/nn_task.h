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
uint32_t nn_get_detections(t_nn_box* box_buff, uint32_t box_cap);

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
 *        bit0 = save to SD (W12), bit1 = report cellular (W11/W13),
 *        bit2 = upload the photo to the remote server.
 *
 *        bit2 is SoW §3.1's "taking photo and sending to remote server on
 *        detection of new objects", which the §4.2 mask table never gave a
 *        bit — so until now the only way to get a picture off the device
 *        was to type `photo upload`. It is rate-limited internally; the
 *        transfer is far slower than the scene it describes.
 *
 *        Checked on each change of the debounced count. bit0 and bit2 do
 *        nothing when that count changes to zero: there is nobody to
 *        photograph. 0 = no side effects.
 */
void nn_task_action_set(uint8_t mask);

/**
 * @brief Automatic-upload counters: photos dropped by the rate floor, and
 *        photos dropped because the capture pipeline was already busy.
 *        Both are normal under load; a climbing 'skipped' means the scene
 *        is changing faster than the link can carry pictures of it.
 */
void nn_task_upload_stats(uint32_t *skipped, uint32_t *busy);

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
 * @brief Turn main-path tiled detection on or off (ScopusQA #22).
 *
 *        Off (the historical behaviour): one inference per camera frame on the
 *        256x256 ancillary, which is the whole field of view downscaled. An
 *        object smaller than roughly half the frame is below the detection
 *        cliff measured in ScopusQA #17.
 *
 *        On: each camera frame carries one tile of a 4x3 sweep over the live
 *        main pipe, and the merged, NMS-ed result of the whole sweep is what
 *        drives the counts, the overlay and the §4.2 notifications. ~12
 *        inferences per detection, about 1.1 s — the rate this was asked for.
 *
 *        Switching modes abandons any sweep in flight and clears the published
 *        set, so the first result after a switch is a fresh one and the change
 *        cannot fabricate an edge.
 *
 *        An armed test frame suspends tiling for as long as it is armed:
 *        `frame run` and the injection suites need the single-frame path.
 */
void nn_task_tile_set(bool enable);

/** @brief The debounce window actually in force, in ms. Tile mode floors it at
 *         two sweeps, because a window shorter than the sampling period
 *         debounces nothing. */
uint32_t nn_task_debounce_effective(void);

/** @brief Is main-path tiling on? */
bool nn_task_tile_get(void);

/**
 * @brief Sweep counters: how many sweeps have completed, how long the last one
 *        took in ms, and how many tiles it was. Any pointer may be NULL.
 */
void nn_task_tile_stats(uint32_t *sweeps, uint32_t *last_ms, uint32_t *tiles);

/**
 * @brief Read back the SoW §4.2 'det_msk' set by nn_task_det_set().
 *        The live-view overlay uses it to decide which counts to show:
 *        a vehicles-only profile has no business printing a people count
 *        (ScopusQA #16).
 */
uint8_t nn_task_det_get(void);

/**
 * @brief Current debounced per-class counts — the same two numbers the
 *        §4.2 notifications carry in 'rsd'. Either pointer may be NULL.
 *
 *        Exposed so the overlay and the notification cannot disagree: they
 *        now read one source. Before this the screen printed a single
 *        "Objects: N" total, so a frame with two people and a car showed 3
 *        and the events said 2 and 1, and there was no way to tell from
 *        the picture which number the receiver would get.
 */
void nn_task_counts_get(uint32_t *people, uint32_t *vehicles);

/**
 * @brief Set how long a new detection count must hold before it is believed
 *        and reported (SoW §4.2).
 *
 *        The reporting rule is "every change of the debounced count", in
 *        both directions — 3 -> 4 and 4 -> 3 alike, and 0 when the last
 *        person leaves. Reporting only the 0 -> N arrival, as the firmware
 *        did before, means somebody joining a group already in view raises
 *        nothing; reporting every frame's raw count means a detector that
 *        flickers for one frame raises an event every few seconds. The
 *        window is what separates the two.
 *
 *        Measured on the bench with both faults live: a static scene
 *        produced an event every few seconds, faster than the modem could
 *        drain them, while a person walking up to people already in frame
 *        produced none at all.
 *
 * @param ms Milliseconds the new count must hold continuously. Default
 *           1000. 0 disables the wait and reports every frame's change.
 */
void nn_task_debounce_set(uint32_t ms);
uint32_t nn_task_debounce_get(void);

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
 * @brief As nn_task_simulate_detection, for a chosen COCO class.
 *
 * Lets the bench exercise the vehicle path (SoW §4.2 `0x20`) without a car
 * in front of the lens. `class_index` 0 is person; 2 is car.
 */
void nn_task_simulate_detection_class(uint32_t boxes, int32_t class_index);

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
 *        The override EXPIRES. It lapses NN_TEST_FRAME_TTL_S seconds after
 *        the last call, and every call restarts that clock, so a sequence of
 *        'frame run' / tile sweeps holds it for as long as the sequence
 *        lasts and no longer. This is deliberate: an override that outlives
 *        the test that set it leaves the product inferring on a photograph
 *        while appearing to watch the room, and nothing downstream — not the
 *        live view, not the detection notifications — can tell the
 *        difference. A test frame is a test frame; it should not survive the
 *        test.
 *
 * @param frame Buffer pointer, or NULL to revert to live camera.
 */
void nn_task_set_test_frame(uint8_t *frame);

/**
 * @brief Count of inferences that have actually run ON the injected frame.
 *
 *        `frame run` used to arm the override, sleep 100 ms and read the box
 *        buffer. 100 ms is one inference at best, and the loop is driven by
 *        the camera's frame event, not by the arming — so the read landed on
 *        whatever the NN had last produced, which on a busy lens is the LIVE
 *        SCENE. A sweep of 36 test images came back with the same two boxes
 *        at the same two confidences for image after image: the lab monitor,
 *        not the pictures. An injection test that silently reports the room
 *        is worse than no injection test.
 *
 *        Sample this before arming and wait for it to move.
 */
uint32_t nn_task_test_frame_seq(void);

/**
 * @brief Let an injected test frame drive the §4.2 actions (snapshot, photo
 *        upload, notification) the same way a live detection does.
 *
 *        Off by default, and that default is the safe one: a test frame is
 *        not the scene, and a bench injecting `3_people.jpg` must not put
 *        three people on the customer's server. So the live loop mutes the
 *        action path whenever the input came from the override.
 *
 *        The consequence was that the one path the product exists for —
 *        the NN sees objects and an event leaves the device unaided — could
 *        not be tested at all without walking real people in front of the
 *        lens. `detect simulate` reaches the notification but skips the
 *        detector; `notify trigger` skips both. This switch closes that,
 *        deliberately and for one test at a time.
 *
 *        It is cleared whenever the test-frame override is set or cleared,
 *        so it cannot outlive the test that asked for it — same reasoning
 *        as the override's own TTL.
 */
void nn_task_test_frame_report_set(bool enable);
bool nn_task_test_frame_report_get(void);

/**
 * @brief Whether a test frame is currently driving the NN.
 *        True means the boxes being reported and drawn describe an injected
 *        picture, NOT what the lens sees. Callers that show detections to a
 *        human, or report them as real events, must say so or suppress them.
 */
bool nn_task_test_frame_active(void);

/**
 * @brief Seconds left before the test-frame override lapses, 0 if none.
 */
uint32_t nn_task_test_frame_remaining_s(void);

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
