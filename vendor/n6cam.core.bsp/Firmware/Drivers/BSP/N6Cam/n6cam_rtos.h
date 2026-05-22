/**
 *******************************************************************************
 * @file    n6cam_rtos.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam compatibility layer for RTOS
 * @note    Configures malloc lock/unlock
 *******************************************************************************
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
 *******************************************************************************
 */
#ifndef _N6CAM_RTOS_H_
#define _N6CAM_RTOS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_error.h"
#include "common.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup RTOS
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Get if the current function is on IRQ */
#define IS_IRQ_MODE() \
    (__get_IPSR() != 0U)

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
 * @brief Check if RTOS is initialized
 * @return true if RTOS is initialized, false otherwise
 */
bool rtos_is_initialized(void);

/**
 * @brief Raise an event
 * @param event Event object
 * @param mask  Event mask to raise
 */
void rtos_raise_event(TX_EVENT_FLAGS_GROUP *event, uint32_t mask);

/**
 * @brief Clear an event
 * @param event Event object
 * @param mask  Event mask to clear
 */
void rtos_clear_event(TX_EVENT_FLAGS_GROUP *event, uint32_t mask);

/**
 * @brief Wait for any event in the mask
 * @param event Event object
 * @param mask  Event mask to wait for
 * @param clear Clear the event mask that was raised
 * @return Event mask that was raised
 */
uint32_t rtos_wait_any_event(TX_EVENT_FLAGS_GROUP *event, uint32_t mask, bool clear);

/**
 * @brief Wait for all events in the mask
 * @param event Event object
 * @param mask  Event mask to wait for
 * @param clear Clear the event mask that was raised
 * @return Event mask that was raised
 */
uint32_t rtos_wait_all_events(TX_EVENT_FLAGS_GROUP *event, uint32_t mask, bool clear);

/**
 * @brief Capture a mutex
 * @param mutex   Mutex object
 * @param acquire True to acquire the lock, false to release it
 */
void rtos_mutex_acquire(TX_MUTEX *mutex, bool acquire);

/**
 * @brief Capture a semaphore
 * @param semaphore Semaphore object
 * @param acquire   True to acquire the lock, false to release it
 */
void rtos_semaphore_acquire(TX_SEMAPHORE *semaphore, bool acquire);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: RTOS -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_RTOS_H_ */
