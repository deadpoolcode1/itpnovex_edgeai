/**
 *******************************************************************************
 * @file    n6cam_init.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam initialization utilities API
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
#include "n6cam_core.h"
#include "n6cam_i2c.h"
#include "n6cam_init.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup INIT
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

/* OTP user configuration (ID: 124)
 * XSPI port 1 and 2 are configured as HSLV (High Speed Low Voltage) ports
 */
#define BSP_OTP_HW_ID           124U
#define BSP_OTP_HW_HSLV_VDDIO2  16U
#define BSP_OTP_HW_HSLV_VDDIO3  15U
#define BSP_OTP_HW_MASK         ((1U << BSP_OTP_HW_HSLV_VDDIO2) | (1U << BSP_OTP_HW_HSLV_VDDIO3))

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

/** @brief OTP configuration */
typedef struct
{
  uint32_t fuse;
  uint32_t mask;
} t_otp_config;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/** @brief OTP configuration */
static const t_otp_config _otp_config[] = {
  { BSP_OTP_HW_ID , BSP_OTP_HW_MASK },
};
static const uint32_t _otp_config_count = sizeof(_otp_config) / sizeof(t_otp_config);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

void bsp_config_iac(void)
{
  __HAL_RCC_IAC_CLK_ENABLE();
  __HAL_RCC_IAC_FORCE_RESET();
  __HAL_RCC_IAC_RELEASE_RESET();
}

void bsp_config_fuse(void)
{
  BSEC_HandleTypeDef hbsec = {};
  const t_otp_config *config;
  uint32_t           data;

  /* Configure OTP fuses */
  hbsec.Instance = BSEC;
  for (uint8_t idx = 0; idx < _otp_config_count; idx++)
  {
    config = &_otp_config[idx];
    /* Read OTP fuses */
    if (HAL_BSEC_OTP_Read(&hbsec, config->fuse, &data) != HAL_OK)
    {
      Error_Handler();
    }
    /* Check if is already configured. If not, attempt to configure */
    if ((data & config->mask) == config->mask)
    {
      continue;
    }
    /* Configure OTP fuses */
    data |= config->mask;
    if (HAL_BSEC_OTP_Program(&hbsec, config->fuse, data, HAL_BSEC_NORMAL_PROG) != HAL_OK)
    {
      Error_Handler();
    }
    /* Validate OTP fuses */
    if (HAL_BSEC_OTP_Read(&hbsec, config->fuse, &data) != HAL_OK)
    {
      Error_Handler();
    }
    if ((data & config->mask) != config->mask)
    {
      Error_Handler();
    }
  }
}

void bsp_config_pwr(void)
{
  /* Configure power domain */
  __HAL_RCC_PWR_CLK_ENABLE();
  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_PWREx_GetSupplyConfig() != PWR_EXTERNAL_SOURCE_SUPPLY)
  {
    Error_Handler();
  }

  #if defined(N6CAM)
  /* From DK errata: Fix power-up issue on USBC connector
   * TCPP03 power down due shut off sink path MOSFET
   * PA4 (EN) -> OUTPUT - PULL_DOWN (HW)
   */
  bsp_io_init(IO_PWR_USB1_EN);
  bsp_io_set_state(IO_PWR_USB1_EN, false);

  /* Ensure VDDCORE=0.89V before increasing the system frequency.
   * Note: Assuming voltage ramp-up speed of 1[mV/us]: 100[mV] -> 100[us]
   */
  uint8_t config = 0x62U;
  if (bsp_i2c_init(I2C_POWER, 100000U) != BSP_OK)
  {
    Error_Handler();
  }
  bsp_i2c_set_mode(I2C_POWER, I2C_MODE_BLOCK);
  bsp_i2c_write_reg8(I2C_POWER, 0x49U << 1U, 0x01U, &config, 1U, 1000U);
  bsp_i2c_deinit(I2C_POWER);
  HAL_Delay(10U);
  #endif /* N6CAM */

  /* Configure voltage scaling for highest frequency */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
  {
    Error_Handler();
  }

  /* Enable power for RTC (and reset clocks) */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_BACKUPRESET_FORCE();
  __HAL_RCC_BACKUPRESET_RELEASE();

  /* Enable power for LSE */
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_HIGH);
}

void bsp_config_risaf(void)
{
  __HAL_RCC_RIFSC_CLK_ENABLE();
  RIMC_MasterConfig_t RIMC_master = {0};

  RIMC_master.MasterCID = RIF_CID_1;
  RIMC_master.SecPriv   = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;

  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D , &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_NPU   , &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_SDMMC1, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_SDMMC2, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_VENC  , &RIMC_master);

  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RCC_PERIPH_INDEX_GPDMA1 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RCC_PERIPH_INDEX_HPDMA1 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_CSI   , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DMA2D , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_I2C1  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_I2C2  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_I2C4  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_JPEG  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_NPU   , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SDMMC1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SDMMC2, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SPI3  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_SPI5  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_TIM2  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_TIM3  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_USART1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_USART2, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_VENC  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_WWDG  , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_XSPI1 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_XSPI2 , RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
}

void bsp_system_fixes(void)
{
  /* Fix to enable APB CLKs */
  RCC->BUSENSR = 0xFFFFFFFF;

  /* Reset SYSSW and CPUSW */
  RCC->CFGR1 = 0x00000000U;
}

void bsp_usb_fixes(void)
{
  /* Disable USB1 OTG interrupt with OTG1 reset */
  NVIC_DisableIRQ(USB1_OTG_HS_IRQn);
  RCC->AHB5RSTSR = RCC_AHB5RSTSR_OTG1RSTS;
  RCC->AHB5RSTCR = RCC_AHB5RSTCR_OTG1RSTC;

  /* Deactivate USB OTG1 clock */
  RCC->AHB5ENCR = RCC_AHB5ENCR_OTG1ENC;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Controllers -->
* @} <!-- End: INIT -->
*//*--------------------------------------------------------------------------*/
