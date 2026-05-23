/**
 ******************************************************************************
 * @file    snapshot_task.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the snapshot capture module.
 *          This module is responsible for:
 *          - Receive user commands to capture a snapshot
 *          - Requesting a JPEG encoded frame
 *          - Storing the snapshot
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
#ifndef _SNAPSHOT_TASK_H_
#define _SNAPSHOT_TASK_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "draw.h"
#include "tx_app.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Snapshot
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

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
 * @brief Start the snapshot task.
 * @return Error code
 */
int32_t snapshot_task_start(void);

/**
 * @brief Create a snapshot
 * @param draw Drawing context to use for the snapshot
 */
void snapshot_create(t_draw *draw);

/**
 * @brief Request a new snapshot
 * @return True if the snapshot was requested, false otherwise
 */
bool snapshot_trigger(void);

/**
 * @brief Atomic "claim a snapshot slot with this filename" — the
 *        snapshot_set_filename / snapshot_trigger pair is racy when
 *        multiple producers compete (shell `photo savesd` vs nn_task's
 *        auto-detect path): one caller can win the trigger and another
 *        can clobber its filename a microsecond later because the
 *        filename slot is a single static buffer.
 *
 *        This call sets the filename and triggers in one critical
 *        section; if the snapshot pipeline isn't AVAILABLE, returns
 *        false WITHOUT touching the pending filename.
 *
 * @param filename  Name to use for this snapshot, or NULL to fall
 *                  back to the default Snapshot{idx}.jpeg
 * @return true if the slot was claimed (trigger fired), false if the
 *         pipeline is busy or no SD card is present
 */
bool snapshot_request(const char *filename);

/**
 * @brief Override the next snapshot's filename (SoW §7 timestamped names).
 *        Pass NULL to revert to the default 'Snapshot{idx}.jpeg'. The
 *        override is consumed by the next snapshot then cleared.
 *
 * @note  Caller owns the storage; snapshot_task copies it.
 */
void snapshot_set_filename(const char *filename);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Snapshot -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SNAPSHOT_TASK_H_ */
