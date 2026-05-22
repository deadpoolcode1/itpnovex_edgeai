/**
 *******************************************************************************
 * @file    fx_stm32_config.h
 * @author  SIANA Systems
 * @brief   FileX thread definition
 *******************************************************************************
 */
#ifndef __FX_STM32_CONFIG_H__
#define __FX_STM32_CONFIG_H__
#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32n6xx_hal.h"
#include "tx_api.h"

/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

#define FX_STACK_SIZE           (2U * 1024U)
#define FX_TASK_PRIO            APP_PRIO_THREADX_STACK
#define FX_TASK_TIME_SLICE      APP_TIME_SLICE_DEFAULT

#define FX_SD_VOLUME_NAME       "N6CAM_SDCard"

/* Exported macros -----------------------------------------------------------*/

/* Exported functions prototypes ---------------------------------------------*/

#ifdef __cplusplus
}
#endif
#endif  /* __FX_STM32_CONFIG_H__ */
