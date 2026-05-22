/**
 *******************************************************************************
 * @file    n6cam_rtc.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam RTC API
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
#ifndef _N6CAM_RTC_H_
#define _N6CAM_RTC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup RTC
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* RTC year offset */
#define DATETIME_YEAR_OFFSET          2000U

/* Datetime limits */
#define DATETIME_YEAR_MAX             99U
#define DATETIME_MONTH_MIN            1U
#define DATETIME_MONTH_MAX            12U
#define DATETIME_DAY_MIN              1U
#define DATETIME_DAY_MAX              31U
#define DATETIME_HOURS_MAX            23U
#define DATETIME_MINUTES_MAX          59U
#define DATETIME_SECONDS_MAX          59U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/* Datetime validation */
#define IS_DATETIME_YEAR(year)        ((year) <= DATETIME_YEAR_MAX)
#define IS_DATETIME_MONTH(month)      (((month) >= DATETIME_MONTH_MIN) && ((month) <= DATETIME_MONTH_MAX))
#define IS_DATETIME_DAY(day)          (((day) >= DATETIME_DAY_MIN) && ((day) <= DATETIME_DAY_MAX))
#define IS_DATETIME_HOURS(hours)      ((hours) <= DATETIME_HOURS_MAX)
#define IS_DATETIME_MINUTES(minutes)  ((minutes) <= DATETIME_MINUTES_MAX)
#define IS_DATETIME_SECONDS(seconds)  ((seconds) <= DATETIME_SECONDS_MAX)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/** RTC datetime */
typedef struct
{
  uint8_t year;    /*!< Date year.    Range: 0 - 99 */
  uint8_t month;   /*!< Date day.     Range: 1 - 12 */
  uint8_t day;     /*!< Date day.     Range: 1 - 31 */
  uint8_t hours;   /*!< Time hour.    Range: 0 - 23 */
  uint8_t minutes; /*!< Time minutes. Range: 0 - 59 */
  uint8_t seconds; /*!< Time seconds. Range: 0 - 59 */
} t_datetime;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initialize RTC
 * @return Error code
 */
int32_t bsp_rtc_init(void);

/**
 * @brief Get RTC time
 * @param datetime Time in datetime format
 * @return Error code
 */
int32_t bsp_rtc_get_time(t_datetime *datetime);

/**
 * @brief Set RTC time
 * @param datetime Time in datetime format
 * @return Error code
 */
int32_t bsp_rtc_set_time(t_datetime *datetime);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: RTC -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_RTC_H_ */
