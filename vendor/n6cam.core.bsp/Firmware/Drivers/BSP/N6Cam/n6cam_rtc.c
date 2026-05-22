/**
 *******************************************************************************
 * @file    n6cam_rtc.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam RTC API
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
#include "n6cam_rtc.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup RTC
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

RTC_HandleTypeDef       hrtc;
static TX_MUTEX         _rtc_mtx;
static RTC_DateTypeDef  _rtc_date = { 0 };
static RTC_TimeTypeDef  _rtc_time = { 0 };
static bool             _rtc_init = false;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

extern void HAL_RTC_MspInit(RTC_HandleTypeDef *phrtc);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_rtc_init(void)
{
  int32_t status;

  /* Validate */
  if (_rtc_init)
  {
    return BSP_OK;
  }

  /* Init Mutex */
  status = tx_mutex_create(&_rtc_mtx, "tx.mtx.rtc", TX_NO_INHERIT);
  if (status != TX_SUCCESS)
  {
    return BSP_ERROR_COMPONENT;
  }

  /* Init RTC */
  hrtc.Instance             = RTC;
  hrtc.Init.BinMode         = RTC_BINARY_NONE;
  hrtc.Init.HourFormat      = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv    = 127;
  hrtc.Init.SynchPrediv     = 255;
  hrtc.Init.OutPut          = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity  = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType      = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutRemap     = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPullUp    = RTC_OUTPUT_PULLUP_NONE;
  status = HAL_RTC_Init(&hrtc);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Enable reference clock input */
  status = HAL_RTCEx_SetRefClock(&hrtc);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  _rtc_init = true;
  return BSP_OK;
}

int32_t bsp_rtc_get_time(t_datetime *datetime)
{
  int32_t status;

  /* Validate */
  if (!_rtc_init)
  {
    return BSP_ERROR_NO_INIT;
  }
  if (!datetime)
  {
    /* Invalid parameter */
    return BSP_ERROR_PARAMETER;
  }

  /* Capture RTC */
  rtos_mutex_acquire(&_rtc_mtx, true);

  /* Get date/time
   * Note: HAL GetTime must be called before GetDate to unlock the values
   */
  status = HAL_RTC_GetTime(&hrtc, &_rtc_time, RTC_FORMAT_BIN);
  if (status != HAL_OK)
  {
    status = BSP_ERROR_PERIPHERAL;
    goto finish;
  }
  status = HAL_RTC_GetDate(&hrtc, &_rtc_date, RTC_FORMAT_BIN);
  if (status != HAL_OK)
  {
    status = BSP_ERROR_PERIPHERAL;
    goto finish;
  }

  /* Convert to datetime */
  datetime->year    = _rtc_date.Year;
  datetime->month   = _rtc_date.Month;
  datetime->day     = _rtc_date.Date;
  datetime->hours   = _rtc_time.Hours;
  datetime->minutes = _rtc_time.Minutes;
  datetime->seconds = _rtc_time.Seconds;
  status            = BSP_OK;

finish:
  /* Release RTC and return */
  rtos_mutex_acquire(&_rtc_mtx, false);
  return status;
}

int32_t bsp_rtc_set_time(t_datetime *datetime)
{
  int32_t status;

  /* Validate */
  if (!_rtc_init)
  {
    return BSP_ERROR_NO_INIT;
  }
  if (
    !datetime ||
    !IS_DATETIME_YEAR(datetime->year) ||
    !IS_DATETIME_MONTH(datetime->month) ||
    !IS_DATETIME_DAY(datetime->day) ||
    !IS_DATETIME_HOURS(datetime->hours) ||
    !IS_DATETIME_MINUTES(datetime->minutes) ||
    !IS_DATETIME_SECONDS(datetime->seconds)
  )
  {
    /* Invalid parameter */
    return BSP_ERROR_PARAMETER;
  }

  /* Capture RTC */
  rtos_mutex_acquire(&_rtc_mtx, true);

  /* Convert to RTC format */
  _rtc_date.Year    = datetime->year;
  _rtc_date.Month   = datetime->month;
  _rtc_date.Date    = datetime->day;
  _rtc_date.WeekDay = 1U;
  _rtc_time.Hours   = datetime->hours;
  _rtc_time.Minutes = datetime->minutes;
  _rtc_time.Seconds = datetime->seconds;

  /* Set date/time */
  status = HAL_RTC_SetDate(&hrtc, &_rtc_date, RTC_FORMAT_BIN);
  if (status != HAL_OK)
  {
    status = BSP_ERROR_PERIPHERAL;
  }
  status = HAL_RTC_SetTime(&hrtc, &_rtc_time, RTC_FORMAT_BIN);
  if (status != HAL_OK)
  {
    status = BSP_ERROR_PERIPHERAL;
  }

  /* Release RTC and return */
  rtos_mutex_acquire(&_rtc_mtx, false);
  return status;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initialize the RTC MSP
 * @param phrtc RTC handle
 */
void HAL_RTC_MspInit(RTC_HandleTypeDef *phrtc)
{
  UNUSED(phrtc);

  /* Clock enable */
  __HAL_RCC_RTC_ENABLE();
  __HAL_RCC_RTC_CLK_ENABLE();
  __HAL_RCC_RTCAPB_CLK_ENABLE();
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: RTC -->
*//*--------------------------------------------------------------------------*/
