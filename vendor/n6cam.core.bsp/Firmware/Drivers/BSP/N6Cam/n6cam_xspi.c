/**
  ******************************************************************************
  * @file    n6cam_xspi.c
  * @author  SIANA (based on MCD Application Team)
  * @brief   This file includes a standard driver for the MX66UW1G45G and the APS256XX
  *          XSPI memories mounted on the N6Cam board.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  @verbatim
  ==============================================================================
                     ##### How to use this driver #####
  ==============================================================================
  [..]
   (#) This driver is used to drive the MX66UW1G45G Octal NOR and the APS256XX Octal PSRAM
       external memories mounted on STM32N6570-DK board.

   (#) This driver need specific component driver (MX66UW1G45G and APS256XX) to be included with.

   (#) MX66UW1G45G Initialization steps:
       (++) Initialize the XSPI external memory using the BSP_XSPI_NOR_Init() function. This
            function includes the MSP layer hardware resources initialization and the
            XSPI interface with the external memory.

   (#) MX66UW1G45G Octal NOR memory operations
       (++) XSPI memory can be accessed with read/write operations once it is
            initialized.
            Read/write operation can be performed with AHB access using the functions
            BSP_XSPI_NOR_Read()/BSP_XSPI_NOR_Write().
       (++) The function BSP_XSPI_NOR_GetInfo() returns the configuration of the XSPI memory.
            (see the XSPI memory data sheet)
       (++) Perform erase block operation using the function BSP_XSPI_NOR_Erase_Block() and by
            specifying the block address. You can perform an erase operation of the whole
            chip by calling the function BSP_XSPI_NOR_Erase_Chip().
       (++) The function BSP_XSPI_NOR_GetStatus() returns the current status of the XSPI memory.
            (see the XSPI memory data sheet)
       (++) The memory access can be configured in memory-mapped mode with the call of
            function BSP_XSPI_NOR_EnableMemoryMapped(). To go back in indirect mode, the
            function BSP_XSPI_NOR_DisableMemoryMapped() should be used.
       (++) The erase operation can be suspend and resume with using functions
            BSP_XSPI_NOR_SuspendErase() and BSP_XSPI_NOR_ResumeErase()
       (++) It is possible to put the memory in deep power-down mode to reduce its consumption.
            For this, the function BSP_XSPI_NOR_EnterDeepPowerDown() should be called. To leave
            the deep power-down mode, the function BSP_XSPI_NOR_LeaveDeepPowerDown() should be called.
       (++) The function BSP_XSPI_NOR_ReadID() returns the identifier of the memory
            (see the XSPI memory data sheet)
       (++) The configuration of the interface between peripheral and memory is done by
            the function BSP_XSPI_NOR_ConfigFlash(), three modes are possible :
            - SPI : instruction, address and data on one line
            - STR OPI : instruction, address and data on eight lines with sampling on one edge of clock
            - DTR OPI : instruction, address and data on eight lines with sampling on both edgaes of clock

   (#) APS256XX Octal PSRAM memory Initialization steps:
       (++) Initialize the Octal PSRAM external memory using the BSP_XSPI_RAM_Init() function. This
            function includes the MSP layer hardware resources initialization and the
            XSPI interface with the external memory.

   (#) APS256XXL Octal PSRAM memory operations
       (++) Octal PSRAM memory can be accessed with read/write operations once it is
            initialized.
            Read/write operation can be performed with AHB access using the functions
            BSP_XSPI_RAM_Read()/BSP_XSPI_RAM_Write().
       (++) The memory access can be configured in memory-mapped mode with the call of
            function BSP_XSPI_RAM_EnableMemoryMapped(). To go back in indirect mode, the
            function BSP_XSPI_RAM_DisableMemoryMapped() should be used.
       (++) The function BSP_XSPI_RAM_ReadID() returns the identifier of the memory
            (see the XSPI memory data sheet)

  @endverbatim
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/

#include "n6cam_io.h"
#include "n6cam_xspi.h"

/** @addtogroup BSP
  * @{
  */

/** @addtogroup STM32N6570_DK
  * @{
  */

/** @defgroup STM32N6570_DK_XSPI XSPI
  * @{
  */

/* Exported variables --------------------------------------------------------*/
#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @addtogroup STM32N6570_DK_XSPI_NOR_Exported_Variables
  * @{
  */
XSPI_HandleTypeDef hxspi_nor[XSPI_NOR_INSTANCES_NUMBER] = {0};
XSPI_NOR_Ctx_t XSPI_Nor_Ctx[XSPI_NOR_INSTANCES_NUMBER]  = {{
    XSPI_ACCESS_NONE,
    MX66UW1G45G_SPI_MODE,
    MX66UW1G45G_STR_TRANSFER
  }
};
/**
  * @}
  */
#endif

/* Exported variables --------------------------------------------------------*/
#if (USE_RAM_MEMORY_APS256XX == 1)
/** @addtogroup STM32N6570_DK_XSPI_RAM_Exported_Variables
  * @{
  */
XSPI_HandleTypeDef hxspi_ram[XSPI_RAM_INSTANCES_NUMBER] = {0};
XSPI_RAM_Ctx_t XSPI_Ram_Ctx[XSPI_RAM_INSTANCES_NUMBER] = {{
    XSPI_ACCESS_NONE,
    BSP_XSPI_RAM_VARIABLE_LATENCY,
    BSP_XSPI_RAM_HYBRID_BURST,
    APS256XX_BURST_16_BYTES
  }
};

/**
  * @}
  */
#endif

/* Private constants --------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/** @defgroup STM32N6570_DK_XSPI_NOR_Private_Variables XSPI_NOR Private Variables
  * @{
  */
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 1)
static uint32_t XSPINor_IsMspCbValid[XSPI_NOR_INSTANCES_NUMBER] = {0};
#endif /* USE_HAL_XSPI_REGISTER_CALLBACKS */
/**
  * @}
  */

/** @defgroup STM32N6570_DK_XSPI_RAM_Private_Variables XSPI_RAM Private Variables
  * @{
  */
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 1)
static uint32_t XSPIRam_IsMspCbValid[XSPI_RAM_INSTANCES_NUMBER] = {0};
#endif /* USE_HAL_XSPI_REGISTER_CALLBACKS */
/**
  * @}
  */
/* Private functions ---------------------------------------------------------*/

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @defgroup STM32N6570_DK_XSPI_NOR_Private_Functions XSPI_NOR Private Functions
  * @{
  */
static void    XSPI_NOR_MspInit(XSPI_HandleTypeDef *hxspi);
static void    XSPI_NOR_MspDeInit(XSPI_HandleTypeDef *hxspi);
static int32_t XSPI_NOR_ResetMemory(uint32_t Instance);
static int32_t XSPI_NOR_EnterDOPIMode(uint32_t Instance);
static int32_t XSPI_NOR_EnterSOPIMode(uint32_t Instance);
static int32_t XSPI_NOR_ExitOPIMode(uint32_t Instance);
/**
  * @}
  */
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
/** @defgroup STM32N6570_DK_XSPI_RAM_Private_Functions XSPI_RAM Private Functions
  * @{
  */
static void XSPI_RAM_MspInit(XSPI_HandleTypeDef *hxspi);
static void XSPI_RAM_MspDeInit(XSPI_HandleTypeDef *hxspi);
/**
  * @}
  */
#endif

/* Exported functions ---------------------------------------------------------*/

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @addtogroup STM32N6570_DK_XSPI_NOR_Exported_Functions
  * @{
  */

/**
  * @brief  Initializes the XSPI interface.
  * @param  Instance   XSPI Instance
  * @param  Init       XSPI Init structure
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_Init(uint32_t Instance, BSP_XSPI_NOR_Init_t *Init)
{
  int32_t ret;
  BSP_XSPI_NOR_Info_t pInfo;
  MX_XSPI_InitTypeDef xspi_init;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Check if the instance is already initialized */
    if (XSPI_Nor_Ctx[Instance].IsInitialized == XSPI_ACCESS_NONE)
    {
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 0)
      /* Msp XSPI initialization */
      XSPI_NOR_MspInit(&hxspi_nor[Instance]);
#else
      /* Register the XSPI MSP Callbacks */
      if (XSPINor_IsMspCbValid[Instance] == 0UL)
      {
        if (BSP_XSPI_NOR_RegisterDefaultMspCallbacks(Instance) != BSP_OK)
        {
          return BSP_ERROR_PERIPHERAL;
        }
      }
#endif /* USE_HAL_XSPI_REGISTER_CALLBACKS */

      /* Get Flash information of one memory */
      (void)MX66UW1G45G_GetFlashInfo(&pInfo);

      /* Fill config structure */
      xspi_init.ClockPrescaler = 0x03; /* XSPI clock = 200MHz / ClockPrescaler = 50MHz, then switch to 200MHz*/
      xspi_init.MemorySize     = (uint32_t)POSITION_VAL((uint32_t)pInfo.FlashSize);
      xspi_init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
      xspi_init.TransferRate   = (uint32_t)Init->TransferRate;

      /* STM32 XSPI interface initialization */
      if (MX_XSPI_NOR_Init(&hxspi_nor[Instance], &xspi_init) != HAL_OK)
      {
        ret = BSP_ERROR_PERIPHERAL;
      }
      /* XSPI memory reset */
      else if (XSPI_NOR_ResetMemory(Instance) != BSP_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      /* Check if memory is ready */
      else if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                                XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      /* Configure the memory */
      else if (BSP_XSPI_NOR_ConfigFlash(Instance, Init->InterfaceMode, Init->TransferRate) != BSP_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      else
      {
        ret = BSP_OK;
      }
    }
    else
    {
      ret = BSP_OK;
    }
  }

  HAL_XSPI_SetClockPrescaler(&hxspi_nor[Instance], 0);

  /* Return BSP status */
  return ret;
}

/**
  * @brief  De-Initializes the XSPI interface.
  * @param  Instance   XSPI Instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_DeInit(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Check if the instance is already initialized */
    if (XSPI_Nor_Ctx[Instance].IsInitialized != XSPI_ACCESS_NONE)
    {
      /* Disable Memory mapped mode */
      if (XSPI_Nor_Ctx[Instance].IsInitialized == XSPI_ACCESS_MMP)
      {
        if (BSP_XSPI_NOR_DisableMemoryMappedMode(Instance) != BSP_OK)
        {
          return BSP_ERROR_COMPONENT;
        }
      }

      /* Set default XSPI_Nor_Ctx values */
      XSPI_Nor_Ctx[Instance].IsInitialized = XSPI_ACCESS_NONE;
      XSPI_Nor_Ctx[Instance].InterfaceMode = BSP_XSPI_NOR_SPI_MODE;
      XSPI_Nor_Ctx[Instance].TransferRate  = BSP_XSPI_NOR_STR_TRANSFER;

#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 0)
      XSPI_NOR_MspDeInit(&hxspi_nor[Instance]);
#endif /* (USE_HAL_XSPI_REGISTER_CALLBACKS == 0) */

      /* Call the DeInit function to reset the driver */
      if (HAL_XSPI_DeInit(&hxspi_nor[Instance]) != HAL_OK)
      {
        ret = BSP_ERROR_PERIPHERAL;
      }
    }
  }

  /* Return BSP status */
  return ret;
}
/**
  * @}
  */

/** @addtogroup STM32N6570_DK_XSPI_Exported_Init_Functions
  * @{
  */
/**
  * @brief  Initializes the XSPI interface.
  * @param  hxspi          XSPI handle
  * @param  Init           XSPI config structure
  * @retval BSP status
  */
__weak HAL_StatusTypeDef MX_XSPI_NOR_Init(XSPI_HandleTypeDef *hxspi, MX_XSPI_InitTypeDef *Init)
{
  /* XSPI initialization */
  hxspi->Instance = XSPI2;

  hxspi->Init.FifoThresholdByte       = 1;
  hxspi->Init.MemorySize              = Init->MemorySize; /* 1 GBits */
  hxspi->Init.ChipSelectHighTimeCycle = 2;
  hxspi->Init.FreeRunningClock        = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi->Init.ClockMode               = HAL_XSPI_CLOCK_MODE_0;
  hxspi->Init.DelayHoldQuarterCycle   = HAL_XSPI_DHQC_DISABLE;
  hxspi->Init.ClockPrescaler          = Init->ClockPrescaler;
  hxspi->Init.SampleShifting          = Init->SampleShifting;
  hxspi->Init.ChipSelectBoundary      = HAL_XSPI_BONDARYOF_NONE;
  hxspi->Init.MemoryMode              = HAL_XSPI_SINGLE_MEM;
  hxspi->Init.WrapSize                = HAL_XSPI_WRAP_NOT_SUPPORTED;

  if (Init->TransferRate == (uint32_t) BSP_XSPI_NOR_DTR_TRANSFER)
  {
    hxspi->Init.MemoryType            = HAL_XSPI_MEMTYPE_MACRONIX;
    hxspi->Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_ENABLE;
  }
  else
  {
    hxspi->Init.MemoryType            = HAL_XSPI_MEMTYPE_MACRONIX;
    hxspi->Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_DISABLE;
  }
  return HAL_XSPI_Init(hxspi);
}
/**
  * @}
  */

/** @addtogroup STM32N6570_DK_XSPI_NOR_Exported_Functions
  * @{
  */
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 1)
/**
  * @brief Default BSP XSPI Msp Callbacks
  * @param Instance      XSPI Instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_RegisterDefaultMspCallbacks(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Register MspInit/MspDeInit Callbacks */
    if (HAL_XSPI_RegisterCallback(&hxspi_nor[Instance], HAL_XSPI_MSP_INIT_CB_ID, XSPI_NOR_MspInit) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else if (HAL_XSPI_RegisterCallback(&hxspi_nor[Instance], HAL_XSPI_MSP_DEINIT_CB_ID, XSPI_NOR_MspDeInit) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else
    {
      XSPINor_IsMspCbValid[Instance] = 1U;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief BSP XSPI Msp Callback registering
  * @param Instance     XSPI Instance
  * @param CallBacks    pointer to MspInit/MspDeInit callbacks functions
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_RegisterMspCallbacks(uint32_t Instance, BSP_XSPI_Cb_t *CallBacks)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Register MspInit/MspDeInit Callbacks */
    if (HAL_XSPI_RegisterCallback(&hxspi_nor[Instance], HAL_XSPI_MSP_INIT_CB_ID, CallBacks->pMspInitCb) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else if (HAL_XSPI_RegisterCallback(&hxspi_nor[Instance],
                                       HAL_XSPI_MSP_DEINIT_CB_ID, CallBacks->pMspDeInitCb) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else
    {
      XSPINor_IsMspCbValid[Instance] = 1U;
    }
  }

  /* Return BSP status */
  return ret;
}
#endif /* (USE_HAL_XSPI_REGISTER_CALLBACKS == 1) */

/**
  * @brief  Reads an amount of data from the XSPI memory.
  * @param  Instance  XSPI instance
  * @param  pData     Pointer to data to be read
  * @param  ReadAddr  Read start address
  * @param  Size      Size of data to read
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_Read(uint32_t Instance, uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    if (XSPI_Nor_Ctx[Instance].TransferRate == BSP_XSPI_NOR_STR_TRANSFER)
    {
      if (MX66UW1G45G_ReadSTR(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                               MX66UW1G45G_4BYTES_SIZE, pData, ReadAddr, Size) != MX66UW1G45G_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      else
      {
        ret = BSP_OK;
      }
    }
    else
    {

      /* Bypass the Pre-scaler */
      HAL_XSPI_SetClockPrescaler(&hxspi_nor[Instance], 0);

      if (MX66UW1G45G_ReadDTR(&hxspi_nor[Instance], pData, ReadAddr, Size) != MX66UW1G45G_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      else
      {
        ret = BSP_OK;
      }
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Writes an amount of data to the XSPI memory.
  * @param  Instance  XSPI instance
  * @param  pData     Pointer to data to be written
  * @param  WriteAddr Write start address
  * @param  Size      Size of data to write
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_Write(uint32_t Instance, uint8_t *pData, uint32_t WriteAddr, uint32_t Size)
{
  int32_t ret = BSP_OK;
  uint32_t end_addr;
  uint32_t current_size;
  uint32_t current_addr;
  uint32_t data_addr;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Calculation of the size between the write address and the end of the page */
    current_size = MX66UW1G45G_PAGE_SIZE - (WriteAddr % MX66UW1G45G_PAGE_SIZE);

    /* Check if the size of the data is less than the remaining place in the page */
    if (current_size > Size)
    {
      current_size = Size;
    }

    /* Initialize the address variables */
    current_addr = WriteAddr;
    end_addr = WriteAddr + Size;
    data_addr = (uint32_t)pData;

    /* Perform the write page by page */
    do
    {

      /* Check if Flash busy ? */
      if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                           XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }/* Enable write operations */
      else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                        XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      else
      {
        if (XSPI_Nor_Ctx[Instance].TransferRate == BSP_XSPI_NOR_STR_TRANSFER)
        {
          /* Issue page program command */
          if (MX66UW1G45G_PageProgram(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                       MX66UW1G45G_4BYTES_SIZE, (uint8_t *)data_addr, current_addr,
                                       current_size) != MX66UW1G45G_OK)
          {
            ret = BSP_ERROR_COMPONENT;
          }
        }
        else
        {
          /* Issue page program command */
          if (MX66UW1G45G_PageProgramDTR(&hxspi_nor[Instance], (uint8_t *)data_addr, current_addr,
                                          current_size) != MX66UW1G45G_OK)
          {
            ret = BSP_ERROR_COMPONENT;
          }
        }

        if (ret == BSP_OK)
        {
          /* Configure automatic polling mode to wait for end of program */
          if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                               XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
          {
            ret = BSP_ERROR_COMPONENT;
          }
          else
          {
            /* Update the address and size variables for next page programming */
            current_addr += current_size;
            data_addr += current_size;
            current_size = ((current_addr + MX66UW1G45G_PAGE_SIZE) > end_addr)
                           ? (end_addr - current_addr)
                           : MX66UW1G45G_PAGE_SIZE;
          }
        }
      }
    } while ((current_addr < end_addr) && (ret == BSP_OK));
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Erases the specified block of the XSPI memory.
  * @param  Instance     XSPI instance
  * @param  BlockAddress Block address to erase
  * @param  BlockSize    Erase Block size
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_Erase_Block(uint32_t Instance, uint32_t BlockAddress, BSP_XSPI_NOR_Erase_t BlockSize)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Check Flash busy ? */
    if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                         XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }/* Enable write operations */
    else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                      XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }/* Issue Block Erase command */
    else if (MX66UW1G45G_BlockErase(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                     XSPI_Nor_Ctx[Instance].TransferRate, MX66UW1G45G_4BYTES_SIZE,
                                     BlockAddress, BlockSize) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else
    {
      ret = BSP_OK;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Erases the entire XSPI memory.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_Erase_Chip(uint32_t Instance)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Check Flash busy ? */
    if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                         XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }/* Enable write operations */
    else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                      XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }/* Issue Chip erase command */
    else if (MX66UW1G45G_ChipErase(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                     XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else
    {
      ret = BSP_OK;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Reads current status of the XSPI memory.
  * @param  Instance  XSPI instance
  * @retval XSPI memory status: whether busy or not
  */
int32_t BSP_XSPI_NOR_GetStatus(uint32_t Instance)
{
  static uint8_t reg[2];
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    if (MX66UW1G45G_ReadSecurityRegister(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                          XSPI_Nor_Ctx[Instance].TransferRate, reg) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }/* Check the value of the register */
    else if ((reg[0] & (MX66UW1G45G_SECR_P_FAIL | MX66UW1G45G_SECR_E_FAIL)) != 0U)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else if ((reg[0] & (MX66UW1G45G_SECR_PSB | MX66UW1G45G_SECR_ESB)) != 0U)
    {
      ret = BSP_ERROR_XSPI_SUSPENDED;
    }
    else if (MX66UW1G45G_ReadStatusRegister(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                             XSPI_Nor_Ctx[Instance].TransferRate, reg) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }/* Check the value of the register */
    else if ((reg[0] & MX66UW1G45G_SR_WIP) != 0U)
    {
      ret = BSP_ERROR_BUSY;
    }
    else
    {
      ret = BSP_OK;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Return the configuration of the XSPI memory.
  * @param  Instance  XSPI instance
  * @param  pInfo     pointer on the configuration structure
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_GetInfo(uint32_t Instance, BSP_XSPI_NOR_Info_t *pInfo)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    (void)MX66UW1G45G_GetFlashInfo(pInfo);
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Configure the XSPI in memory-mapped mode
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_EnableMemoryMappedMode(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {

    /* Bypass the Pre-scaler */
    HAL_XSPI_SetClockPrescaler(&hxspi_nor[Instance], 0);

    if (XSPI_Nor_Ctx[Instance].TransferRate == BSP_XSPI_NOR_STR_TRANSFER)
    {
      if (MX66UW1G45G_EnableMemoryMappedModeSTR(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                                 MX66UW1G45G_4BYTES_SIZE) != MX66UW1G45G_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      else /* Update XSPI context if all operations are well done */
      {
        XSPI_Nor_Ctx[Instance].IsInitialized = XSPI_ACCESS_MMP;
      }
    }
    else
    {
      if (MX66UW1G45G_EnableMemoryMappedModeDTR(&hxspi_nor[Instance],
                                                 XSPI_Nor_Ctx[Instance].InterfaceMode) != MX66UW1G45G_OK)
      {
        ret = BSP_ERROR_COMPONENT;
      }
      else /* Update XSPI context if all operations are well done */
      {

       XSPI_Nor_Ctx[Instance].IsInitialized = XSPI_ACCESS_MMP;
      }
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Exit form memory-mapped mode
  *         Only 1 Instance can running MMP mode. And it will lock system at this mode.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_DisableMemoryMappedMode(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    if (XSPI_Nor_Ctx[Instance].IsInitialized != XSPI_ACCESS_MMP)
    {
      ret = BSP_ERROR_XSPI_MMP_UNLOCK_FAILURE;
    }/* Abort MMP back to indirect mode */
    else if (HAL_XSPI_Abort(&hxspi_nor[Instance]) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else /* Update XSPI NOR context if all operations are well done */
    {
      XSPI_Nor_Ctx[Instance].IsInitialized = XSPI_ACCESS_INDIRECT;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Get flash ID 3 Bytes:
  *         Manufacturer ID, Memory type, Memory density
  * @param  Instance  XSPI instance
  * @param  Id Pointer to flash ID bytes
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_ReadID(uint32_t Instance, uint8_t *Id)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else if (MX66UW1G45G_ReadID(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                               XSPI_Nor_Ctx[Instance].TransferRate, Id) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    ret = BSP_OK;
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Set Flash to desired Interface mode. And this instance becomes current instance.
  *         If current instance running at MMP mode then this function doesn't work.
  *         Indirect -> Indirect
  * @param  Instance  XSPI instance
  * @param  Mode      XSPI mode
  * @param  Rate      XSPI transfer rate
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_ConfigFlash(uint32_t Instance, BSP_XSPI_NOR_Interface_t Mode, BSP_XSPI_NOR_Transfer_t Rate)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Check if MMP mode locked ************************************************/
    if (XSPI_Nor_Ctx[Instance].IsInitialized == XSPI_ACCESS_MMP)
    {
      ret = BSP_ERROR_XSPI_MMP_LOCK_FAILURE;
    }
    else
    {
      /* Setup Flash interface ***************************************************/
      switch (XSPI_Nor_Ctx[Instance].InterfaceMode)
      {
        case BSP_XSPI_NOR_OPI_MODE :  /* 8-8-8 commands */
          if ((Mode != BSP_XSPI_NOR_OPI_MODE) || (Rate != XSPI_Nor_Ctx[Instance].TransferRate))
          {
            /* Exit OPI mode */
            ret = XSPI_NOR_ExitOPIMode(Instance);

            if ((ret == BSP_OK) && (Mode == BSP_XSPI_NOR_OPI_MODE))
            {

              if (XSPI_Nor_Ctx[Instance].TransferRate == BSP_XSPI_NOR_STR_TRANSFER)
              {
                /* Enter DTR OPI mode */
                ret = XSPI_NOR_EnterDOPIMode(Instance);
              }
              else
              {
                /* Enter STR OPI mode */
                ret = XSPI_NOR_EnterSOPIMode(Instance);
              }
            }
          }
          break;

        case BSP_XSPI_NOR_SPI_MODE :  /* 1-1-1 commands, Power on H/W default setting */
        default :
          if (Mode == BSP_XSPI_NOR_OPI_MODE)
          {
            if (Rate == BSP_XSPI_NOR_STR_TRANSFER)
            {
              /* Enter STR OPI mode */
              ret = XSPI_NOR_EnterSOPIMode(Instance);
            }
            else
            {
              /* Enter DTR OPI mode */
              ret = XSPI_NOR_EnterDOPIMode(Instance);
            }
          }
          break;
      }

      /* Update XSPI context if all operations are well done */
      if (ret == BSP_OK)
      {
        /* Update current status parameter *****************************************/
        XSPI_Nor_Ctx[Instance].IsInitialized = XSPI_ACCESS_INDIRECT;
        XSPI_Nor_Ctx[Instance].InterfaceMode = Mode;
        XSPI_Nor_Ctx[Instance].TransferRate  = Rate;
      }
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  This function suspends an ongoing erase command.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_SuspendErase(uint32_t Instance)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  /* Check whether the device is busy (erase operation is in progress). */
  else if (BSP_XSPI_NOR_GetStatus(Instance) != BSP_ERROR_BUSY)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (MX66UW1G45G_Suspend(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (BSP_XSPI_NOR_GetStatus(Instance) != BSP_ERROR_XSPI_SUSPENDED)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    ret = BSP_OK;
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  This function resumes a paused erase command.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_ResumeErase(uint32_t Instance)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  /* Check whether the device is busy (erase operation is in progress). */
  else if (BSP_XSPI_NOR_GetStatus(Instance) != BSP_ERROR_XSPI_SUSPENDED)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (MX66UW1G45G_Resume(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                               XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  /*
  When this command is executed, the status register write in progress bit is set to 1, and
  the flag status register program erase controller bit is set to 0. This command is ignored
  if the device is not in a suspended state.
  */
  else if (BSP_XSPI_NOR_GetStatus(Instance) != BSP_ERROR_BUSY)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    ret = BSP_OK;
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  This function enter the XSPI memory in deep power down mode.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_EnterDeepPowerDown(uint32_t Instance)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else if (MX66UW1G45G_EnterPowerDown(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                       XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    ret = BSP_OK;
  }

  /* ---          Memory takes 10us max to enter deep power down          --- */

  /* Return BSP status */
  return ret;
}

/**
  * @brief  This function leave the XSPI memory from deep power down mode.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_NOR_LeaveDeepPowerDown(uint32_t Instance)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else if (MX66UW1G45G_NoOperation(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                    XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    ret = BSP_OK;
  }

  /* --- A NOP command is sent to the memory, as the nCS should be low for at least 20 ns --- */
  /* ---                  Memory takes 30us min to leave deep power down                  --- */

  /* Return BSP status */
  return ret;
}
/**
  * @}
  */
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
/** @addtogroup STM32N6570_DK_XSPI_RAM_Exported_Functions
  * @{
  */

/**
  * @brief  Initializes the XSPI interface.
  * @param  Instance   XSPI Instance
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_Init(uint32_t Instance)
{
  MX_XSPI_InitTypeDef xspi_init;
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Check if the instance is already initialized */
    if (XSPI_Ram_Ctx[Instance].IsInitialized == XSPI_ACCESS_NONE)
    {
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 0)
      /* Msp XSPI initialization */
      XSPI_RAM_MspInit(&hxspi_ram[Instance]);
#else
      /* Register the XSPI MSP Callbacks */
      if (XSPIRam_IsMspCbValid[Instance] == 0UL)
      {
        if (BSP_XSPI_RAM_RegisterDefaultMspCallbacks(Instance) != BSP_OK)
        {
          return BSP_ERROR_PERIPHERAL;
        }
      }
#endif /* USE_HAL_XSPI_REGISTER_CALLBACKS */

      /* Fill config structure */
      xspi_init.ClockPrescaler = 3;
      xspi_init.MemorySize     = HAL_XSPI_SIZE_256MB;
      xspi_init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;

      /* STM32 XSPI interface initialization */
      if (MX_XSPI_RAM_Init(&hxspi_ram[Instance], &xspi_init) != HAL_OK)
      {
        ret = BSP_ERROR_PERIPHERAL;
      }
      /* Update current status parameter */
      XSPI_Ram_Ctx[Instance].IsInitialized = XSPI_ACCESS_INDIRECT;
      XSPI_Ram_Ctx[Instance].LatencyType   = BSP_XSPI_RAM_FIXED_LATENCY;
      XSPI_Ram_Ctx[Instance].BurstType     = BSP_XSPI_RAM_LINEAR_BURST;
    }

    /* Read Latency=7 up to 200MHz */
    APS256XX_WriteReg(&hxspi_ram[Instance], 0, 0x30);

    /* Write Latency=7 up to 200MHz */
    APS256XX_WriteReg(&hxspi_ram[Instance], 4, 0x20);

    /* Switch to x16 mode */
    APS256XX_WriteReg(&hxspi_ram[Instance], 8, 0x40);

    /* Bypass the Pre-scaler */
    HAL_XSPI_SetClockPrescaler(&hxspi_ram[Instance], 0);

  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  De-Initializes the XSPI interface.
  * @param  Instance   XSPI Instance
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_DeInit(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Check if the instance is already initialized */
    if (XSPI_Ram_Ctx[Instance].IsInitialized != XSPI_ACCESS_NONE)
    {
      /* Disable Memory mapped mode */
      if (XSPI_Ram_Ctx[Instance].IsInitialized == XSPI_ACCESS_MMP)
      {
        if (BSP_XSPI_RAM_DisableMemoryMappedMode(Instance) != BSP_OK)
        {
          return BSP_ERROR_COMPONENT;
        }
      }
      /* Set default XSPI_Ram_Ctx values */
      XSPI_Ram_Ctx[Instance].IsInitialized = XSPI_ACCESS_NONE;
      XSPI_Ram_Ctx[Instance].LatencyType   = BSP_XSPI_RAM_FIXED_LATENCY;
      XSPI_Ram_Ctx[Instance].BurstType     = BSP_XSPI_RAM_LINEAR_BURST;

#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 0)
      XSPI_RAM_MspDeInit(&hxspi_ram[Instance]);
#endif /* (USE_HAL_XSPI_REGISTER_CALLBACKS == 0) */

      /* Call the DeInit function to reset the driver */
      if (HAL_XSPI_DeInit(&hxspi_ram[Instance]) != HAL_OK)
      {
        ret = BSP_ERROR_PERIPHERAL;
      }
    }
  }

  /* Return BSP status */
  return ret;
}
/**
  * @}
  */

/** @addtogroup STM32N6570_DK_XSPI_Exported_Init_Functions
  * @{
  */

/**
  * @brief  Initializes the XSPI interface.
  * @param  hxspi          XSPI handle
  * @param  Init           XSPI config structure
  * @retval BSP status
  */
__weak HAL_StatusTypeDef MX_XSPI_RAM_Init(XSPI_HandleTypeDef *hxspi, MX_XSPI_InitTypeDef *Init)
{
  uint32_t hspi_clk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_XSPI1);

  /* XSPI initialization */
  hxspi->Instance = XSPI1;

  hxspi->Init.FifoThresholdByte          = 8;
  hxspi->Init.MemoryType                 = HAL_XSPI_MEMTYPE_APMEM_16BITS;
  hxspi->Init.MemoryMode                 = HAL_XSPI_SINGLE_MEM;
  hxspi->Init.MemorySize                 = Init->MemorySize;
  hxspi->Init.MemorySelect               = HAL_XSPI_CSSEL_NCS1;
  hxspi->Init.ChipSelectHighTimeCycle    = 1;
  hxspi->Init.ClockMode                  = HAL_XSPI_CLOCK_MODE_0;
  hxspi->Init.ClockPrescaler             = Init->ClockPrescaler;
  hxspi->Init.SampleShifting             = Init->SampleShifting;
  hxspi->Init.DelayHoldQuarterCycle      = HAL_XSPI_DHQC_ENABLE;
  hxspi->Init.ChipSelectBoundary         = HAL_XSPI_BONDARYOF_16KB;
  hxspi->Init.FreeRunningClock           = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi->Init.Refresh                    = ((2U * (hspi_clk / hxspi->Init.ClockPrescaler)) / 1000000U) - 4U;
#if defined (OCTOSPI_DCR1_DLYBYP)
  hxspi->Init.DelayBlockBypass           = HAL_XSPI_DELAY_BLOCK_BYPASS;
#endif
  hxspi->Init.WrapSize                   = HAL_XSPI_WRAP_NOT_SUPPORTED;

  return HAL_XSPI_Init(hxspi);
}
/**
  * @}
  */

/** @addtogroup STM32N6570_DK_XSPI_RAM_Exported_Functions
  * @{
  */
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 1)
/**
  * @brief Default BSP XSPI Msp Callbacks
  * @param Instance      XSPI Instance
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_RegisterDefaultMspCallbacks(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Register MspInit/MspDeInit Callbacks */
    if (HAL_XSPI_RegisterCallback(&hxspi_ram[Instance], HAL_XSPI_MSP_INIT_CB_ID, XSPI_RAM_MspInit) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else if (HAL_XSPI_RegisterCallback(&hxspi_ram[Instance],
                                       HAL_XSPI_MSP_DEINIT_CB_ID, XSPI_RAM_MspDeInit) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else
    {
      XSPIRam_IsMspCbValid[Instance] = 1U;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief BSP XSPI Msp Callback registering
  * @param Instance     XSPI Instance
  * @param CallBacks    pointer to MspInit/MspDeInit callbacks functions
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_RegisterMspCallbacks(uint32_t Instance, BSP_XSPI_Cb_t *CallBacks)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    /* Register MspInit/MspDeInit Callbacks */
    if (HAL_XSPI_RegisterCallback(&hxspi_ram[Instance], HAL_XSPI_MSP_INIT_CB_ID, CallBacks->pMspInitCb) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else if (HAL_XSPI_RegisterCallback(&hxspi_ram[Instance], HAL_XSPI_MSP_DEINIT_CB_ID,
                                       CallBacks->pMspDeInitCb) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    else
    {
      XSPIRam_IsMspCbValid[Instance] = 1U;
    }
  }

  /* Return BSP status */
  return ret;
}
#endif /* (USE_HAL_XSPI_REGISTER_CALLBACKS == 1) */

/**
  * @brief  Reads an amount of data from the XSPI memory.
  * @param  Instance  XSPI instance
  * @param  pData     Pointer to data to be read
  * @param  ReadAddr  Read start address
  * @param  Size      Size of data to read
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_Read(uint32_t Instance, uint8_t *pData, uint32_t ReadAddr, uint32_t Size)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    if (APS256XX_Read(&hxspi_ram[Instance], pData, ReadAddr, Size, 7, 1, 0) != APS256XX_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Writes an amount of data to the XSPI memory.
  * @param  Instance  XSPI instance
  * @param  pData     Pointer to data to be written
  * @param  WriteAddr Write start address
  * @param  Size      Size of data to write
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_Write(uint32_t Instance, uint8_t *pData, uint32_t WriteAddr, uint32_t Size)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    if (APS256XX_Write(&hxspi_ram[Instance], pData, WriteAddr, Size, 7, 1, 0) != APS256XX_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Configure the XSPI in memory-mapped mode
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_EnableMemoryMappedMode(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    if (APS256XX_EnableMemoryMappedMode(&hxspi_ram[Instance], 7, 7, 1, 0) != APS256XX_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
  }


  /* Return BSP status */
  return ret;
}

/**
  * @brief  Exit the memory-mapped mode
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_DisableMemoryMappedMode(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else
  {
    if (XSPI_Ram_Ctx[Instance].IsInitialized != XSPI_ACCESS_MMP)
    {
      ret = BSP_ERROR_XSPI_MMP_UNLOCK_FAILURE;
    }
    /* Abort MMP back to indirect mode */
    else if (HAL_XSPI_Abort(&hxspi_ram[Instance]) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    /* Update XSPI HyperRAM context if all operations are well done */
    else
    {
      XSPI_Ram_Ctx[Instance].IsInitialized = XSPI_ACCESS_INDIRECT;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  Get RAM ID 3 Bytes:
  *         Vendor ID, Device ID, Device Density
  * @param  Instance  XSPI instance
  * @param  Id Pointer to RAM ID bytes
  * @retval BSP status
  */
int32_t BSP_XSPI_RAM_ReadID(uint32_t Instance, uint8_t *Id)
{
  int32_t ret;

  /* Check if the instance is supported */
  if (Instance >= XSPI_RAM_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else if (APS256XX_ReadID(&hxspi_ram[0], Id, 6U) != APS256XX_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    ret = BSP_OK;
  }

  /* Return BSP status */
  return ret;
}

/**
  * @}
  */
#endif

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @addtogroup STM32N6570_DK_XSPI_NOR_Private_Functions
  * @{
  */

/**
  * @brief  Initializes the XSPI MSP.
  * @param  hxspi XSPI handle
  */
static void XSPI_NOR_MspInit(XSPI_HandleTypeDef *hxspi)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  /* hxspi unused argument(s) compilation warning */
  UNUSED(hxspi);

  /* XSPI power enable */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_EnableVddIO3();
  HAL_PWREx_ConfigVddIORange(PWR_VDDIO3, PWR_VDDIO_RANGE_1V8);

  /* Enable the XSPI memory interface clock */
  __HAL_RCC_XSPI2_CLK_ENABLE();
  XSPI2_CLK_CLK_ENABLE();
  XSPI2_DQS_CLK_ENABLE();
  XSPI2_D0_CLK_ENABLE();
  XSPI2_D1_CLK_ENABLE();
  XSPI2_D2_CLK_ENABLE();
  XSPI2_D3_CLK_ENABLE();
  XSPI2_D4_CLK_ENABLE();
  XSPI2_D5_CLK_ENABLE();
  XSPI2_D6_CLK_ENABLE();
  XSPI2_D7_CLK_ENABLE();
  XSPI2_CS_CLK_ENABLE();

  /* Reset the XSPI memory interface */
  __HAL_RCC_XSPI2_FORCE_RESET();
  __HAL_RCC_XSPI2_RELEASE_RESET();

  /* Enable and reset XSPI I/O Manager */
  __HAL_RCC_XSPIM_CLK_ENABLE();
  __HAL_RCC_XSPIM_FORCE_RESET();
  __HAL_RCC_XSPIM_RELEASE_RESET();

  /* XSPI common pin configuration */
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_PULLUP;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;

  /* XSPI CLK GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_CLK_PIN;
  GPIO_InitStruct.Alternate = XSPI2_CLK_AF;
  HAL_GPIO_Init(XSPI2_CLK_PORT, &GPIO_InitStruct);

  /* XSPI DQS GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_DQS_PIN;
  GPIO_InitStruct.Alternate = XSPI2_DQS_AF;
  HAL_GPIO_Init(XSPI2_DQS_PORT, &GPIO_InitStruct);

  /* XSPI D0 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D0_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D0_AF;
  HAL_GPIO_Init(XSPI2_D0_PORT, &GPIO_InitStruct);

  /* XSPI D1 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D1_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D1_AF;
  HAL_GPIO_Init(XSPI2_D1_PORT, &GPIO_InitStruct);

  /* XSPI D2 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D2_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D2_AF;
  HAL_GPIO_Init(XSPI2_D2_PORT, &GPIO_InitStruct);

  /* XSPI D3 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D3_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D3_AF;
  HAL_GPIO_Init(XSPI2_D3_PORT, &GPIO_InitStruct);

  /* XSPI D4 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D4_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D4_AF;
  HAL_GPIO_Init(XSPI2_D4_PORT, &GPIO_InitStruct);

  /* XSPI D5 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D5_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D5_AF;
  HAL_GPIO_Init(XSPI2_D5_PORT, &GPIO_InitStruct);

  /* XSPI D6 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D6_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D6_AF;
  HAL_GPIO_Init(XSPI2_D6_PORT, &GPIO_InitStruct);

  /* XSPI D7 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_D7_PIN;
  GPIO_InitStruct.Alternate = XSPI2_D7_AF;
  HAL_GPIO_Init(XSPI2_D7_PORT, &GPIO_InitStruct);

  /* XSPI CS GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI2_CS_PIN;
  GPIO_InitStruct.Alternate = XSPI2_CS_AF;
  HAL_GPIO_Init(XSPI2_CS_PORT, &GPIO_InitStruct);
}

/**
  * @brief  De-Initializes the XSPI MSP.
  * @param  hxspi XSPI handle
  */
static void XSPI_NOR_MspDeInit(XSPI_HandleTypeDef *hxspi)
{
  /* hxspi unused argument(s) compilation warning */
  UNUSED(hxspi);

  /* XSPI GPIO pins de-configuration  */
  HAL_GPIO_DeInit(XSPI2_CLK_PORT, XSPI2_CLK_PIN);
  HAL_GPIO_DeInit(XSPI2_DQS_PORT, XSPI2_DQS_PIN);
  HAL_GPIO_DeInit(XSPI2_CS_PORT, XSPI2_CS_PIN);
  HAL_GPIO_DeInit(XSPI2_D0_PORT, XSPI2_D0_PIN);
  HAL_GPIO_DeInit(XSPI2_D1_PORT, XSPI2_D1_PIN);
  HAL_GPIO_DeInit(XSPI2_D2_PORT, XSPI2_D2_PIN);
  HAL_GPIO_DeInit(XSPI2_D3_PORT, XSPI2_D3_PIN);
  HAL_GPIO_DeInit(XSPI2_D4_PORT, XSPI2_D4_PIN);
  HAL_GPIO_DeInit(XSPI2_D5_PORT, XSPI2_D5_PIN);
  HAL_GPIO_DeInit(XSPI2_D6_PORT, XSPI2_D6_PIN);
  HAL_GPIO_DeInit(XSPI2_D7_PORT, XSPI2_D7_PIN);

  /* Reset the XSPI memory interface */
  __HAL_RCC_XSPI2_FORCE_RESET();
  __HAL_RCC_XSPI2_RELEASE_RESET();

  /* Disable the XSPI memory interface clock */
  __HAL_RCC_XSPI2_CLK_DISABLE();
}

/**
  * @brief  This function reset the XSPI memory.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
static int32_t XSPI_NOR_ResetMemory(uint32_t Instance)
{
  int32_t ret = BSP_OK;

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  else if (MX66UW1G45G_ResetEnable(&hxspi_nor[Instance], BSP_XSPI_NOR_SPI_MODE,
                                    BSP_XSPI_NOR_STR_TRANSFER) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (MX66UW1G45G_ResetMemory(&hxspi_nor[Instance], BSP_XSPI_NOR_SPI_MODE,
                                    BSP_XSPI_NOR_STR_TRANSFER) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (MX66UW1G45G_ResetEnable(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE,
                                    BSP_XSPI_NOR_STR_TRANSFER) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (MX66UW1G45G_ResetMemory(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE,
                                    BSP_XSPI_NOR_STR_TRANSFER) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (MX66UW1G45G_ResetEnable(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE,
                                    BSP_XSPI_NOR_DTR_TRANSFER) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else if (MX66UW1G45G_ResetMemory(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE,
                                    BSP_XSPI_NOR_DTR_TRANSFER) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    XSPI_Nor_Ctx[Instance].IsInitialized = XSPI_ACCESS_INDIRECT;     /* After reset S/W setting to indirect access  */
    XSPI_Nor_Ctx[Instance].InterfaceMode = BSP_XSPI_NOR_SPI_MODE;    /* After reset H/W back to SPI mode by default */
    XSPI_Nor_Ctx[Instance].TransferRate  = BSP_XSPI_NOR_STR_TRANSFER; /* After reset S/W setting to STR mode        */

    /* After SWreset CMD, wait in case SWReset occurred during erase operation */
    HAL_Delay(MX66UW1G45G_RESET_MAX_TIME);
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  This function enables the octal DTR mode of the memory.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
static int32_t XSPI_NOR_EnterDOPIMode(uint32_t Instance)
{
  int32_t ret;
  uint8_t reg[2];

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  /* Enable write operations */
  else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                    XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  /* Write Configuration register 2 (with new dummy cycles) */
  else if (MX66UW1G45G_WriteCfg2Register(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                          XSPI_Nor_Ctx[Instance].TransferRate, MX66UW1G45G_CR2_REG3_ADDR,
                                          MX66UW1G45G_CR2_DC_20_CYCLES) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  /* Enable write operations */
  else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                    XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  /* Write Configuration register 2 (with Octal I/O SPI protocol) */
  else if (MX66UW1G45G_WriteCfg2Register(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                          XSPI_Nor_Ctx[Instance].TransferRate, MX66UW1G45G_CR2_REG1_ADDR,
                                          MX66UW1G45G_CR2_DOPI) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    /* Wait that the configuration is effective and check that memory is ready */
    HAL_Delay(MX66UW1G45G_WRITE_REG_MAX_TIME);

    /* Reconfigure the memory type of the peripheral */
    hxspi_nor[Instance].Init.MemoryType            = HAL_XSPI_MEMTYPE_MACRONIX;
    hxspi_nor[Instance].Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_ENABLE;
    if (HAL_XSPI_Init(&hxspi_nor[Instance]) != HAL_OK)
    {
      ret = BSP_ERROR_PERIPHERAL;
    }
    /* Check Flash busy ? */
    else if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE,
                                              BSP_XSPI_NOR_DTR_TRANSFER) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    /* Check the configuration has been correctly done */
    else if (MX66UW1G45G_ReadCfg2Register(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE, BSP_XSPI_NOR_DTR_TRANSFER,
                                           MX66UW1G45G_CR2_REG1_ADDR, reg) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else if (reg[0] != MX66UW1G45G_CR2_DOPI)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else
    {
      ret = BSP_OK;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  This function enables the octal STR mode of the memory.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
static int32_t XSPI_NOR_EnterSOPIMode(uint32_t Instance)
{
  int32_t ret;
  uint8_t reg[2];

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  /* Enable write operations */
  else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                    XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  /* Write Configuration register 2 (with new dummy cycles) */
  else if (MX66UW1G45G_WriteCfg2Register(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                          XSPI_Nor_Ctx[Instance].TransferRate, MX66UW1G45G_CR2_REG3_ADDR,
                                          MX66UW1G45G_CR2_DC_20_CYCLES) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  /* Enable write operations */
  else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                    XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  /* Write Configuration register 2 (with Octal I/O SPI protocol) */
  else if (MX66UW1G45G_WriteCfg2Register(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                          XSPI_Nor_Ctx[Instance].TransferRate, MX66UW1G45G_CR2_REG1_ADDR,
                                          MX66UW1G45G_CR2_SOPI) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    /* Wait that the configuration is effective and check that memory is ready */
    HAL_Delay(MX66UW1G45G_WRITE_REG_MAX_TIME);

    /* Check Flash busy ? */
    if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE,
                                         BSP_XSPI_NOR_STR_TRANSFER) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    /* Check the configuration has been correctly done */
    else if (MX66UW1G45G_ReadCfg2Register(&hxspi_nor[Instance], BSP_XSPI_NOR_OPI_MODE, BSP_XSPI_NOR_STR_TRANSFER,
                                           MX66UW1G45G_CR2_REG1_ADDR, reg) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else if (reg[0] != MX66UW1G45G_CR2_SOPI)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else
    {
      ret = BSP_OK;
    }
  }

  /* Return BSP status */
  return ret;
}

/**
  * @brief  This function disables the octal DTR or STR mode of the memory.
  * @param  Instance  XSPI instance
  * @retval BSP status
  */
static int32_t XSPI_NOR_ExitOPIMode(uint32_t Instance)
{
  int32_t ret = BSP_OK;
  uint8_t reg[2];

  /* Check if the instance is supported */
  if (Instance >= XSPI_NOR_INSTANCES_NUMBER)
  {
    ret = BSP_ERROR_PARAMETER;
  }
  /* Enable write operations */
  else if (MX66UW1G45G_WriteEnable(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                    XSPI_Nor_Ctx[Instance].TransferRate) != MX66UW1G45G_OK)
  {
    ret = BSP_ERROR_COMPONENT;
  }
  else
  {
    /* Write Configuration register 2 (with SPI protocol) */
    reg[0] = 0;
    reg[1] = 0;
    if (MX66UW1G45G_WriteCfg2Register(&hxspi_nor[Instance], XSPI_Nor_Ctx[Instance].InterfaceMode,
                                       XSPI_Nor_Ctx[Instance].TransferRate, MX66UW1G45G_CR2_REG1_ADDR,
                                       reg[0]) != MX66UW1G45G_OK)
    {
      ret = BSP_ERROR_COMPONENT;
    }
    else
    {
      /* Wait that the configuration is effective and check that memory is ready */
      HAL_Delay(MX66UW1G45G_WRITE_REG_MAX_TIME);

      if (XSPI_Nor_Ctx[Instance].TransferRate == BSP_XSPI_NOR_DTR_TRANSFER)
      {
        /* Reconfigure the memory type of the peripheral */
        hxspi_nor[Instance].Init.MemoryType            = HAL_XSPI_MEMTYPE_MICRON;
        hxspi_nor[Instance].Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_DISABLE;
        if (HAL_XSPI_Init(&hxspi_nor[Instance]) != HAL_OK)
        {
          ret = BSP_ERROR_PERIPHERAL;
        }
      }

      if (ret == BSP_OK)
      {
        /* Check Flash busy ? */
        if (MX66UW1G45G_AutoPollingMemReady(&hxspi_nor[Instance], BSP_XSPI_NOR_SPI_MODE,
                                             BSP_XSPI_NOR_STR_TRANSFER) != MX66UW1G45G_OK)
        {
          ret = BSP_ERROR_COMPONENT;
        }
        /* Check the configuration has been correctly done */
        else if (MX66UW1G45G_ReadCfg2Register(&hxspi_nor[Instance], BSP_XSPI_NOR_SPI_MODE, BSP_XSPI_NOR_STR_TRANSFER,
                                               MX66UW1G45G_CR2_REG1_ADDR, reg) != MX66UW1G45G_OK)
        {
          ret = BSP_ERROR_COMPONENT;
        }
        else if (reg[0] != 0U)
        {
          ret = BSP_ERROR_COMPONENT;
        }
        else
        {
          /* Nothing to do */
        }
      }
    }
  }

  /* Return BSP status */
  return ret;
}


/**
  * @}
  */
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
/** @addtogroup STM32N6570_DK_XSPI_RAM_Private_Functions
  * @{
  */
/**
  * @brief  Initializes the XSPI MSP.
  * @param  hxspi XSPI handle
  */
static void XSPI_RAM_MspInit(XSPI_HandleTypeDef *hxspi)
{
  GPIO_InitTypeDef GPIO_InitStruct;

  /* hxspi unused argument(s) compilation warning */
  UNUSED(hxspi);

  /* XSPI power enable */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_EnableVddIO2();
  HAL_PWREx_ConfigVddIORange(PWR_VDDIO2, PWR_VDDIO_RANGE_1V8);

  /* Enable the XSPI memory interface clock */
  __HAL_RCC_XSPI1_CLK_ENABLE();
  XSPI1_CLK_CLK_ENABLE();
  XSPI1_DQS0_CLK_ENABLE();
  XSPI1_DQS1_CLK_ENABLE();
  XSPI1_D0_CLK_ENABLE();
  XSPI1_D1_CLK_ENABLE();
  XSPI1_D2_CLK_ENABLE();
  XSPI1_D3_CLK_ENABLE();
  XSPI1_D4_CLK_ENABLE();
  XSPI1_D5_CLK_ENABLE();
  XSPI1_D6_CLK_ENABLE();
  XSPI1_D7_CLK_ENABLE();
  XSPI1_D8_CLK_ENABLE();
  XSPI1_D9_CLK_ENABLE();
  XSPI1_D10_CLK_ENABLE();
  XSPI1_D11_CLK_ENABLE();
  XSPI1_D12_CLK_ENABLE();
  XSPI1_D13_CLK_ENABLE();
  XSPI1_D14_CLK_ENABLE();
  XSPI1_D15_CLK_ENABLE();
  XSPI1_CS_CLK_ENABLE();

  /* Reset the XSPI memory interface */
  __HAL_RCC_XSPI1_FORCE_RESET();
  __HAL_RCC_XSPI1_RELEASE_RESET();

  /* Enable and reset XSPI I/O Manager */
  __HAL_RCC_XSPIM_CLK_ENABLE();
  __HAL_RCC_XSPIM_FORCE_RESET();
  __HAL_RCC_XSPIM_RELEASE_RESET();

  /* XSPI common pin configuration */
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_PULLUP;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;

  /* XSPI CLK GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_CLK_PIN;
  GPIO_InitStruct.Alternate = XSPI1_CLK_AF;
  HAL_GPIO_Init(XSPI1_CLK_PORT, &GPIO_InitStruct);

  /* XSPI DQS0 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_DQS0_PIN;
  GPIO_InitStruct.Alternate = XSPI1_DQS0_AF;
  HAL_GPIO_Init(XSPI1_DQS0_PORT, &GPIO_InitStruct);

  /* XSPI DQS1 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_DQS1_PIN;
  GPIO_InitStruct.Alternate = XSPI1_DQS1_AF;
  HAL_GPIO_Init(XSPI1_DQS1_PORT, &GPIO_InitStruct);

  /* XSPI D0 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D0_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D0_AF;
  HAL_GPIO_Init(XSPI1_D0_PORT, &GPIO_InitStruct);

  /* XSPI D1 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D1_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D1_AF;
  HAL_GPIO_Init(XSPI1_D1_PORT, &GPIO_InitStruct);

  /* XSPI D2 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D2_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D2_AF;
  HAL_GPIO_Init(XSPI1_D2_PORT, &GPIO_InitStruct);

  /* XSPI D3 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D3_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D3_AF;
  HAL_GPIO_Init(XSPI1_D3_PORT, &GPIO_InitStruct);

  /* XSPI D4 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D4_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D4_AF;
  HAL_GPIO_Init(XSPI1_D4_PORT, &GPIO_InitStruct);

  /* XSPI D5 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D5_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D5_AF;
  HAL_GPIO_Init(XSPI1_D5_PORT, &GPIO_InitStruct);

  /* XSPI D6 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D6_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D6_AF;
  HAL_GPIO_Init(XSPI1_D6_PORT, &GPIO_InitStruct);

  /* XSPI D7 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D7_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D7_AF;
  HAL_GPIO_Init(XSPI1_D7_PORT, &GPIO_InitStruct);

  /* XSPI D8 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D8_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D8_AF;
  HAL_GPIO_Init(XSPI1_D8_PORT, &GPIO_InitStruct);

  /* XSPI D9 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D9_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D9_AF;
  HAL_GPIO_Init(XSPI1_D9_PORT, &GPIO_InitStruct);

  /* XSPI D10 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D10_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D10_AF;
  HAL_GPIO_Init(XSPI1_D10_PORT, &GPIO_InitStruct);

  /* XSPI D11 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D11_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D11_AF;
  HAL_GPIO_Init(XSPI1_D11_PORT, &GPIO_InitStruct);

  /* XSPI D12 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D12_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D12_AF;
  HAL_GPIO_Init(XSPI1_D12_PORT, &GPIO_InitStruct);

  /* XSPI D13 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D13_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D13_AF;
  HAL_GPIO_Init(XSPI1_D13_PORT, &GPIO_InitStruct);

  /* XSPI D14 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D14_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D14_AF;
  HAL_GPIO_Init(XSPI1_D14_PORT, &GPIO_InitStruct);

  /* XSPI D15 GPIO pin configuration  */
  GPIO_InitStruct.Pin       = XSPI1_D15_PIN;
  GPIO_InitStruct.Alternate = XSPI1_D15_AF;
  HAL_GPIO_Init(XSPI1_D15_PORT, &GPIO_InitStruct);

  /* XSPI CS GPIO pin configuration  */
  GPIO_InitStruct.Alternate = XSPI1_CS_AF;
  GPIO_InitStruct.Pin       = XSPI1_CS_PIN;
  HAL_GPIO_Init(XSPI1_CS_PORT, &GPIO_InitStruct);
}

/**
  * @brief  De-Initializes the XSPI MSP.
  * @param  hxspi XSPI handle
  */
static void XSPI_RAM_MspDeInit(XSPI_HandleTypeDef *hxspi)
{
  /* hxspi unused argument(s) compilation warning */
  UNUSED(hxspi);

  /* XSPI GPIO pins de-configuration  */
  HAL_GPIO_DeInit(XSPI1_CLK_PORT, XSPI1_CLK_PIN);
  HAL_GPIO_DeInit(XSPI1_DQS0_PORT, XSPI1_DQS0_PIN);
  HAL_GPIO_DeInit(XSPI1_DQS1_PORT, XSPI1_DQS1_PIN);
  HAL_GPIO_DeInit(XSPI1_D0_PORT, XSPI1_D0_PIN);
  HAL_GPIO_DeInit(XSPI1_D1_PORT, XSPI1_D1_PIN);
  HAL_GPIO_DeInit(XSPI1_D2_PORT, XSPI1_D2_PIN);
  HAL_GPIO_DeInit(XSPI1_D3_PORT, XSPI1_D3_PIN);
  HAL_GPIO_DeInit(XSPI1_D4_PORT, XSPI1_D4_PIN);
  HAL_GPIO_DeInit(XSPI1_D5_PORT, XSPI1_D5_PIN);
  HAL_GPIO_DeInit(XSPI1_D6_PORT, XSPI1_D6_PIN);
  HAL_GPIO_DeInit(XSPI1_D7_PORT, XSPI1_D7_PIN);
  HAL_GPIO_DeInit(XSPI1_D8_PORT, XSPI1_D8_PIN);
  HAL_GPIO_DeInit(XSPI1_D9_PORT, XSPI1_D9_PIN);
  HAL_GPIO_DeInit(XSPI1_D10_PORT, XSPI1_D10_PIN);
  HAL_GPIO_DeInit(XSPI1_D11_PORT, XSPI1_D11_PIN);
  HAL_GPIO_DeInit(XSPI1_D12_PORT, XSPI1_D12_PIN);
  HAL_GPIO_DeInit(XSPI1_D13_PORT, XSPI1_D13_PIN);
  HAL_GPIO_DeInit(XSPI1_D14_PORT, XSPI1_D14_PIN);
  HAL_GPIO_DeInit(XSPI1_D15_PORT, XSPI1_D15_PIN);
  HAL_GPIO_DeInit(XSPI1_CS_PORT, XSPI1_CS_PIN);

  /* Reset the XSPI memory interface */
  __HAL_RCC_XSPI1_FORCE_RESET();
  __HAL_RCC_XSPI1_RELEASE_RESET();

  /* Disable the XSPI memory interface clock */
  __HAL_RCC_XSPI1_CLK_DISABLE();
}
#endif
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

