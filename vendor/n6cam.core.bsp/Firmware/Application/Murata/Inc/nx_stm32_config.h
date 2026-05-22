/**
 *******************************************************************************
 * @file    nx_stm32_config.h
 * @author  SIANA Systems
 * @brief   NetX thread definition
 *******************************************************************************
 */
#ifndef __NX_STM32_CONFIG_H__
#define __NX_STM32_CONFIG_H__
#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "tx_app.h"

/* Exported types ------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

/* DHCP device name
 * TODO: This needs to be changed manually on WHD middleware if updated, check
 * cy_netxduo.c:L1323 > dhcp_client_init() > nx_dhcp_create()
 */
#define NX_DHCP_NAME        "N6CAM"

/* NetX task */
#define NX_STACK_SIZE       (2U * 1024U)
#define NX_TASK_PRIO        APP_PRIO_THREADX_STACK
#define NX_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/* Address formats */
#define FMT_IPV4            "%lu.%lu.%lu.%lu"                 /*!< IPV4 address format */
#define FMT_MAC             "%02X:%02X:%02X:%02X:%02X:%02X"   /*!< MAC address format */
#define FMT_PORT            "%lu"                             /*!< Port format */

/* Exported macros -----------------------------------------------------------*/

/** Reverse IPV4 address */
#define REVERSE_IPV4(addr) \
  (uint32_t)( \
  (((addr) & 0xFF000000U) >> 24U) | \
  (((addr) & 0x00FF0000U) >>  8U) | \
  (((addr) & 0x0000FF00U) <<  8U) | \
  (((addr) & 0x000000FFU) << 24U) \
  )

/** Pack IPV4 address */
#define PACK_IPV4(a, b, c, d) \
  (uint32_t)(((d) << 24U) | ((c) << 16U) | ((b) << 8U) | (a))

/** Parse IPV4 address */
#define PARSE_IPV4(addr) \
  (addr & 0xFFU), ((addr >> 8U) & 0xFFU), ((addr >> 16U) & 0xFFU), ((addr >> 24U) & 0xFFU)

/** Print IPV4 address */
#define PRINT_IPV4(addr, info) \
  LINFO(TRACE_NX, "%s "FMT_IPV4, (info), PARSE_IPV4(addr))

/** Print IPV4 address and port */
#define PRINT_IPV4_PORT(addr, port, info) \
  LINFO(TRACE_NX, "%s "FMT_IPV4":"FMT_PORT, (info), PARSE_IPV4(addr), (port))

/** Parse MAC address */
#define PARSE_MAC(addr) \
  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]

/** Print MAC address */
#define PRINT_MAC(addr, info) \
  LINFO(TRACE_NX, "%s "FMT_MAC, (info), PARSE_MAC(addr))

/* Exported functions prototypes ---------------------------------------------*/

#ifdef __cplusplus
}
#endif
#endif  /* __NX_STM32_CONFIG_H__ */
