/**
 *******************************************************************************
 * @file    slib32_pid.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for a generic PID
 * @note    Ref: https://www.wescottdesign.com/articles/pid/pidWithoutAPhd.pdf
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 SIANA Systems. All rights reserved.
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
#ifndef _SLIB32_PID_H_
#define _SLIB32_PID_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "slib32_core.h"
#include "slib32_error.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup PID
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

/** PID controller instance */
typedef struct
{
  float kp;        /*!< Proportional gain */
  float ki;        /*!< Integral gain */
  float kd;        /*!< Derivative gain */

  float int_acc;   /*!< Integral accumulator */
  float int_max;   /*!< Integral anti-windup max */
  float int_min;   /*!< Integral anti-windup min */

  float out;       /*!< Control signal */
  float out_max;   /*!< Control signal clamp max */
  float out_min;   /*!< Control signal clamp min */

  float error;     /*!< Last error */
  float step;      /*!< Time step in [sec] */
} t_pid;

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
 * @brief Init PID data structure
 * @param pid PID controller
 * @param kp Proportional gain
 * @param ki Integral gain
 * @param kd Derivative gain
 * @param step Time step in [sec]
 * @return Error code
 */
int32_t pid_init(t_pid *pid, float kp, float ki, float kd, float step);

/**
 * @brief Set anti-windup limits
 * @param pid PID controller
 * @param max Maximum value
 * @param min Minimum value
 * @return Error code
 */
int32_t pid_set_antiwindup(t_pid *pid, float max, float min);

/**
 * @brief Set output limits
 * @param pid PID controller
 * @param max Maximum value
 * @param min Minimum value
 * @return Error code
 */
int32_t pid_set_out_limits(t_pid *pid, float max, float min);

/**
 * @brief Reset PID controller
 * @param pid PID controller
 * @return Error code
 */
int32_t pid_reset(t_pid *pid);

/**
 * @brief Update PID controller
 * @param pid PID controller
 * @param error Error value
 * @return Control signal
 */
float pid_update(t_pid *pid, float error);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: PID -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_PID_H_ */
