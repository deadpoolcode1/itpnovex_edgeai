/**
 ******************************************************************************
 * @file    ux_device_app.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   USBX application definition
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
#include "ux_api.h"
#include "ux_app.h"
#include "ux_dcd_stm32.h"
#include "ux_device_cdc.h"
#include "ux_device_descriptors.h"
#include "ux_device_uvc.h"

/*-->> Private Tunables <<----------------------------------------------------*/

/*-->> Private Definitions <<-------------------------------------------------*/

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/*-->> Private Data <<--------------------------------------------------------*/

PCD_HandleTypeDef                               husb1;                        /*!< USB1 handler */

static uint8_t                                  _ux_stack[UX_STACK_SIZE];     /*!< UX system stack */

#if USBD_CDC_CLASS_ACTIVATED == 1U
  static ULONG                                  _ux_cdc_if_num;               /*!< UX CDC interface number */
  static ULONG                                  _ux_cdc_config_num;           /*!< UX CDC configuration number */
  static UX_SLAVE_CLASS_CDC_ACM_PARAMETER       _ux_cdc_param = {0x00U};      /*!< UX CDC handlers */
#endif /* USBD_CDC_CLASS_ACTIVATED */

#if USBD_UVC_CLASS_ACTIVATED == 1U
  static ULONG                                  _ux_uvc_if_num;               /*!< UX UVC interface number */
  static ULONG                                  _ux_uvc_config_num;           /*!< UX UVC configuration number */
  static UX_DEVICE_CLASS_VIDEO_PARAMETER        _ux_uvc_param = {0x00U};      /*!< UX UVC handlers */
  static UX_DEVICE_CLASS_VIDEO_STREAM_PARAMETER _ux_uvc_stream[1] = {0x00U};  /*!< UX UVC stream handlers */
#endif /* USBD_UVC_CLASS_ACTIVATED */

/*-->> Private Functions <<---------------------------------------------------*/

static UINT _ux_app_config(VOID);
static VOID _ux_phy_init(VOID);

/*-->> Public API <<----------------------------------------------------------*/

void ux_app_init(void)
{
  int32_t status;

  /* Initialize peripheral */
  _ux_phy_init();

  /* Configure devices */
  status = _ux_app_config();
  if (status != UX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize devices */
  #if USBD_CDC_CLASS_ACTIVATED == 1U
  status = ux_cdc_init();
  if (status != UX_SUCCESS)
  {
    Error_Handler();
  }
  #endif /* USBD_CDC_CLASS_ACTIVATED */

  /* Start the USB device */
  if (HAL_PCD_Start(&husb1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Report USB initialized */
  #if USBD_CDC_CLASS_ACTIVATED == 1U
  task_raise_event(TX_EVT_UX_CDC_READY);
  #endif /* USBD_CDC_CLASS_ACTIVATED */
  #if USBD_UVC_CLASS_ACTIVATED == 1U
  task_raise_event(TX_EVT_UX_UVC_READY);
  #endif /* USBD_UVC_CLASS_ACTIVATED */
}

/*-->> Private Functions <<---------------------------------------------------*/

UINT _ux_app_config(VOID)
{
  UCHAR *device_framework_hs;
  UCHAR *device_framework_fs;
  ULONG device_framework_hs_length;
  ULONG device_framework_fs_length;
  ULONG string_framework_length;
  ULONG language_id_framework_length;
  UCHAR *string_framework;
  UCHAR *language_id_framework;
  UINT  status;

  /* Initialize USBX System pool */
  status = ux_system_initialize(_ux_stack, UX_STACK_SIZE, UX_NULL, 0);
  if (status != UX_SUCCESS)
  {
    return UX_ERROR;
  }

  /* Get Device Framework High Speed and get the length */
  device_framework_hs = USBD_Get_Device_Framework_Speed(USBD_HIGH_SPEED, &device_framework_hs_length);

  /* Get Device Framework Full Speed and get the length */
  device_framework_fs = USBD_Get_Device_Framework_Speed(USBD_FULL_SPEED, &device_framework_fs_length);

  /* Get String Framework and get the length */
  string_framework = USBD_Get_String_Framework(&string_framework_length);

  /* Get Language Id Framework and get the length */
  language_id_framework = USBD_Get_Language_Id_Framework(&language_id_framework_length);

  /* Install the device portion of USBX */
  status = ux_device_stack_initialize(
    device_framework_hs,
    device_framework_hs_length,
    device_framework_fs,
    device_framework_fs_length,
    string_framework,
    string_framework_length,
    language_id_framework,
    language_id_framework_length,
    NULL
  );
  if (status != UX_SUCCESS)
  {
    return UX_ERROR;
  }

#if USBD_CDC_CLASS_ACTIVATED == 1U
  /* CDC */
  /* Initialize the cdc acm class parameters for the device */
  _ux_cdc_param.ux_slave_class_cdc_acm_instance_activate   = ux_cdc_activate;
  _ux_cdc_param.ux_slave_class_cdc_acm_instance_deactivate = ux_cdc_deactivate;
  _ux_cdc_param.ux_slave_class_cdc_acm_parameter_change    = ux_cdc_parameter_change;

  /* Get cdc acm configuration/interface number */
  _ux_cdc_config_num = USBD_Get_Configuration_Number(CLASS_TYPE_CDC_ACM, 0);
  _ux_cdc_if_num     = USBD_Get_Interface_Number(CLASS_TYPE_CDC_ACM, 0);

  /* Initialize the device cdc acm class */
  status = ux_device_stack_class_register(
    _ux_system_slave_class_cdc_acm_name,
    ux_device_class_cdc_acm_entry,
    _ux_cdc_config_num,
    _ux_cdc_if_num,
    &_ux_cdc_param
  );
  if (status != UX_SUCCESS)
  {
    return UX_ERROR;
  }
#endif /* USBD_CDC_CLASS_ACTIVATED */

#if USBD_UVC_CLASS_ACTIVATED == 1U
  /* UVC */
  /* Initialize the uvc class parameters for the device */
  _ux_uvc_stream[0].ux_device_class_video_stream_parameter_thread_entry            = ux_device_class_video_write_thread_entry;
  _ux_uvc_stream[0].ux_device_class_video_stream_parameter_thread_stack_size       = 0x00U;
  _ux_uvc_stream[0].ux_device_class_video_stream_parameter_max_payload_buffer_nb   = UX_UVC_BUFFER_NUM;
  _ux_uvc_stream[0].ux_device_class_video_stream_parameter_max_payload_buffer_size = USBD_UVC_EPIN_HS_MPS;
  _ux_uvc_stream[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_change      = ux_uvc_change;
  _ux_uvc_stream[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_payload_done= ux_uvc_stream_done;
  _ux_uvc_stream[0].ux_device_class_video_stream_parameter_callbacks.ux_device_class_video_stream_request     = ux_uvc_stream_request;

  _ux_uvc_param.ux_device_class_video_parameter_streams    = _ux_uvc_stream;
  _ux_uvc_param.ux_device_class_video_parameter_streams_nb = 0x01U;
  _ux_uvc_param.ux_device_class_video_parameter_callbacks.ux_device_class_video_request           = NULL;
  _ux_uvc_param.ux_device_class_video_parameter_callbacks.ux_slave_class_video_instance_activate  = ux_uvc_activate;
  _ux_uvc_param.ux_device_class_video_parameter_callbacks.ux_slave_class_video_instance_deactivate= ux_uvc_deactivate;

  /* Get uvc configuration/interface number */
  _ux_uvc_config_num = USBD_Get_Configuration_Number(CLASS_TYPE_UVC, 0);
  _ux_uvc_if_num     = USBD_Get_Interface_Number(CLASS_TYPE_UVC, 0);

  /* Initialize the device uvc class */
  status = ux_device_stack_class_register(
    _ux_system_device_class_video_name,
    ux_device_class_video_entry,
    _ux_uvc_config_num,
    _ux_uvc_if_num,
    &_ux_uvc_param
  );
  if (status != UX_SUCCESS)
  {
    return UX_ERROR;
  }
#endif /* USBD_UVC_CLASS_ACTIVATED */

  /* Initialize the driver */
  status = ux_dcd_stm32_initialize((ULONG)USB1_OTG_HS, (ULONG)&husb1);
  if (status != UX_SUCCESS)
  {
    Error_Handler();
  }
  return status;
}

static VOID _ux_phy_init(VOID)
{
  /* Configure peripheral */
  husb1.Instance                 = USB1_OTG_HS;
  husb1.Init.dev_endpoints       = 9U;
  husb1.Init.speed               = PCD_SPEED_HIGH;
  husb1.Init.phy_itface          = USB_OTG_HS_EMBEDDED_PHY;
  husb1.Init.Sof_enable          = DISABLE;
  husb1.Init.low_power_enable    = DISABLE;
  husb1.Init.lpm_enable          = DISABLE;
  husb1.Init.use_dedicated_ep1   = DISABLE;
  husb1.Init.vbus_sensing_enable = DISABLE;
  husb1.Init.dma_enable          = DISABLE;
  if (HAL_PCD_Init(&husb1) != HAL_OK)
  {
    Error_Handler();
  }

  /* USB packet memory area configuration: Total = 0x400 > 4096 */
  HAL_PCDEx_SetRxFiFo(&husb1, 0x200);    // EP0.OUT => Shared  (      > 2048    > 0x200)
  HAL_PCDEx_SetTxFiFo(&husb1, 0, 0x70);  // EP0.IN  => Control (0x80  > 448     >  0x70)
  HAL_PCDEx_SetTxFiFo(&husb1, 1, 0x10);  // EP1.IN  => CDC.Cmd (0x81  > HS@64   >  0x10)
  HAL_PCDEx_SetTxFiFo(&husb1, 2, 0x80);  // EP2.IN  => CDC.In  (0x82  > HS@512  >  0x80)
  HAL_PCDEx_SetTxFiFo(&husb1, 3, 0x100); // EP3.IN  => UVS.Iso (0x83  > HS@1024 > 0x100)
}

void HAL_PCD_MspInit(PCD_HandleTypeDef* handle)
{
  /* Enable VDDUSB */
  HAL_PWREx_EnableVddUSBVMEN();
  while(__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY));
  HAL_PWREx_EnableVddUSB();

  /* Clock enable */
  LL_AHB5_GRP1_ForceReset(0x00800000);
  __HAL_RCC_USB1_OTG_HS_FORCE_RESET();
  __HAL_RCC_USB1_OTG_HS_PHY_FORCE_RESET();
  LL_RCC_HSE_SelectHSEDiv2AsDiv2Clock();
  LL_AHB5_GRP1_ReleaseReset(0x00800000);
  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();

  /* Update control registers */
  HAL_Delay(1);
  USB1_HS_PHYC->USBPHYC_CR &= ~(0x7 << 0x4);
  USB1_HS_PHYC->USBPHYC_CR |= (0x1 << 16) | (0x2 << 4) | (0x1 << 2) | 0x1U;
  __HAL_RCC_USB1_OTG_HS_PHY_RELEASE_RESET();
  HAL_Delay(1);

  /* Complete initialization */
  __HAL_RCC_USB1_OTG_HS_RELEASE_RESET();
  __HAL_RCC_USB1_OTG_HS_PHY_CLK_ENABLE();

  /* Configure interruptions */
  HAL_NVIC_SetPriority(USB1_OTG_HS_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(USB1_OTG_HS_IRQn);
}
