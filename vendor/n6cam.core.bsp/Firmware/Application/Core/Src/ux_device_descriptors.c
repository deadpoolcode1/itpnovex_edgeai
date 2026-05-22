/**
 ******************************************************************************
 * @file    ux_device_descriptors.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   USBX device descriptors
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
#include "camera_task.h"
#include "ux_device_descriptors.h"

/*-->> Private Tunables <<----------------------------------------------------*/

/*-->> Private Definitions <<-------------------------------------------------*/

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/*-->> Private Data <<--------------------------------------------------------*/

USBD_DevClassHandleTypeDef _ux_dev_fs = { 0 };
USBD_DevClassHandleTypeDef _ux_dev_hs = { 0 };

uint8_t _ux_class_instances[USBD_MAX_SUPPORTED_CLASS] = {
#if USBD_UVC_CLASS_ACTIVATED == 1U
  CLASS_TYPE_UVC,
#endif /* USBD_UVC_CLASS_ACTIVATED */
#if USBD_CDC_CLASS_ACTIVATED == 1U
  CLASS_TYPE_CDC_ACM,
#endif /* USBD_CDC_CLASS_ACTIVATED */
};

static const uint8_t _ux_uvc_guid[USBD_GUID_SIZE] = UVC_GUID_H264;

/* The generic device descriptor buffer that will be filled by builder
   Size of the buffer is the maximum possible device FS descriptor size. */
#if defined ( __ICCARM__ ) /* IAR Compiler */
#pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
__ALIGN_BEGIN static uint8_t _ux_dev_fw_fs[USBD_FRAMEWORK_MAX_DESC_SZ] __ALIGN_END = {0};

/* The generic device descriptor buffer that will be filled by builder
   Size of the buffer is the maximum possible device HS descriptor size. */
#if defined ( __ICCARM__ ) /* IAR Compiler */
#pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
__ALIGN_BEGIN static uint8_t _ux_dev_fw_hs[USBD_FRAMEWORK_MAX_DESC_SZ] __ALIGN_END = {0};

static uint8_t *_p_ux_dev_fw_fs = _ux_dev_fw_fs;
static uint8_t *_p_ux_dev_fw_hs = _ux_dev_fw_hs;

/* String Device Framework :
 Byte 0 and 1 : Word containing the language ID : 0x0904 for US
 Byte 2       : Byte containing the index of the descriptor
 Byte 3       : Byte containing the length of the descriptor string
*/
#if defined ( __ICCARM__ ) /* IAR Compiler */
#pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
__ALIGN_BEGIN UCHAR _ux_string_fw[USBD_STRING_FRAMEWORK_MAX_LENGTH]
__ALIGN_END = {0};

/* Multiple languages are supported on the device, to add
   a language besides English, the Unicode language code must
   be appended to the language_id_framework array and the length
   adjusted accordingly. */

#if defined ( __ICCARM__ ) /* IAR Compiler */
#pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
__ALIGN_BEGIN UCHAR _ux_lang_id_fw[LANGUAGE_ID_MAX_LENGTH]
__ALIGN_END = {0};

/*-->> Private Functions <<---------------------------------------------------*/

static void     USBD_Desc_GetString(uint8_t *desc, uint8_t *Buffer, uint16_t *len);
static uint8_t  USBD_Desc_GetLen(uint8_t *buf);

static uint8_t  *USBD_Device_Framework_Builder(USBD_DevClassHandleTypeDef *pdev,uint8_t *pDevFrameWorkDesc, uint8_t *UserClassInstance, uint8_t Speed);

static uint8_t  USBD_FrameWork_AddToConfDesc(USBD_DevClassHandleTypeDef *pdev, uint8_t Speed, uint8_t *pCmpstConfDesc);
static uint8_t  USBD_FrameWork_AddClass(USBD_DevClassHandleTypeDef *pdev, USBD_CompositeClassTypeDef class, uint8_t cfgidx, uint8_t Speed, uint8_t *pCmpstConfDesc);
static uint8_t  USBD_FrameWork_FindFreeIFNbr(USBD_DevClassHandleTypeDef *pdev);
static void     USBD_FrameWork_AddConfDesc(uint32_t Conf, uint32_t *pSze);
static void     USBD_FrameWork_AssignEp(USBD_DevClassHandleTypeDef *pdev, uint8_t Add, uint8_t Type, uint32_t Sze);

#if USBD_CDC_CLASS_ACTIVATED == 1U
  static void   USBD_FrameWork_CDCDesc(USBD_DevClassHandleTypeDef *pdev, uint32_t pConf, uint32_t *Sze);
#endif /* USBD_CDC_ACM_CLASS_ACTIVATED */

#if USBD_UVC_CLASS_ACTIVATED == 1U
  static void   USBD_FrameWork_UVCDesc(USBD_DevClassHandleTypeDef *pdev, uint32_t pConf, uint32_t *Sze);
#endif /* USBD_UVC_CLASS_ACTIVATED */

/*-->> Public API <<----------------------------------------------------------*/

uint8_t *USBD_Get_Device_Framework_Speed(uint8_t Speed, ULONG *Length)
{
  uint8_t *pFrameWork = NULL;
  if (USBD_FULL_SPEED == Speed)
  {
    USBD_Device_Framework_Builder(&_ux_dev_fs, _p_ux_dev_fw_fs, _ux_class_instances, Speed);

    /* Get the length of USBD_device_framework_full_speed */
    *Length = (ULONG)(_ux_dev_fs.CurrDevDescSz + _ux_dev_fs.CurrConfDescSz);

    pFrameWork = _p_ux_dev_fw_fs;
  }
  else
  {
    USBD_Device_Framework_Builder(&_ux_dev_hs, _p_ux_dev_fw_hs, _ux_class_instances, Speed);

    /* Get the length of USBD_device_framework_high_speed */
    *Length = (ULONG)(_ux_dev_hs.CurrDevDescSz + _ux_dev_hs.CurrConfDescSz);

    pFrameWork = _p_ux_dev_fw_hs;
  }
  return pFrameWork;
}

uint8_t *USBD_Get_String_Framework(ULONG *Length)
{
  uint16_t len = 0U;
  uint8_t count = 0U;

  /* Set the Manufacturer language Id and index in _ux_string_fw */
  _ux_string_fw[count++] = USBD_LANGID_STRING & 0xFF;
  _ux_string_fw[count++] = USBD_LANGID_STRING >> 8;
  _ux_string_fw[count++] = USBD_IDX_MFC_STR;

  /* Set the Manufacturer string in string_framework */
  USBD_Desc_GetString((uint8_t *)USBD_MANUFACTURER_STRING, _ux_string_fw + count, &len);

  /* Set the Product language Id and index in _ux_string_fw */
  count += len + 1;
  _ux_string_fw[count++] = USBD_LANGID_STRING & 0xFF;
  _ux_string_fw[count++] = USBD_LANGID_STRING >> 8;
  _ux_string_fw[count++] = USBD_IDX_PRODUCT_STR;

  /* Set the Product string in _ux_string_fw */
  USBD_Desc_GetString((uint8_t *)USBD_PRODUCT_STRING, _ux_string_fw + count, &len);

  /* Set Serial language Id and index in string_framework */
  count += len + 1;
  _ux_string_fw[count++] = USBD_LANGID_STRING & 0xFF;
  _ux_string_fw[count++] = USBD_LANGID_STRING >> 8;
  _ux_string_fw[count++] = USBD_IDX_SERIAL_STR;

  /* Set the Serial number in _ux_string_fw */
  USBD_Desc_GetString((uint8_t *)USBD_SERIAL_NUMBER, _ux_string_fw + count, &len);

  /* Get the length of USBD_string_framework */
  *Length = strlen((const char *)_ux_string_fw);

  return _ux_string_fw;
}

uint8_t *USBD_Get_Language_Id_Framework(ULONG *Length)
{
  uint8_t count = 0U;

  /* Set the language Id in _ux_lang_id_fw */
  _ux_lang_id_fw[count++] = USBD_LANGID_STRING & 0xFF;
  _ux_lang_id_fw[count++] = USBD_LANGID_STRING >> 8;

  /* Get the length of _ux_lang_id_fw */
  *Length = strlen((const char *)_ux_lang_id_fw);

  return _ux_lang_id_fw;
}

uint16_t USBD_Get_Interface_Number(uint8_t class_type, uint8_t interface_type)
{
  uint8_t itf_num = 0U;
  uint8_t idx = 0U;

  for(idx = 0; idx < USBD_MAX_SUPPORTED_CLASS; idx++)
  {
    if ((_ux_dev_fs.tclasslist[idx].ClassType == class_type) && (_ux_dev_fs.tclasslist[idx].InterfaceType == interface_type))
    {
      itf_num = _ux_dev_fs.tclasslist[idx].Ifs[0];
    }
  }
  return itf_num;
}

uint16_t USBD_Get_Configuration_Number(uint8_t class_type, uint8_t interface_type)
{
  uint8_t cfg_num = 1U;
  return cfg_num;
}

/*-->> Private Functions <<---------------------------------------------------*/

static void USBD_Desc_GetString(uint8_t *desc, uint8_t *unicode, uint16_t *len)
{
  uint8_t idx = 0U;
  uint8_t *pdesc;

  if (desc == NULL)
  {
    return;
  }

  pdesc = desc;
  *len = (uint16_t)USBD_Desc_GetLen(pdesc);

  unicode[idx++] = *(uint8_t *)len;

  while (*pdesc != (uint8_t)'\0')
  {
    unicode[idx++] = *pdesc;
    pdesc++;
  }
}

static uint8_t USBD_Desc_GetLen(uint8_t *buf)
{
  uint8_t  len = 0U;
  uint8_t *pbuff = buf;

  while (*pbuff != (uint8_t)'\0')
  {
    len++;
    pbuff++;
  }

  return len;
}

static uint8_t *USBD_Device_Framework_Builder(USBD_DevClassHandleTypeDef *pdev, uint8_t *pDevFrameWorkDesc, uint8_t *UserClassInstance, uint8_t Speed)
{
  static USBD_DeviceDescTypedef   *pDevDesc;
  static USBD_DevQualiDescTypedef *pDevQualDesc;
  uint8_t Idx_Instance = 0U;

  /* Set Dev and conf descriptors size to 0 */
  pdev->CurrConfDescSz = 0U;
  pdev->CurrDevDescSz  = 0U;

  /* Set the pointer to the device descriptor area*/
  pDevDesc = (USBD_DeviceDescTypedef *)pDevFrameWorkDesc;

  /* Start building the generic device descriptor common part */
  pDevDesc->bLength           = (uint8_t)sizeof(USBD_DeviceDescTypedef);
  pDevDesc->bDescriptorType   = UX_DEVICE_DESCRIPTOR_ITEM;
  pDevDesc->bcdUSB            = USB_BCDUSB;
  pDevDesc->bDeviceClass      = 0x00;
  pDevDesc->bDeviceSubClass   = 0x00;
  pDevDesc->bDeviceProtocol   = 0x00;
  pDevDesc->bMaxPacketSize    = USBD_MAX_EP0_SIZE;
  pDevDesc->idVendor          = USBD_VID;
  pDevDesc->idProduct         = USBD_PID;
  pDevDesc->bcdDevice         = 0x0200;
  pDevDesc->iManufacturer     = USBD_IDX_MFC_STR;
  pDevDesc->iProduct          = USBD_IDX_PRODUCT_STR;
  pDevDesc->iSerialNumber     = USBD_IDX_SERIAL_STR;
  pDevDesc->bNumConfigurations= USBD_MAX_NUM_CONFIGURATION;
  pdev->CurrDevDescSz += (uint32_t)sizeof(USBD_DeviceDescTypedef);

  /* Check if USBx is in high speed mode to add qualifier descriptor */
  if (Speed == USBD_HIGH_SPEED)
  {
    pDevQualDesc = (USBD_DevQualiDescTypedef *)(pDevFrameWorkDesc + pdev->CurrDevDescSz);
    pDevQualDesc->bLength           = (uint8_t)sizeof(USBD_DevQualiDescTypedef);
    pDevQualDesc->bDescriptorType   = UX_DEVICE_QUALIFIER_DESCRIPTOR_ITEM;
    pDevQualDesc->bcdDevice         = 0x0200;
    pDevQualDesc->Class             = 0x00;
    pDevQualDesc->SubClass          = 0x00;
    pDevQualDesc->Protocol          = 0x00;
    pDevQualDesc->bMaxPacketSize    = 0x40;
    pDevQualDesc->bNumConfigurations= 0x01;
    pDevQualDesc->bReserved         = 0x00;
    pdev->CurrDevDescSz += (uint32_t)sizeof(USBD_DevQualiDescTypedef);
  }

  /* Build the device framework */
  while (Idx_Instance < USBD_MAX_SUPPORTED_CLASS)
  {
    if ((pdev->classId < USBD_MAX_SUPPORTED_CLASS) && (pdev->NumClasses < USBD_MAX_SUPPORTED_CLASS) && (UserClassInstance[Idx_Instance] != CLASS_TYPE_NONE))
    {
      /* Call the composite class builder */
      (void)USBD_FrameWork_AddClass(pdev, (USBD_CompositeClassTypeDef)UserClassInstance[Idx_Instance], 0, Speed, (pDevFrameWorkDesc + pdev->CurrDevDescSz));

      /* Increment the ClassId for the next occurrence */
      pdev->classId ++;
      pdev->NumClasses ++;
    }

    Idx_Instance++;
  }

  /* Check if there is a composite class and update device class */
  if (pdev->NumClasses > 1)
  {
    pDevDesc->bDeviceClass    = 0xEF;
    pDevDesc->bDeviceSubClass = 0x02;
    pDevDesc->bDeviceProtocol = 0x01;
  }
  else
  {
    /* Check if the CDC ACM class is set and update device class */
    if (UserClassInstance[0] == CLASS_TYPE_CDC_ACM)
    {
      pDevDesc->bDeviceClass    = 0x02;
      pDevDesc->bDeviceSubClass = 0x02;
      pDevDesc->bDeviceProtocol = 0x00;
    }
  }

  return pDevFrameWorkDesc;
}

static uint8_t  USBD_FrameWork_AddToConfDesc(USBD_DevClassHandleTypeDef *pdev, uint8_t Speed, uint8_t *pCmpstConfDesc)
{
  uint8_t interface = 0U;

  /* The USB drivers do not set the speed value, so set it here before starting */
  pdev->Speed = Speed;

  /* start building the config descriptor common part */
  if (pdev->classId == 0U)
  {
    /* Add configuration and IAD descriptors */
    USBD_FrameWork_AddConfDesc((uint32_t)pCmpstConfDesc, &pdev->CurrConfDescSz);
  }

  switch (pdev->tclasslist[pdev->classId].ClassType)
  {

#if USBD_CDC_CLASS_ACTIVATED == 1U

    case CLASS_TYPE_CDC_ACM:

      /* Find the first available interface slot and Assign number of interfaces */
      interface = USBD_FrameWork_FindFreeIFNbr(pdev);
      pdev->tclasslist[pdev->classId].NumIf  = 2U;
      pdev->tclasslist[pdev->classId].Ifs[0] = interface;
      pdev->tclasslist[pdev->classId].Ifs[1] = (uint8_t)(interface + 1U);

      /* Assign endpoint numbers */
      pdev->tclasslist[pdev->classId].NumEps = 3U;  /* EP_IN, EP_OUT, CMD_EP */

      /* Check the current speed to assign endpoints */
      if (Speed == USBD_HIGH_SPEED)
      {
        /* Assign OUT Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_CDC_EPOUT_ADDR, USBD_EP_TYPE_BULK, USBD_CDC_EPOUT_HS_MPS);

        /* Assign IN Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_CDC_EPIN_ADDR, USBD_EP_TYPE_BULK, USBD_CDC_EPIN_HS_MPS);

        /* Assign CMD Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_CDC_EPINCMD_ADDR, USBD_EP_TYPE_INTR, USBD_CDC_EPINCMD_HS_MPS);
      }
      else
      {
        /* Assign OUT Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_CDC_EPOUT_ADDR, USBD_EP_TYPE_BULK, USBD_CDC_EPOUT_FS_MPS);

        /* Assign IN Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_CDC_EPIN_ADDR, USBD_EP_TYPE_BULK, USBD_CDC_EPIN_FS_MPS);

        /* Assign CMD Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_CDC_EPINCMD_ADDR, USBD_EP_TYPE_INTR, USBD_CDC_EPINCMD_FS_MPS);
      }

      /* Configure and Append the Descriptor */
      USBD_FrameWork_CDCDesc(pdev, (uint32_t)pCmpstConfDesc, &pdev->CurrConfDescSz);

      break;

#endif /* USBD_CDC_ACM_CLASS_ACTIVATED */

#if USBD_UVC_CLASS_ACTIVATED == 1U

    case CLASS_TYPE_UVC:

      /* Find the first available interface slot and Assign number of interfaces */
      interface = USBD_FrameWork_FindFreeIFNbr(pdev);
      pdev->tclasslist[pdev->classId].NumIf  = 2U;
      pdev->tclasslist[pdev->classId].Ifs[0] = interface;
      pdev->tclasslist[pdev->classId].Ifs[1] = (uint8_t)(interface + 1U);

      /* Assign endpoint numbers */
      pdev->tclasslist[pdev->classId].NumEps = 1U; /* EP_IN */

      /* Check the current speed to assign endpoint IN */
      if (pdev->Speed == USBD_HIGH_SPEED)
      {
        /* Assign IN Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_UVC_EPIN_ADDR, USBD_EP_TYPE_ISOC, USBD_UVC_EPIN_HS_MPS);
      }
      else
      {
        /* Assign IN Endpoint */
        USBD_FrameWork_AssignEp(pdev, USBD_UVC_EPIN_ADDR, USBD_EP_TYPE_ISOC, USBD_UVC_EPIN_FS_MPS);
      }

      /* Configure and Append the Descriptor */
      USBD_FrameWork_UVCDesc(pdev, (uint32_t)pCmpstConfDesc, &pdev->CurrConfDescSz);

      break;

#endif /* USBD_UVC_CLASS_ACTIVATED */

    default:
      break;
  }

  return UX_SUCCESS;
}

static uint8_t  USBD_FrameWork_AddClass(USBD_DevClassHandleTypeDef *pdev, USBD_CompositeClassTypeDef class, uint8_t cfgidx, uint8_t Speed, uint8_t *pCmpstConfDesc)
{
  if ((pdev->classId < USBD_MAX_SUPPORTED_CLASS) && (pdev->tclasslist[pdev->classId].Active == 0U))
  {
    /* Store the class parameters in the global tab */
    pdev->tclasslist[pdev->classId].ClassId   = pdev->classId;
    pdev->tclasslist[pdev->classId].Active    = 1U;
    pdev->tclasslist[pdev->classId].ClassType = class;

    /* Call configuration descriptor builder and endpoint configuration builder */
    if (USBD_FrameWork_AddToConfDesc(pdev, Speed, pCmpstConfDesc) != UX_SUCCESS)
    {
      return UX_ERROR;
    }
  }

  UNUSED(cfgidx);

  return UX_SUCCESS;
}

static uint8_t USBD_FrameWork_FindFreeIFNbr(USBD_DevClassHandleTypeDef *pdev)
{
  uint32_t idx = 0U;

  /* Unroll all already activated classes */
  for (uint32_t i = 0U; i < pdev->NumClasses; i++)
  {
    /* Unroll each class interfaces */
    for (uint32_t j = 0U; j < pdev->tclasslist[i].NumIf; j++)
    {
      /* Increment the interface counter index */
      idx++;
    }
  }

  /* Return the first available interface slot */
  return (uint8_t)idx;
}

static void  USBD_FrameWork_AddConfDesc(uint32_t Conf, uint32_t *pSze)
{
  /* Intermediate variable to comply with MISRA-C Rule 11.3 */
  USBD_ConfigDescTypedef *ptr = (USBD_ConfigDescTypedef *)Conf;

  ptr->bLength            = (uint8_t)sizeof(USBD_ConfigDescTypedef);
  ptr->bDescriptorType    = USB_DESC_TYPE_CONFIGURATION;
  ptr->wDescriptorLength  = 0U;
  ptr->bNumInterfaces     = 0U;
  ptr->bConfigurationValue= 1U;
  ptr->iConfiguration     = USBD_CONFIG_STR_DESC_IDX;
  ptr->bmAttributes       = USBD_CONFIG_BMATTRIBUTES;
  ptr->bMaxPower          = USBD_CONFIG_MAXPOWER;
  *pSze += sizeof(USBD_ConfigDescTypedef);
}

static void  USBD_FrameWork_AssignEp(USBD_DevClassHandleTypeDef *pdev, uint8_t Add, uint8_t Type, uint32_t Sze)
{
  uint32_t idx = 0U;

  /* Find the first available endpoint slot */
  while (((idx < (pdev->tclasslist[pdev->classId]).NumEps) && ((pdev->tclasslist[pdev->classId].Eps[idx].is_used) != 0U)))
  {
    /* Increment the index */
    idx++;
  }

  /* Configure the endpoint */
  pdev->tclasslist[pdev->classId].Eps[idx].add  = Add;
  pdev->tclasslist[pdev->classId].Eps[idx].type = Type;
  pdev->tclasslist[pdev->classId].Eps[idx].size = (uint16_t) Sze;
  pdev->tclasslist[pdev->classId].Eps[idx].is_used = 1U;
}

#if USBD_CDC_CLASS_ACTIVATED == 1U

  static void USBD_FrameWork_CDCDesc(USBD_DevClassHandleTypeDef *pdev, uint32_t pConf, uint32_t *Sze)
  {
    USBD_IfDescTypedef    *pIfDesc;
    USBD_EpDescTypedef    *pEpDesc;

  #if USBD_COMPOSITE_USE_IAD == 1
    USBD_IadDescTypedef   *pIadDesc;
  #endif /* USBD_COMPOSITE_USE_IAD */

    USBD_CDCHeaderFuncDescTypedef   *pHeadDesc;
    USBD_CDCCallMgmFuncDescTypedef  *pCallMgmDesc;
    USBD_CDCACMFuncDescTypedef      *pACMDesc;
    USBD_CDCUnionFuncDescTypedef    *pUnionDesc;

  #if USBD_COMPOSITE_USE_IAD == 1
    pIadDesc = ((USBD_IadDescTypedef *)((uint32_t)pConf + *Sze));
    pIadDesc->bLength               = (uint8_t)sizeof(USBD_IadDescTypedef);
    pIadDesc->bDescriptorType       = USB_DESC_TYPE_IAD;                      /* IAD descriptor */
    pIadDesc->bFirstInterface       = pdev->tclasslist[pdev->classId].Ifs[0];
    pIadDesc->bInterfaceCount       = 2U;                                     /* 2 interfaces */
    pIadDesc->bFunctionClass        = 0x02U;
    pIadDesc->bFunctionSubClass     = 0x02U;
    pIadDesc->bFunctionProtocol     = 0x01U;
    pIadDesc->iFunction             = 0x00U;                                  /* String Index */
    *Sze += (uint32_t)sizeof(USBD_IadDescTypedef);
  #endif /* USBD_COMPOSITE_USE_IAD */

    /* Control Interface Descriptor */
    __USBD_FRAMEWORK_SET_IF(
      pdev->tclasslist[pdev->classId].Ifs[0], 0U, 1U,
      0x02U,
      0x02U,
      0x01U,
      0x00U
    );

    /* Header Functional Descriptor*/
    pHeadDesc = ((USBD_CDCHeaderFuncDescTypedef *)((uint32_t)pConf + *Sze));
    pHeadDesc->bLength              = 0x05U;
    pHeadDesc->bDescriptorType      = 0x24U;
    pHeadDesc->bDescriptorSubtype   = 0x00U;
    pHeadDesc->bcdCDC               = 0x0110U;
    *Sze += (uint32_t)sizeof(USBD_CDCHeaderFuncDescTypedef);

    /* Call Management Functional Descriptor*/
    pCallMgmDesc = ((USBD_CDCCallMgmFuncDescTypedef *)((uint32_t)pConf + *Sze));
    pCallMgmDesc->bLength           = 0x05U;
    pCallMgmDesc->bDescriptorType   = 0x24U;
    pCallMgmDesc->bDescriptorSubtype= 0x01U;
    pCallMgmDesc->bmCapabilities    = 0x00U;
    pCallMgmDesc->bDataInterface    = pdev->tclasslist[pdev->classId].Ifs[1];
    *Sze += (uint32_t)sizeof(USBD_CDCCallMgmFuncDescTypedef);

    /* ACM Functional Descriptor*/
    pACMDesc = ((USBD_CDCACMFuncDescTypedef *)((uint32_t)pConf + *Sze));
    pACMDesc->bLength               = 0x04U;
    pACMDesc->bDescriptorType       = 0x24U;
    pACMDesc->bDescriptorSubtype    = 0x02U;
    pACMDesc->bmCapabilities        = 0x02U;
    *Sze += (uint32_t)sizeof(USBD_CDCACMFuncDescTypedef);

    /* Union Functional Descriptor*/
    pUnionDesc = ((USBD_CDCUnionFuncDescTypedef *)((uint32_t)pConf + *Sze));
    pUnionDesc->bLength             = 0x05U;
    pUnionDesc->bDescriptorType     = 0x24U;
    pUnionDesc->bDescriptorSubtype  = 0x06U;
    pUnionDesc->bMasterInterface    = pdev->tclasslist[pdev->classId].Ifs[0];
    pUnionDesc->bSlaveInterface     = pdev->tclasslist[pdev->classId].Ifs[1];
    *Sze += (uint32_t)sizeof(USBD_CDCUnionFuncDescTypedef);

    /* Append Endpoint descriptor to Configuration descriptor */
    __USBD_FRAMEWORK_SET_EP(
      pdev->tclasslist[pdev->classId].Eps[2].add,
      USBD_EP_TYPE_INTR,
      (uint16_t)pdev->tclasslist[pdev->classId].Eps[2].size,
      USBD_CDC_EPINCMD_HS_BINTERVAL,
      USBD_CDC_EPINCMD_FS_BINTERVAL
    );

    /* Data Interface Descriptor */
    __USBD_FRAMEWORK_SET_IF(
      pdev->tclasslist[pdev->classId].Ifs[1],
      0x00U,
      0x02U,
      0x0AU,
      0x00U,
      0x00U,
      0x00U
    );

    /* Append Endpoint descriptor to Configuration descriptor */
    __USBD_FRAMEWORK_SET_EP(
      pdev->tclasslist[pdev->classId].Eps[0].add,
      USBD_EP_TYPE_BULK,
      (uint16_t)pdev->tclasslist[pdev->classId].Eps[0].size,
      0x00U,
      0x00U
    );

    /* Append Endpoint descriptor to Configuration descriptor */
    __USBD_FRAMEWORK_SET_EP(
      pdev->tclasslist[pdev->classId].Eps[1].add,
      USBD_EP_TYPE_BULK,
      (uint16_t)pdev->tclasslist[pdev->classId].Eps[1].size,
      0x00U,
      0x00U
    );

    /* Update Config Descriptor and IAD descriptor */
    ((USBD_ConfigDescTypedef *)pConf)->bNumInterfaces   += 2U;
    ((USBD_ConfigDescTypedef *)pConf)->wDescriptorLength = *Sze;
  }

#endif /* USBD_CDC_ACM_CLASS_ACTIVATED */

#if USBD_UVC_CLASS_ACTIVATED == 1U

  static void USBD_FrameWork_UVCDesc(USBD_DevClassHandleTypeDef *pdev, uint32_t pConf, uint32_t *Sze)
  {
    USBD_IfDescTypedef    *pIfDesc;
    USBD_EpDescTypedef    *pEpDesc;

  #if USBD_COMPOSITE_USE_IAD == 1
    USBD_IadDescTypedef   *pIadDesc;
  #endif /* USBD_COMPOSITE_USE_IAD */

    USBD_UVCControlDescTypeDef        *pCtrlDesc;
    USBD_UVCInputTerminalDescTypeDef  *pITDesc;
    USBD_UVCOutputTerminalDescTypeDef *pOTDesc;
    USBD_UVCHeaderDescTypeDef         *pHdrDesc;
    USBD_UVCFormatDescTypeDef         *pFmtDesc;
    USBD_UVCFrameDescTypeDef          *pFrmDesc;
    USBD_UVCColorDescTypeDef          *pColDesc;

  #if USBD_COMPOSITE_USE_IAD == 1
    /* Interface association descriptor */
    pIadDesc = ((USBD_IadDescTypedef *)((uint32_t)pConf + *Sze));
    pIadDesc->bLength               = (uint8_t)sizeof(USBD_IadDescTypedef);
    pIadDesc->bDescriptorType       = USB_DESC_TYPE_IAD;
    pIadDesc->bFirstInterface       = pdev->tclasslist[pdev->classId].Ifs[0];
    pIadDesc->bInterfaceCount       = 0x02U;
    pIadDesc->bFunctionClass        = UX_DEVICE_CLASS_VIDEO_CC_VIDEO;
    pIadDesc->bFunctionSubClass     = UX_DEVICE_CLASS_VIDEO_SC_INTERFACE_COLLECTION;
    pIadDesc->bFunctionProtocol     = UX_DEVICE_CLASS_VIDEO_PC_PROTOCOL_UNDEFINED;
    pIadDesc->iFunction             = 0U;
    *Sze += (uint32_t)sizeof(USBD_IadDescTypedef);
  #endif /* USBD_COMPOSITE_USE_IAD */

    /* Standard VC Interface descriptor (interface 0) */
    __USBD_FRAMEWORK_SET_IF(
      pdev->tclasslist[pdev->classId].Ifs[0],
      0x00U,
      0x00U,
      UX_DEVICE_CLASS_VIDEO_CC_VIDEO,
      UX_DEVICE_CLASS_VIDEO_SC_CONTROL,
      UX_DEVICE_CLASS_VIDEO_PC_PROTOCOL_UNDEFINED,
      0x00U
    );

    /* Class-specific VC Interface Descriptor */
    pCtrlDesc = ((USBD_UVCControlDescTypeDef *)((uint32_t)pConf + *Sze));
    pCtrlDesc->bLength              = (uint8_t)sizeof(USBD_UVCControlDescTypeDef);
    pCtrlDesc->bDescriptorType      = UX_DEVICE_CLASS_VIDEO_CS_INTERFACE;
    pCtrlDesc->bDescriptorSubtype   = 0x01U;
    pCtrlDesc->bcdUVC               = USBD_UVC_VERSION;
    pCtrlDesc->wTotalLength         = USBD_UVC_FRAME_DESC_SIZE;
    pCtrlDesc->dwClockFrequency     = HSE_VALUE;
    pCtrlDesc->bInCollection        = 0x01U;
    pCtrlDesc->aInterfaceNr[0]      = 0x01U;
    *Sze += (uint32_t)sizeof(USBD_UVCControlDescTypeDef);

    /* Input Terminal Descriptor */
    pITDesc = ((USBD_UVCInputTerminalDescTypeDef *)((uint32_t)pConf + *Sze));
    pITDesc->bLength                  = (uint8_t)sizeof(USBD_UVCInputTerminalDescTypeDef);
    pITDesc->bDescriptorType          = UX_DEVICE_CLASS_VIDEO_CS_INTERFACE;
    pITDesc->bDescriptorSubtype       = 0x02U;          /* Input terminal */
    pITDesc->bTerminalID              = 0x01U;
    pITDesc->wTerminalType            = 0x0201U;        /* TT Camera */
    pITDesc->bAssocTerminal           = 0x00U;
    pITDesc->iTerminal                = 0x00U;
    pITDesc->wObjectiveFocalLengthMin = 0x00U;
    pITDesc->wObjectiveFocalLengthMax = 0x00U;
    pITDesc->wOcularFocalLength       = 0x00U;
    pITDesc->bControlSize             = 0x03U;
    for (uint8_t idx = 0; idx < 3U; idx++)
    {
      pITDesc->bmControls[idx] = 0x00U;
    }
    *Sze += (uint32_t)sizeof(USBD_UVCInputTerminalDescTypeDef);

    /* Output Terminal Descriptor */
    pOTDesc = ((USBD_UVCOutputTerminalDescTypeDef *)((uint32_t)pConf + *Sze));
    pOTDesc->bLength                = (uint8_t)sizeof(USBD_UVCOutputTerminalDescTypeDef);
    pOTDesc->bDescriptorType        = UX_DEVICE_CLASS_VIDEO_CS_INTERFACE;
    pOTDesc->bDescriptorSubtype     = 0x03U;        /* Output terminal */
    pOTDesc->bTerminalID            = 0x02U;
    pOTDesc->wTerminalType          = 0x0101U;      /* TT Streaming */
    pOTDesc->bAssocTerminal         = 0x00U;
    pOTDesc->bSourceID              = 0x01U;
    pOTDesc->iTerminal              = 0x00U;
    *Sze += (uint32_t)sizeof(USBD_UVCOutputTerminalDescTypeDef);

    /* Standard VC Interface descriptor.
     * interface 1, alternate 0 => zero bandwidth */
    __USBD_FRAMEWORK_SET_IF(
      pdev->tclasslist[pdev->classId].Ifs[1],
      0x00U,
      0x00U,
      UX_DEVICE_CLASS_VIDEO_CC_VIDEO,
      UX_DEVICE_CLASS_VIDEO_SC_STREAMING,
      UX_DEVICE_CLASS_VIDEO_PC_PROTOCOL_UNDEFINED,
      0x00U
    );

    /* Class-specific VS Header Descriptor (Input) */
    pHdrDesc = ((USBD_UVCHeaderDescTypeDef *)((uint32_t)pConf + *Sze));
    pHdrDesc->bLength               = (uint8_t)sizeof(USBD_UVCHeaderDescTypeDef);
    pHdrDesc->bDescriptorType       = UX_DEVICE_CLASS_VIDEO_CS_INTERFACE;
    pHdrDesc->bDescriptorSubtype    = UX_DEVICE_CLASS_VIDEO_VC_HEADER;
    pHdrDesc->bNumFormats           = 0x01U;
    pHdrDesc->wTotalLength          = USBD_UVC_VS_HEADER_SIZE;
    pHdrDesc->bEndpointAddress      = USBD_UVC_EPIN_ADDR;
    pHdrDesc->bmInfo                = 0x00U;
    pHdrDesc->bTerminalLink         = 0x02U;
    pHdrDesc->bStillCaptureMethod   = 0x00U;
    pHdrDesc->bTriggerSupport       = 0x00U;
    pHdrDesc->bTriggerUsage         = 0x00U;
    pHdrDesc->bControlSize          = 0x01U;
    pHdrDesc->bmaControls           = 0x00U;
    *Sze += (uint32_t)sizeof(USBD_UVCHeaderDescTypeDef);

    /* Payload Format Descriptor */
    pFmtDesc = ((USBD_UVCFormatDescTypeDef *)((uint32_t)pConf + *Sze));
    pFmtDesc->bLength               = (uint8_t)sizeof(USBD_UVCFormatDescTypeDef);
    pFmtDesc->bDescriptorType       = UX_DEVICE_CLASS_VIDEO_CS_INTERFACE;
    pFmtDesc->bDescriptorSubType    = UX_DEVICE_CLASS_VIDEO_VS_FORMAT_FRAME_BASED;
    pFmtDesc->bFormatIndex          = 0x01U;
    pFmtDesc->bNumFrameDescriptors  = 0x01U;
    for (uint8_t idx = 0; idx < USBD_GUID_SIZE; idx++)
    {
      pFmtDesc->bGuidFormat[idx]    = _ux_uvc_guid[idx];
    }
    pFmtDesc->bBitsPerPixel         = 0x00U;
    pFmtDesc->bDefaultFrameIndex    = 0x01U;
    pFmtDesc->bAspectRatioX         = 0x00U;
    pFmtDesc->bAspectRatioY         = 0x00U;
    pFmtDesc->bmInterlaceFlag       = 0x00U;
    pFmtDesc->bCopyProtect          = 0x00U;
    pFmtDesc->bVariableSize         = 0x01U;
    *Sze += (uint32_t)sizeof(USBD_UVCFormatDescTypeDef);

    /* Class-specific VS Frame Descriptor */
    pFrmDesc = ((USBD_UVCFrameDescTypeDef *)((uint32_t)pConf + *Sze));
    pFrmDesc->bLength               = (uint8_t)sizeof(USBD_UVCFrameDescTypeDef);
    pFrmDesc->bDescriptorType       = UX_DEVICE_CLASS_VIDEO_CS_INTERFACE;
    pFrmDesc->bDescriptorSubType    = UX_DEVICE_CLASS_VIDEO_VS_FRAME_FRAME_BASED;
    pFrmDesc->bFrameIndex           = 0x01U;
    pFrmDesc->bmCapabilities        = 0x02U;    /* Fixed frame rate */
    pFrmDesc->wWidth                = camera.main.width;
    pFrmDesc->wHeight               = camera.main.height;
    /* This section is fixed for 30FPS (independent of FS/HS) */
    pFrmDesc->dwMinBitRate          = camera.main.width * camera.main.height * camera.sensor.fps;
    pFrmDesc->dwMaxBitRate          = pFrmDesc->dwMinBitRate;
    pFrmDesc->bFrameIntervalType    = 0x01U;
    pFrmDesc->dwDefaultFrameInterval= UX_UVC_INTERVAL(camera.sensor.fps);
    pFrmDesc->dwBytesPerLine        = 0x00U;
    pFrmDesc->dwFrameInterval[0]    = pFrmDesc->dwDefaultFrameInterval;
    /* Continue normally */
    *Sze += (uint32_t)sizeof(USBD_UVCFrameDescTypeDef);

    /* Color Matching Descriptor */
    pColDesc = ((USBD_UVCColorDescTypeDef *)((uint32_t)pConf + *Sze));
    pColDesc->bLength                 = (uint8_t)sizeof(USBD_UVCColorDescTypeDef);
    pColDesc->bDescriptorType         = UX_DEVICE_CLASS_VIDEO_CS_INTERFACE;
    pColDesc->bDescriptorSubType      = UX_DEVICE_CLASS_VIDEO_VS_COLORFORMAT;
    pColDesc->bColorPrimarie          = 0x01U;          /* BT.709, sRGB */
    pColDesc->bTransferCharacteristics= 0x01U;          /* BT.709 */
    pColDesc->bMatrixCoefficients     = 0x04U;          /* BT.601 */
    *Sze += (uint32_t)sizeof(USBD_UVCColorDescTypeDef);

    /* Standard VS Interface descriptor
     * interface 1, alternate 1 => data transfer */
    __USBD_FRAMEWORK_SET_IF(
      pdev->tclasslist[pdev->classId].Ifs[1],
      0x01U,
      0x01U,
      UX_DEVICE_CLASS_VIDEO_CC_VIDEO,
      UX_DEVICE_CLASS_VIDEO_SC_STREAMING,
      UX_DEVICE_CLASS_VIDEO_PC_PROTOCOL_UNDEFINED,
      0x00U
    );

    /* Standard VS data Endpoint */
    __USBD_FRAMEWORK_SET_EP(
      pdev->tclasslist[pdev->classId].Eps[0].add,
      (USBD_EP_TYPE_ISOC | USBD_EP_ATTR_ISOC_ASYNC),
      (uint16_t)(pdev->tclasslist[pdev->classId].Eps[0].size),
      USBD_UVC_EPIN_HS_BINTERVAL,
      USBD_UVC_EPIN_FS_BINTERVAL
    );

    /* Update Config Descriptor and IAD descriptor */
    ((USBD_ConfigDescTypedef *)pConf)->bNumInterfaces   += 2U;
    ((USBD_ConfigDescTypedef *)pConf)->wDescriptorLength = *Sze;
  }

#endif /* USBD_UVC_CLASS_ACTIVATED */
