/**
 ******************************************************************************
 * @file    ux_device_descriptors.h
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
#ifndef _UX_DEVICE_DESCRIPTORS_H_
#define _UX_DEVICE_DESCRIPTORS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "ux_api.h"
#include "ux_device_class_cdc_acm.h"
#include "ux_device_class_video.h"
#include "ux_stm32_config.h"

/*-->> Public Definitions <<--------------------------------------------------*/

#define USBD_MAX_NUM_CONFIGURATION              1U
#define USBD_MAX_SUPPORTED_CLASS                2U
#define USBD_MAX_CLASS_ENDPOINTS                9U
#define USBD_MAX_CLASS_INTERFACES               11U

#define USBD_CONFIG_BMATTRIBUTES                0x80U   /* bus-powered  */
#define USBD_CONFIG_MAXPOWER                    50U     /* 100 [mA]     */

#define USBD_COMPOSITE_USE_IAD                  1U
#define USBD_DEVICE_FRAMEWORK_BUILDER_ENABLED   1U

#define USBD_GUID_SIZE                          16U
#define USBD_FRAMEWORK_MAX_DESC_SZ              512U    /* Updated to 512 to support multiclass */

#define USBD_CDC_CLASS_ACTIVATED                1U
#define USBD_UVC_CLASS_ACTIVATED                1U

#define USBD_VID                                0x0483
#define USBD_PID                                0x5740  /*!< From IQTune */
#define USBD_LANGID_STRING                      1033
#define USBD_MANUFACTURER_STRING                "STMicroelectronics"
#define USBD_PRODUCT_STRING                     "N6Cam"
#define USBD_SERIAL_NUMBER                      "DEADBEEF"

#define USB_DESC_TYPE_INTERFACE                 0x04U
#define USB_DESC_TYPE_ENDPOINT                  0x05U
#define USB_DESC_TYPE_CONFIGURATION             0x02U
#define USB_DESC_TYPE_IAD                       0x0BU

#define USBD_EP_TYPE_CTRL                       0x00U
#define USBD_EP_TYPE_ISOC                       0x01U
#define USBD_EP_TYPE_BULK                       0x02U
#define USBD_EP_TYPE_INTR                       0x03U

#define USBD_EP_ATTR_ISOC_NOSYNC                0x00U
#define USBD_EP_ATTR_ISOC_ASYNC                 0x04U
#define USBD_EP_ATTR_ISOC_ADAPT                 0x08U
#define USBD_EP_ATTR_ISOC_SYNC                  0x0CU

#define USBD_FULL_SPEED                         0x00U
#define USBD_HIGH_SPEED                         0x01U

#define USB_BCDUSB                              0x0200U
#define LANGUAGE_ID_MAX_LENGTH                  2U

#define USBD_IDX_MFC_STR                        0x01U
#define USBD_IDX_PRODUCT_STR                    0x02U
#define USBD_IDX_SERIAL_STR                     0x03U

#define USBD_MAX_EP0_SIZE                       64U
#define USBD_DEVICE_QUALIFIER_DESC_SIZE         0x0AU

#define USBD_STRING_FRAMEWORK_MAX_LENGTH        256U

/* Device CDC-ACM Class */
#define USBD_CDC_EPINCMD_ADDR                   (IN + 1U)   /* 0x81U */
#define USBD_CDC_EPINCMD_FS_MPS                 8U
#define USBD_CDC_EPINCMD_HS_MPS                 8U
#define USBD_CDC_EPIN_ADDR                      (IN + 2U)   /* 0x82U */
#define USBD_CDC_EPIN_FS_MPS                    64U
#define USBD_CDC_EPIN_HS_MPS                    512U
#define USBD_CDC_EPOUT_ADDR                     (OUT + 3U)  /* 0x03U */
#define USBD_CDC_EPOUT_FS_MPS                   64U
#define USBD_CDC_EPOUT_HS_MPS                   512U
#define USBD_CDC_EPINCMD_FS_BINTERVAL           5U
#define USBD_CDC_EPINCMD_HS_BINTERVAL           5U

/* Device Video Class */
#define USBD_UVC_EPIN_ADDR                      (IN + 3U)   /* 0x83U */
#define USBD_UVC_EPIN_FS_MPS                    1023U
#define USBD_UVC_EPIN_HS_MPS                    1024U
#define USBD_UVC_EPIN_FS_BINTERVAL              1U
#define USBD_UVC_EPIN_HS_BINTERVAL              1U

#define USBD_UVC_VERSION                        0x0150U      /* UVC 1.5 */
#define USBD_UVC_FRAME_DESC_SIZE                0x28U        /* From UVC: 40 */

/* Giud Format: H264 {0000-0010-8000-00AA00389B71} */
#define UVC_GUID_H264 {               \
  'H', '2', '6', '4',                 \
  0x00, 0x00,                         \
  0x10, 0x00,                         \
  0x80, 0x00,                         \
  0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71, \
}

#ifndef USBD_CONFIG_STR_DESC_IDX
#define USBD_CONFIG_STR_DESC_IDX                0x00U
#endif /* USBD_CONFIG_STR_DESC_IDX */

#ifndef USBD_CONFIG_BMATTRIBUTES
#define USBD_CONFIG_BMATTRIBUTES                0xC0U
#endif /* USBD_CONFIG_BMATTRIBUTES */

/*-->> Public Types <<--------------------------------------------------------*/

/* Enum Class Type */
typedef enum
{
  CLASS_TYPE_NONE     = 0,
  CLASS_TYPE_HID      = 1,
  CLASS_TYPE_CDC_ACM  = 2,
  CLASS_TYPE_MSC      = 3,
  CLASS_TYPE_DFU      = 4,
  CLASS_TYPE_UVC      = 5,
} USBD_CompositeClassTypeDef;

/* USB Endpoint handle structure */
typedef struct
{
  uint32_t status;
  uint32_t total_length;
  uint32_t rem_length;
  uint32_t maxpacket;
  uint16_t is_used;
  uint16_t bInterval;
} USBD_EndpointTypeDef;

/* USB endpoint handle structure */
typedef struct
{
  uint8_t  add;
  uint8_t  type;
  uint16_t size;
  uint8_t  is_used;
} USBD_EPTypeDef;

/* USB Composite handle structure */
typedef struct
{
  USBD_CompositeClassTypeDef ClassType;
  uint32_t ClassId;
  uint8_t  InterfaceType;
  uint32_t Active;
  uint32_t NumEps;
  uint32_t NumIf;
  USBD_EPTypeDef Eps[USBD_MAX_CLASS_ENDPOINTS];
  uint8_t  Ifs[USBD_MAX_CLASS_INTERFACES];
} USBD_CompositeElementTypeDef;

/* USB Device handle structure */
typedef struct _USBD_DevClassHandleTypeDef
{
  uint8_t  Speed;
  uint32_t classId;
  uint32_t NumClasses;
  USBD_CompositeElementTypeDef tclasslist[USBD_MAX_SUPPORTED_CLASS];
  uint32_t CurrDevDescSz;
  uint32_t CurrConfDescSz;
} USBD_DevClassHandleTypeDef;

/* USB Device endpoint direction */
typedef enum
{
  OUT   = 0x00U,
  IN    = 0x80U,
} USBD_EPDirectionTypeDef;

/* USB Device descriptors structure */
typedef struct
{
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t bcdUSB;
  uint8_t  bDeviceClass;
  uint8_t  bDeviceSubClass;
  uint8_t  bDeviceProtocol;
  uint8_t  bMaxPacketSize;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t  iManufacturer;
  uint8_t  iProduct;
  uint8_t  iSerialNumber;
  uint8_t  bNumConfigurations;
} __PACKED USBD_DeviceDescTypedef;

/* USB Iad descriptors structure */
typedef struct
{
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint8_t  bFirstInterface;
  uint8_t  bInterfaceCount;
  uint8_t  bFunctionClass;
  uint8_t  bFunctionSubClass;
  uint8_t  bFunctionProtocol;
  uint8_t  iFunction;
} __PACKED USBD_IadDescTypedef;

/* USB interface descriptors structure */
typedef struct
{
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint8_t  bInterfaceNumber;
  uint8_t  bAlternateSetting;
  uint8_t  bNumEndpoints;
  uint8_t  bInterfaceClass;
  uint8_t  bInterfaceSubClass;
  uint8_t  bInterfaceProtocol;
  uint8_t  iInterface;
} __PACKED USBD_IfDescTypedef;

/* USB endpoint descriptors structure */
typedef struct
{
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint8_t  bEndpointAddress;
  uint8_t  bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t  bInterval;
} __PACKED USBD_EpDescTypedef;

/* USB Config descriptors structure */
typedef struct
{
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t wDescriptorLength;
  uint8_t  bNumInterfaces;
  uint8_t  bConfigurationValue;
  uint8_t  iConfiguration;
  uint8_t  bmAttributes;
  uint8_t  bMaxPower;
} __PACKED USBD_ConfigDescTypedef;

/* USB Qualifier descriptors structure */
typedef struct
{
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t bcdDevice;
  uint8_t  Class;
  uint8_t  SubClass;
  uint8_t  Protocol;
  uint8_t  bMaxPacketSize;
  uint8_t  bNumConfigurations;
  uint8_t  bReserved;
} __PACKED USBD_DevQualiDescTypedef;

#if USBD_CDC_CLASS_ACTIVATED == 1U

  /* ACM Header Functional Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint16_t bcdCDC;
  } __PACKED USBD_CDCHeaderFuncDescTypedef;

  /* ACM Call Management Functional Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bmCapabilities;
    uint8_t  bDataInterface;
  } __PACKED USBD_CDCCallMgmFuncDescTypedef;

  /* ACM Functional Descriptor*/
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bmCapabilities;
  } __PACKED USBD_CDCACMFuncDescTypedef;

  /* ACM Union Functional Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bMasterInterface;
    uint8_t  bSlaveInterface;
  } __PACKED USBD_CDCUnionFuncDescTypedef;

#endif /* USBD_CDC_CLASS_ACTIVATED */

#if USBD_UVC_CLASS_ACTIVATED == 1U

  /* UVC: Class-specific VC Interface Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint16_t bcdUVC;
    uint16_t wTotalLength;
    uint32_t dwClockFrequency;
    uint8_t  bInCollection;
    uint8_t  aInterfaceNr[1];
  } __PACKED USBD_UVCControlDescTypeDef;

  /* UVC: Input Terminal Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bTerminalID;
    uint16_t wTerminalType;
    uint8_t  bAssocTerminal;
    uint8_t  iTerminal;
    uint16_t wObjectiveFocalLengthMin;
    uint16_t wObjectiveFocalLengthMax;
    uint16_t wOcularFocalLength;
    uint8_t  bControlSize;
    uint8_t  bmControls[3];
  } __PACKED USBD_UVCInputTerminalDescTypeDef;

  /* UVC: Output Terminal Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bTerminalID;
    uint16_t wTerminalType;
    uint8_t  bAssocTerminal;
    uint8_t  bSourceID;
    uint8_t  iTerminal;
  } __PACKED USBD_UVCOutputTerminalDescTypeDef;

  /* UVC: Class-specific VS Header Descriptor (Input) */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bNumFormats;
    uint16_t wTotalLength;
    uint8_t  bEndpointAddress;
    uint8_t  bmInfo;
    uint8_t  bTerminalLink;
    uint8_t  bStillCaptureMethod;
    uint8_t  bTriggerSupport;
    uint8_t  bTriggerUsage;
    uint8_t  bControlSize;
    uint8_t  bmaControls;
  } __PACKED USBD_UVCHeaderDescTypeDef;

  /* UVC: Payload Format Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubType;
    uint8_t  bFormatIndex;
    uint8_t  bNumFrameDescriptors;
    uint8_t  bGuidFormat[USBD_GUID_SIZE];
    uint8_t  bBitsPerPixel;
    uint8_t  bDefaultFrameIndex;
    uint8_t  bAspectRatioX;
    uint8_t  bAspectRatioY;
    uint8_t  bmInterlaceFlag;
    uint8_t  bCopyProtect;
    uint8_t  bVariableSize;
  } __PACKED USBD_UVCFormatDescTypeDef;

  /* UVC: Class-specific VS Frame Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubType;
    uint8_t  bFrameIndex;
    uint8_t  bmCapabilities;
    uint16_t wWidth;
    uint16_t wHeight;
    uint32_t dwMinBitRate;
    uint32_t dwMaxBitRate;
    uint32_t dwDefaultFrameInterval;
    uint8_t  bFrameIntervalType;
    uint32_t dwBytesPerLine;
    uint32_t dwFrameInterval[1];
  } __PACKED USBD_UVCFrameDescTypeDef;

  /* UVC: Color Matching Descriptor */
  typedef struct
  {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubType;
    uint8_t  bColorPrimarie;
    uint8_t  bTransferCharacteristics;
    uint8_t  bMatrixCoefficients;
  } __PACKED USBD_UVCColorDescTypeDef;

#endif /* USBD_UVC_CLASS_ACTIVATED */

/*-->> Public Macros <<-------------------------------------------------------*/

#define USBD_UVC_VS_HEADER_SIZE       \
    sizeof(USBD_UVCHeaderDescTypeDef) \
  + sizeof(USBD_UVCFormatDescTypeDef) \
  + sizeof(USBD_UVCFrameDescTypeDef)  \
  + sizeof(USBD_UVCColorDescTypeDef)

#define __USBD_FRAMEWORK_SET_EP(epadd, eptype, epsize, HSinterval, FSinterval)  \
          do {                                                                  \
            /* Append Endpoint descriptor to Configuration descriptor */        \
            pEpDesc = ((USBD_EpDescTypedef*)((uint32_t)pConf + *Sze));          \
            pEpDesc->bLength            = (uint8_t)sizeof(USBD_EpDescTypedef);  \
            pEpDesc->bDescriptorType    = USB_DESC_TYPE_ENDPOINT;               \
            pEpDesc->bEndpointAddress   = (epadd);                              \
            pEpDesc->bmAttributes       = (eptype);                             \
            pEpDesc->wMaxPacketSize     = (epsize);                             \
            if(pdev->Speed == USBD_HIGH_SPEED)                                  \
            {                                                                   \
              pEpDesc->bInterval        = (HSinterval);                         \
            }                                                                   \
            else                                                                \
            {                                                                   \
              pEpDesc->bInterval        = (FSinterval);                         \
            }                                                                   \
            *Sze += (uint32_t)sizeof(USBD_EpDescTypedef);                       \
          } while(0)

#define __USBD_FRAMEWORK_SET_IF(ifnum, alt, eps, class, subclass, protocol, istring) \
          do {                                                                  \
            /* Interface Descriptor */                                          \
            pIfDesc = ((USBD_IfDescTypedef*)((uint32_t)pConf + *Sze));          \
            pIfDesc->bLength            = (uint8_t)sizeof(USBD_IfDescTypedef);  \
            pIfDesc->bDescriptorType    = USB_DESC_TYPE_INTERFACE;              \
            pIfDesc->bInterfaceNumber   = (ifnum);                              \
            pIfDesc->bAlternateSetting  = (alt);                                \
            pIfDesc->bNumEndpoints      = (eps);                                \
            pIfDesc->bInterfaceClass    = (class);                              \
            pIfDesc->bInterfaceSubClass = (subclass);                           \
            pIfDesc->bInterfaceProtocol = (protocol);                           \
            pIfDesc->iInterface         = (istring);                            \
            *Sze += (uint32_t)sizeof(USBD_IfDescTypedef);                       \
          } while(0)

/*-->> Public Data <<---------------------------------------------------------*/

/*-->> Public API <<----------------------------------------------------------*/

uint8_t *USBD_Get_Device_Framework_Speed(uint8_t Speed, ULONG *Length);
uint8_t *USBD_Get_String_Framework(ULONG *Length);
uint8_t *USBD_Get_Language_Id_Framework(ULONG *Length);
uint16_t USBD_Get_Interface_Number(uint8_t class_type, uint8_t interface_type);
uint16_t USBD_Get_Configuration_Number(uint8_t class_type, uint8_t interface_type);

/* -------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif
#endif  /* _UX_DEVICE_DESCRIPTORS_H_ */
