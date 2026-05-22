/**
 *******************************************************************************
 * @file    flash.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   NOR flash handler for the N6Cam firmware
 * @warning If using this resource, make sure to acquire the FLASH semaphore
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */
#include "flash.h"
#include "n6cam_rtos.h"
#include "n6cam_xspi.h"

/* Private tunables ----------------------------------------------------------*/

#define FLASH_RX_SLOTS      32U     /* Arbitrary number of read slots, not a real limitation */

#define FLASH_TX_CRITICAL   0U      /* Enable critical section during write operations (1) or not (0) */
#define FLASH_TX_MODE       1U      /* Enable block-by-block write mode (1) or single write mode (0) */

/* Private definitions -------------------------------------------------------*/

/* Private macros ------------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/** Flash instance */
typedef struct
{
  TX_MUTEX      mtx;      /*!< TX mutex: Only one can write at a given time */
  TX_SEMAPHORE  sem;      /*!< RX semaphore: But it must wait until all readers are done */
} t_flash;

/* Private data --------------------------------------------------------------*/

static t_flash _flash;    /*!< Flash instance */

/* Private function ----------------------------------------------------------*/

void _flash_read_protect(bool acquire, size_t slots);
void _flash_write_protect(bool acquire);

/* Public API definitions ----------------------------------------------------*/

void flash_init(void)
{
  int32_t status;
  status = tx_mutex_create(&_flash.mtx, "tx.mtx.flash", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
  status = tx_semaphore_create(&_flash.sem, "tx.sem.flash", FLASH_RX_SLOTS);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

void flash_acquire(bool acquire)
{
  _flash_read_protect(acquire, 1U);
}

int32_t flash_read(uint32_t addr, uint8_t *buff, size_t size)
{
  /* Validate */
  if (!buff || (addr < FLASH_ADDR_START) || ((addr + size - 1U) > FLASH_ADDR_END))
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (size == 0U)
  {
    return SLIB32_OK;
  }

  /* Acquire */
  _flash_read_protect(true, 1U);

  /* On mapped mode: Simply copy the data */
  memcpy(buff, (uint8_t*)addr, size);

  /* Release */
  _flash_read_protect(false, 1U);
  return size;
}

int32_t flash_write(uint32_t addr, const uint8_t *buff, size_t size)
{
  #if FLASH_TX_CRITICAL == 1U
  uint32_t mask;
  uint32_t prio;
  #endif /* FLASH_TX_CRITICAL */
  int32_t  status;

  /* Validate */
  if (!buff || (addr < FLASH_ADDR_START))
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (size == 0U)
  {
    return SLIB32_OK;
  }

  /* Configure:
   * - address > flash offset
   * - size limited to maximum possible read
   */
  addr  = (addr - FLASH_ADDR_START);
  size  = MIN(size, FLASH_SIZE - addr);

  /* Acquire and ensure no one is reading */
  _flash_write_protect(true);
  _flash_read_protect(true, FLASH_RX_SLOTS);

  #if FLASH_TX_CRITICAL == 1U
  /* TODO: Critical section - Enter */
  mask = tx_interrupt_control(TX_INT_DISABLE);
  tx_thread_preemption_change(tx_thread_identify(), 0, (UINT*)&prio);
  #endif /* FLASH_TX_CRITICAL */

  /* Disable mapped mode */
  status = BSP_XSPI_NOR_DisableMemoryMappedMode(0);
  if (status != BSP_OK)
  {
    LERROR(TRACE_SYSTEM, "Disable memory mapped mode (%d)", status);
    Error_Handler();
  }

#if FLASH_TX_MODE == 1U
  /* Process block by block */
  uint32_t block;
  uint32_t offset = 0U;
  size_t   chunk;
  size_t   remaining = size;
  while (remaining)
  {
    /* Erase block */
    block  = addr & ~(FLASH_BLOCK_SIZE - 1U);
    status = BSP_XSPI_NOR_Erase_Block(0, block, BSP_XSPI_NOR_ERASE_4K);
    if (status != BSP_OK)
    {
      LERROR(TRACE_SYSTEM, "Erase block 0x%08X (%d)", block, status);
      Error_Handler();
    }

    /* Write chunk */
    chunk  = MIN(remaining, FLASH_BLOCK_SIZE - (addr % FLASH_BLOCK_SIZE));
    status = BSP_XSPI_NOR_Write(0, (uint8_t*)(buff + offset), addr, chunk);
    if (status != BSP_OK)
    {
      LERROR(TRACE_SYSTEM, "Write data (%d)", status);
      Error_Handler();
    }

    /* Update counters */
    addr      += chunk;
    offset    += chunk;
    remaining -= chunk;
  }
#else
  /* Erase blocks (as needed) */
  uint32_t start = addr & ~(FLASH_BLOCK_SIZE - 1U);
  uint32_t end   = ((addr + size) + FLASH_BLOCK_SIZE - 1U) & ~(FLASH_BLOCK_SIZE - 1U);
  while (start < end)
  {
    status = BSP_XSPI_NOR_Erase_Block(0, start, BSP_XSPI_NOR_ERASE_4K);
    if (status != BSP_OK)
    {
      LERROR(TRACE_SYSTEM, "Erase block 0x%08X (%d)", start, status);
      Error_Handler();
    }
    start += FLASH_BLOCK_SIZE;
  }

  /* Write data in bulk */
  status = BSP_XSPI_NOR_Write(0, (uint8_t*)buff, addr, size);
  if (status != BSP_OK)
  {
    LERROR(TRACE_SYSTEM, "Write data (%d)", status);
    Error_Handler();
  }
#endif

  /* Restore mapping mode */
  status = BSP_XSPI_NOR_EnableMemoryMappedMode(0);
  if (status != BSP_OK)
  {
    LERROR(TRACE_SYSTEM, "Enable memory mapped mode (%d)", status);
    Error_Handler();
  }

  #if FLASH_TX_CRITICAL == 1U
  /* TODO: Critical section - Exit */
  tx_thread_preemption_change(tx_thread_identify(), prio, (UINT*)&prio);
  tx_interrupt_control(mask);
  #endif /* FLASH_TX_CRITICAL */

  /* Release */
  _flash_read_protect(false, FLASH_RX_SLOTS);
  _flash_write_protect(false);
  return size;
}

/* Private function definitions ----------------------------------------------*/

/**
 * @brief Acquire or release the flash for reading
 * @param acquire true to acquire, false to release
 * @param slots   Number of read slots to acquire/release (max FLASH_RX_SLOTS)
 */
void _flash_read_protect(bool acquire, size_t slots)
{
  for (size_t slot = 0; slot < MIN(slots, FLASH_RX_SLOTS); slot++)
  {
    rtos_semaphore_acquire(&_flash.sem, acquire);
  }
}

/**
 * @brief Acquire or release the flash for writing
 * @param acquire true to acquire, false to release
 */
void _flash_write_protect(bool acquire)
{
  rtos_mutex_acquire(&_flash.mtx, acquire);
}
