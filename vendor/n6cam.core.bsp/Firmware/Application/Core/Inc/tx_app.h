/**
 ******************************************************************************
 * @file    tx_app.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   ThreadX application initialization
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
#ifndef _TX_APP_H_
#define _TX_APP_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "common.h"
#include "main.h"
#include "n6cam_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Application
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Default time slice for tasks */
#define APP_TIME_SLICE_DEFAULT    10U

/** Default priority definition for tasks */
#define APP_PRIO_DEFAULT          (TX_MAX_PRIORITIES / 2U)

/* Task priority definitions */
#define APP_PRIO_CRITICAL         (APP_PRIO_DEFAULT - 2U)   /*!< Critical tasks       : NN, Watchdog */
#define APP_PRIO_IMPORTANT        (APP_PRIO_DEFAULT - 1U)   /*!< Important tasks      : Camera, Display */
#define APP_PRIO_USER_INTERFACE   (APP_PRIO_DEFAULT + 1U)   /*!< User interface tasks : Shell, SCOM */
#define APP_PRIO_THREADX_STACK    (APP_PRIO_DEFAULT + 2U)   /*!< ThreadX stack tasks  : FX, NX, UX */
#define APP_PRIO_BACKGROUND       (TX_MAX_PRIORITIES - 1U)  /*!< Background tasks     : System, Test */

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

/**
 * @brief Task start function type. Define the entry point for tasks.
 * @return Error code
 */
typedef int32_t (*t_task_start_fn)(void);

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
 * @brief Raise a task event.
 * @param mask  Event mask to raise
 */
void task_raise_event(uint32_t mask);

/**
 * @brief Wait until a tasks event is raised.
 * @param mask  Event flag to capture
 */
void task_wait_event(uint32_t mask);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Application -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _TX_APP_H_ */
