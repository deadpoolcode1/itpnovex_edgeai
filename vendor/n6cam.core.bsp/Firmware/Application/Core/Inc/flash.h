/**
 *******************************************************************************
 * @file		flash.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   NOR flash handler for the N6Cam firmware
 * @warning If using this resource, make sure to acquire the FLASH semaphore
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */
#ifndef _FLASH_H_
#define _FLASH_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/* Public definitions --------------------------------------------------------*/

#define FLASH_SIZE        (64U * 1024U * 1024U)                 /*!< Total size: 64M */
#define FLASH_BLOCK_SIZE  (4U * 1024U)                          /*!< Block size: 4K */

#define FLASH_ADDR_START  (XSPI2_BASE)                          /*!< Start address */
#define FLASH_ADDR_END    (FLASH_ADDR_START + FLASH_SIZE - 1U)  /*!< End address */

/* Public macros -------------------------------------------------------------*/

/* Public types --------------------------------------------------------------*/

/* Public data ---------------------------------------------------------------*/

/* Public API ----------------------------------------------------------------*/

/**
 * @brief Initialize the flash handler
 */
void flash_init(void);

/**
 * @brief Acquire or release the flash for reading
 * @param acquire true to acquire, false to release
 */
void flash_acquire(bool acquire);

/**
 * @brief Read data from flash
 * @param addr Address to read from
 * @param buff Destination buffer
 * @param size Number of bytes to read
 * @return Number of bytes read, or error code
 */
int32_t flash_read(uint32_t addr, uint8_t *buff, size_t size);

/**
 * @brief Write data to flash
 * @param addr Address to write to
 * @param buff Source buffer
 * @param size Number of bytes to write
 * @return Number of bytes written, or error code
 */
int32_t flash_write(uint32_t addr, const uint8_t *buff, size_t size);

/*----------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif /* _FLASH_H_ */
