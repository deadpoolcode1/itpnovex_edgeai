/**
  ******************************************************************************
  * @file    n6cam_xspi.h
  * @author  SIANA (based on MCD Application Team)
  * @brief   This file contains the common defines and functions prototypes for
  *          the n6cam_xspi.c driver.
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
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef N6CAM_XSPI_H
#define N6CAM_XSPI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "n6cam_error.h"

#ifndef USE_NOR_MEMORY_MX66UW1G45G
#define USE_NOR_MEMORY_MX66UW1G45G           1
#endif

#ifndef USE_RAM_MEMORY_APS256XX
#define USE_RAM_MEMORY_APS256XX              1
#endif

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
#include "../Components/mx66uw1g45g/mx66uw1g45g.h"
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
#include "../Components/aps256xx/aps256xx.h"
#endif

/** @addtogroup BSP
  * @{
  */

/** @addtogroup STM32N6570_DK
  * @{
  */

/** @addtogroup STM32N6570_DK_XSPI
  * @{
  */

/* Exported types ------------------------------------------------------------*/
/** @defgroup STM32N6570_DK_XSPI_Exported_Types XSPI Exported Types
  * @{
  */
typedef enum
{
  XSPI_ACCESS_NONE = 0,          /*!<  Instance not initialized,              */
  XSPI_ACCESS_INDIRECT,          /*!<  Instance use indirect mode access      */
  XSPI_ACCESS_MMP                /*!<  Instance use Memory Mapped Mode read   */
} XSPI_Access_t;

#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 1)
typedef struct
{
  pXSPI_CallbackTypeDef  pMspInitCb;
  pXSPI_CallbackTypeDef  pMspDeInitCb;
} BSP_XSPI_Cb_t;
#endif /* (USE_HAL_XSPI_REGISTER_CALLBACKS == 1) */

typedef struct
{
  uint32_t MemorySize;
  uint32_t ClockPrescaler;
  uint32_t SampleShifting;
  uint32_t TransferRate;
} MX_XSPI_InitTypeDef;
/**
  * @}
  */

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @defgroup STM32N6570_DK_XSPI_NOR_Exported_Types XSPI_NOR Exported Types
  * @{
  */
#define BSP_XSPI_NOR_Info_t                MX66UW1G45G_Info_t
#define BSP_XSPI_NOR_Interface_t           MX66UW1G45G_Interface_t
#define BSP_XSPI_NOR_Transfer_t            MX66UW1G45G_Transfer_t
#define BSP_XSPI_NOR_Erase_t               MX66UW1G45G_Erase_t

typedef struct
{
  XSPI_Access_t              IsInitialized;  /*!<  Instance access Flash method     */
  BSP_XSPI_NOR_Interface_t   InterfaceMode;  /*!<  Flash Interface mode of Instance */
  BSP_XSPI_NOR_Transfer_t    TransferRate;   /*!<  Flash Transfer mode of Instance  */
} XSPI_NOR_Ctx_t;

typedef struct
{
  BSP_XSPI_NOR_Interface_t   InterfaceMode;      /*!<  Current Flash Interface mode */
  BSP_XSPI_NOR_Transfer_t    TransferRate;       /*!<  Current Flash Transfer rate  */
} BSP_XSPI_NOR_Init_t;
/**
  * @}
  */
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
/** @defgroup STM32N6570_DK_XSPI_RAM_Exported_Types XSPI_RAM Exported Types
  * @{
  */
#define BSP_XSPI_RAM_BurstLength_t APS256XX_BurstLength_t

typedef enum
{
  BSP_XSPI_RAM_VARIABLE_LATENCY = HAL_XSPI_VARIABLE_LATENCY,
  BSP_XSPI_RAM_FIXED_LATENCY    = HAL_XSPI_FIXED_LATENCY
} BSP_XSPI_RAM_Latency_t;

typedef enum
{
  BSP_XSPI_RAM_HYBRID_BURST = 0,
  BSP_XSPI_RAM_LINEAR_BURST
} BSP_XSPI_RAM_BurstType_t;

typedef struct
{
  XSPI_Access_t               IsInitialized;   /*!< Instance access Flash method */
  BSP_XSPI_RAM_Latency_t      LatencyType;   /*!< Latency Type of Instance     */
  BSP_XSPI_RAM_BurstType_t    BurstType;     /*!< Burst Type of Instance       */
  BSP_XSPI_RAM_BurstLength_t  BurstLength;   /*!< Burst Length of Instance     */
} XSPI_RAM_Ctx_t;

/**
  * @}
  */
#endif

/* Exported constants --------------------------------------------------------*/
#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @defgroup STM32N6570_DK_XSPI_NOR_Exported_Constants XSPI_NOR Exported Constants
  * @{
  */
#define XSPI_NOR_INSTANCES_NUMBER         1U

/* Definition for XSPI modes */
#define BSP_XSPI_NOR_SPI_MODE (BSP_XSPI_NOR_Interface_t)MX66UW1G45G_SPI_MODE /* 1 Cmd, 1 Address and 1 Data Lines */
#define BSP_XSPI_NOR_OPI_MODE (BSP_XSPI_NOR_Interface_t)MX66UW1G45G_OPI_MODE /* 8 Cmd, 8 Address and 8 Data Lines */

/* Definition for XSPI transfer rates */
#define BSP_XSPI_NOR_STR_TRANSFER  (BSP_XSPI_NOR_Transfer_t)MX66UW1G45G_STR_TRANSFER   /* Single Transfer Rate */
#define BSP_XSPI_NOR_DTR_TRANSFER  (BSP_XSPI_NOR_Transfer_t)MX66UW1G45G_DTR_TRANSFER   /* Double Transfer Rate */

/* XSPI erase types */
#define BSP_XSPI_NOR_ERASE_4K             MX66UW1G45G_ERASE_4K
#define BSP_XSPI_NOR_ERASE_64K            MX66UW1G45G_ERASE_64K
#define BSP_XSPI_NOR_ERASE_CHIP           MX66UW1G45G_ERASE_BULK

/* XSPI block sizes */
#define BSP_XSPI_NOR_BLOCK_4K             MX66UW1G45G_SUBSECTOR_4K
#define BSP_XSPI_NOR_BLOCK_64K            MX66UW1G45G_SECTOR_64K
/**
  * @}
  */
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
/** @defgroup STM32N6570_DK_XSPI_RAM_Exported_Constants XSPI_RAM Exported Constants
  * @{
  */
#define XSPI_RAM_INSTANCES_NUMBER         1U

/* XSPI Burst length */
#define BSP_XSPI_RAM_BURST_16_BYTES       (BSP_XSPI_RAM_BurstLength_t)APS256XX_BURST_16_BYTES
#define BSP_XSPI_RAM_BURST_32_BYTES       (BSP_XSPI_RAM_BurstLength_t)APS256XX_BURST_32_BYTES
#define BSP_XSPI_RAM_BURST_64_BYTES       (BSP_XSPI_RAM_BurstLength_t)APS256XX_BURST_64_BYTES
#define BSP_XSPI_RAM_BURST_128_BYTES      (BSP_XSPI_RAM_BurstLength_t)APS256XX_BURST_128_BYTES
/**
  * @}
  */
#endif

/* Exported variables --------------------------------------------------------*/

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @defgroup STM32N6570_DK_XSPI_NOR_Exported_Variables XSPI_NOR Exported Variables
  * @{
  */
extern XSPI_HandleTypeDef hxspi_nor[XSPI_NOR_INSTANCES_NUMBER];
extern XSPI_NOR_Ctx_t Ospi_Nor_Ctx[XSPI_NOR_INSTANCES_NUMBER];
/**
  * @}
  */
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
/** @defgroup STM32N6570_DK_XSPI_RAM_Exported_Variables XSPI_RAM Exported Variables
  * @{
  */
extern XSPI_HandleTypeDef hxspi_ram[XSPI_RAM_INSTANCES_NUMBER];
extern XSPI_RAM_Ctx_t Ospi_Ram_Ctx[XSPI_RAM_INSTANCES_NUMBER];
/**
  * @}
  */
#endif

/* Exported functions --------------------------------------------------------*/

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
/** @defgroup STM32N6570_DK_XSPI_NOR_Exported_Functions XSPI_NOR Exported Functions
  * @{
  */
int32_t BSP_XSPI_NOR_Init(uint32_t Instance, BSP_XSPI_NOR_Init_t *Init);
int32_t BSP_XSPI_NOR_DeInit(uint32_t Instance);
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 1)
int32_t BSP_XSPI_NOR_RegisterMspCallbacks(uint32_t Instance, BSP_XSPI_Cb_t *CallBacks);
int32_t BSP_XSPI_NOR_RegisterDefaultMspCallbacks(uint32_t Instance);
#endif /* (USE_HAL_XSPI_REGISTER_CALLBACKS == 1) */
int32_t BSP_XSPI_NOR_Read(uint32_t Instance, uint8_t *pData, uint32_t ReadAddr, uint32_t Size);
int32_t BSP_XSPI_NOR_Write(uint32_t Instance, uint8_t *pData, uint32_t WriteAddr, uint32_t Size);
int32_t BSP_XSPI_NOR_Erase_Block(uint32_t Instance, uint32_t BlockAddress, BSP_XSPI_NOR_Erase_t BlockSize);
int32_t BSP_XSPI_NOR_Erase_Chip(uint32_t Instance);
int32_t BSP_XSPI_NOR_GetStatus(uint32_t Instance);
int32_t BSP_XSPI_NOR_GetInfo(uint32_t Instance, BSP_XSPI_NOR_Info_t *pInfo);
int32_t BSP_XSPI_NOR_EnableMemoryMappedMode(uint32_t Instance);
int32_t BSP_XSPI_NOR_DisableMemoryMappedMode(uint32_t Instance);
int32_t BSP_XSPI_NOR_ReadID(uint32_t Instance, uint8_t *Id);
int32_t BSP_XSPI_NOR_ConfigFlash(uint32_t Instance, BSP_XSPI_NOR_Interface_t Mode, BSP_XSPI_NOR_Transfer_t Rate);
int32_t BSP_XSPI_NOR_SuspendErase(uint32_t Instance);
int32_t BSP_XSPI_NOR_ResumeErase(uint32_t Instance);
int32_t BSP_XSPI_NOR_EnterDeepPowerDown(uint32_t Instance);
int32_t BSP_XSPI_NOR_LeaveDeepPowerDown(uint32_t Instance);

/**
  * @}
  */
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
/** @defgroup STM32N6570_DK_XSPI_RAM_Exported_Functions XSPI_RAM Exported Functions
  * @{
  */
int32_t BSP_XSPI_RAM_Init(uint32_t Instance);
int32_t BSP_XSPI_RAM_DeInit(uint32_t Instance);
#if (USE_HAL_XSPI_REGISTER_CALLBACKS == 1)
int32_t BSP_XSPI_RAM_RegisterMspCallbacks(uint32_t Instance, BSP_XSPI_Cb_t *CallBacks);
int32_t BSP_XSPI_RAM_RegisterDefaultMspCallbacks(uint32_t Instance);
#endif /* (USE_HAL_XSPI_REGISTER_CALLBACKS == 1) */
int32_t BSP_XSPI_RAM_Read(uint32_t Instance, uint8_t *pData, uint32_t ReadAddr, uint32_t Size);
int32_t BSP_XSPI_RAM_Write(uint32_t Instance, uint8_t *pData, uint32_t WriteAddr, uint32_t Size);
int32_t BSP_XSPI_RAM_EnableMemoryMappedMode(uint32_t Instance);
int32_t BSP_XSPI_RAM_DisableMemoryMappedMode(uint32_t Instance);
int32_t BSP_XSPI_RAM_ReadID(uint32_t Instance, uint8_t *Id);
/**
  * @}
  */
#endif


/** @defgroup STM32N6570_DK_XSPI_Exported_Init_Functions XSPI_Init Exported Functions
  * @{
  */

/* These functions can be modified in case the current settings
   need to be changed for specific application needs */

#if (USE_NOR_MEMORY_MX66UW1G45G == 1)
HAL_StatusTypeDef MX_XSPI_NOR_Init(XSPI_HandleTypeDef *hxspi, MX_XSPI_InitTypeDef *Init);
#endif

#if (USE_RAM_MEMORY_APS256XX == 1)
HAL_StatusTypeDef MX_XSPI_RAM_Init(XSPI_HandleTypeDef *hxspi, MX_XSPI_InitTypeDef *Init);
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

#ifdef __cplusplus
}
#endif

#endif /* N6CAM_XSPI_H */
