/**
 *******************************************************************************
 * @file    ux_stm32_config.h
 * @author  SIANA Systems
 * @brief   USBX thread definition
 *******************************************************************************
 */
#ifndef __UX_STM32_CONFIG_H__
#define __UX_STM32_CONFIG_H__
#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32n6xx_hal.h"
#include "tx_api.h"

/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

#define UX_STACK_SIZE                   (32U * 1024U)

#define UX_DCD_STM32_MAX_ED             9U                /*!< DCD max. # of endpoints */
#define USBD_HAL_ISOINCOMPLETE_CALLBACK                   /*!< Required for UVC */

/* Exported macros -----------------------------------------------------------*/

#define UX_UVC_INTERVAL(n)            (10000000U / (n))

/* Exported functions prototypes ---------------------------------------------*/

#ifdef __cplusplus
}
#endif
#endif  /* __UX_STM32_CONFIG_H__ */
