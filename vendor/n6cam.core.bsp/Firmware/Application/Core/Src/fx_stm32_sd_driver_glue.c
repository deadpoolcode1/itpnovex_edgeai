/**
 ******************************************************************************
 * @file    fx_stm32_sd_driver_glue.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   FileX SDCard driver definition
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2024 SIANA Systems</center></h2>
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
 ******************************************************************************
 */
#include "fx_stm32_sd_driver.h"
#include "n6cam_sdio.h"

/*-->> Private Tunables <<----------------------------------------------------*/

/*-->> Private Definitions <<-------------------------------------------------*/

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/*-->> Private Data <<--------------------------------------------------------*/

TX_SEMAPHORE sd_rx_semaphore;
TX_SEMAPHORE sd_tx_semaphore;

extern SD_HandleTypeDef hsd2;

/*-->> Private Functions <<---------------------------------------------------*/

/*-->> Public API <<----------------------------------------------------------*/

/**
* @brief Initializes the SD IP instance
* @param UINT instance SD instance to initialize
* @retval 0 on success error value otherwise
*/
INT fx_stm32_sd_init(UINT instance)
{
  UNUSED(instance);

  INT ret = 0;

#if (FX_STM32_SD_INIT == 1)
  bsp_sdio_init();

  /* Slight delay to make sure SD-Card power is stable */
  tx_thread_sleep(3);
#endif

  return ret;
}

/**
* @brief Deinitializes the SD IP instance
* @param UINT instance SD instance to deinitialize
* @retval 0 on success error value otherwise
*/
INT fx_stm32_sd_deinit(UINT instance)
{
  UNUSED(instance);

  INT ret = 0;

#if (FX_STM32_SD_INIT == 1)
  if (bsp_sdio_deinit() != BSP_OK)
  {
    ret = 1;
  }
#endif

  return ret;
}

/**
* @brief Check the SD IP status.
* @param UINT instance SD instance to check
* @retval 0 when ready 1 when busy
*/
INT fx_stm32_sd_get_status(UINT instance)
{
  UNUSED(instance);

  INT ret = 0;

  if(HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER)
  {
    ret = 1;
  }

  return ret;
}

/**
* @brief Read Data from the SD device into a buffer.
* @param UINT instance SD IP instance to read from.
* @param UINT *buffer buffer into which the data is to be read.
* @param UINT start_block the first block to start reading from.
* @param UINT total_blocks total number of blocks to read.
* @retval 0 on success error code otherwise
*/
INT fx_stm32_sd_read_blocks(UINT instance, UINT *buffer, UINT start_block, UINT total_blocks)
{
  UNUSED(instance);

  INT ret = 0;

  if( HAL_SD_ReadBlocks_DMA(&hsd2, (uint8_t *)buffer, start_block, total_blocks) != HAL_OK)
  {
    ret = 1;
  }

  return ret;
}

/**
* @brief Write data buffer into the SD device.
* @param UINT instance SD IP instance to write into.
* @param UINT *buffer buffer to write into the SD device.
* @param UINT start_block the first block to start writing into.
* @param UINT total_blocks total number of blocks to write.
* @retval 0 on success error code otherwise
*/
INT fx_stm32_sd_write_blocks(UINT instance, UINT *buffer, UINT start_block, UINT total_blocks)
{
  UNUSED(instance);

  INT ret = 0;

  if (HAL_SD_WriteBlocks_DMA(&hsd2, (uint8_t *)buffer, start_block, total_blocks) != HAL_OK)
  {
    ret = 1;
  }

  return ret;
}

/**
* @brief SD DMA Tx Transfer completed callbacks
* @param Instance the sd instance
*/
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  tx_semaphore_put(&sd_tx_semaphore);
}

/**
* @brief SD DMA Rx Transfer completed callbacks
* @param Instance the sd instance
*/
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  tx_semaphore_put(&sd_rx_semaphore);
}

/*-->> Private Functions <<---------------------------------------------------*/
