/**
 *******************************************************************************
 * @file    slib32_pid.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements a generic PID
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
#include <float.h>
#include <string.h>

#include "slib32_pid.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Middlewares
* @{
* @addtogroup PID
* @{
* @defgroup PUBLIC_Definitions          PUBLIC constants
* @defgroup PUBLIC_Macros               PUBLIC macros
* @defgroup PUBLIC_Types                PUBLIC data-types
* @defgroup PUBLIC_Data                 PUBLIC data / variables
* @defgroup PUBLIC_API                  PUBLIC API
* @defgroup PRIVATE_TUNABLES            PRIVATE compile-time tunables
* @defgroup PRIVATE_Definitions         PRIVATE constants
* @defgroup PRIVATE_Macros              PRIVATE macros
* @defgroup PRIVATE_Types               PRIVATE data-types
* @defgroup PRIVATE_Data                PRIVATE data / variables
* @defgroup PRIVATE_Functions           PRIVATE functions
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_TUNABLES
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t pid_init(t_pid *pid, float kp, float ki, float kd, float step)
{
  /* Validate */
  if (!pid || (step == 0))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Init */
  memset(pid, 0U, sizeof(t_pid));
  pid->kp         = kp;
  pid->ki         = ki * step;
  pid->kd         = kd / step;
  pid->step       = step;
  pid->int_max    = FLT_MAX;
  pid->int_min    = -FLT_MAX;
  pid->out_max    = FLT_MAX;
  pid->out_min    = -FLT_MAX;
  return SLIB32_OK;
}

int32_t pid_set_antiwindup(t_pid *pid, float max, float min)
{
  /* Validate */
  if (!pid)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Configure */
  pid->int_max = MAX(min, max);
  pid->int_min = MIN(min, max);
  return SLIB32_OK;
}

int32_t pid_set_out_limits(t_pid *pid, float max, float min)
{
  /* Validate */
  if (!pid)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Configure */
  pid->out_max = MAX(min, max);
  pid->out_min = MIN(min, max);
  return SLIB32_OK;
}

int32_t pid_reset(t_pid *pid)
{
  /* Validate */
  if (!pid)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Reset */
  pid->error   = 0.0f;
  pid->int_acc = 0.0f;
  return SLIB32_OK;
}

float pid_update(t_pid *pid, float error)
{
  /* Validate */
  if (!pid)
  {
    return 0.0f;
  }

  /* Prepare */
  pid->int_acc = MIN(MAX(pid->int_acc + error, pid->int_min), pid->int_max);

  /* Update */
  float p_term = pid->kp * error;
  float i_term = pid->ki * pid->int_acc;
  float d_term = pid->kd * (error - pid->error);
  pid->out     = MIN(MAX(p_term + i_term + d_term, pid->out_min), pid->out_max);
  pid->error   = error;

  return pid->out;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Middlewares -->
* @} <!-- End: PID -->
*//*--------------------------------------------------------------------------*/
