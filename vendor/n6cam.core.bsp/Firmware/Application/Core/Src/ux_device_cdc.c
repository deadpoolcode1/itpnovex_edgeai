/**
 ******************************************************************************
 * @file    ux_device_cdc.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   USBX CDC device application definition
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
#include "slib32_tick.h"
#include "tx_app.h"
#include "ux_device_cdc.h"
#include "ux_stm32_config.h"

#if USBD_CDC_CLASS_ACTIVATED == 1U
/*-->> Private Tunables <<----------------------------------------------------*/

/*-->> Private Definitions <<-------------------------------------------------*/

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/*-->> Private Data <<--------------------------------------------------------*/

static UX_SLAVE_CLASS_CDC_ACM *_cdc_ctx = UX_NULL;    /*!< CDC instance (current) */
static TX_MUTEX               _cdc_mtx_rx;
static TX_MUTEX               _cdc_mtx_tx;

/** @brief CDC VCOM configurations */
static UX_SLAVE_CLASS_CDC_ACM_LINE_CODING_PARAMETER _cdc_vcom_config = {
  115200U,  /* Baudrate : 115200  */
    0x00U,  /* Stop bits: 1       */
    0x00U,  /* Parity   : None    */
    0x08U,  /* Bits     : 8       */
};

/*-->> Private Functions <<---------------------------------------------------*/

static int32_t _cdc_read(uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _cdc_write(const uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _cdc_wait_available(uint32_t timeout);

/*-->> Public API <<----------------------------------------------------------*/

int32_t ux_cdc_init(void)
{
  int32_t status;

  /* Configure RTOS */
  status = tx_mutex_create(&_cdc_mtx_rx, "ux.mtx.cdc.rx", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_mutex_create(&_cdc_mtx_tx, "ux.mtx.cdc.tx", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  return UX_SUCCESS;
}

t_stream *ux_cdc_get_stream(void)
{
  static t_stream _cdc_stream = { 0 };
  if (!_cdc_stream.init)
  {
    stream_init(&_cdc_stream, _cdc_read, _cdc_write);
  }
  return &_cdc_stream;
}

VOID ux_cdc_activate(void *instance)
{
  ULONG status;

  /* Store instance */
  _cdc_ctx = (UX_SLAVE_CLASS_CDC_ACM*)instance;

  /* Configure VCOM */
  status = ux_device_class_cdc_acm_ioctl(_cdc_ctx, UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_LINE_CODING, &_cdc_vcom_config);
  if (status != UX_SUCCESS)
  {
    Error_Handler();
  }
  return;
}

void ux_cdc_deactivate(void *instance)
{
  UX_PARAMETER_NOT_USED(instance);

  /* Reset instance */
  _cdc_ctx = UX_NULL;

  return;
}

void ux_cdc_parameter_change(void *instance)
{
  /* USER CODE BEGIN USBD_CDC_ACM_ParameterChange */
  UX_PARAMETER_NOT_USED(instance);

  ULONG             id;
  ULONG             status;
  UX_SLAVE_DEVICE   *device;
  UX_SLAVE_TRANSFER *request;

  /* Get device pointer and request */
  device  = &_ux_system_slave->ux_system_slave_device;
  request = &device->ux_slave_device_control_endpoint.ux_slave_endpoint_transfer_request;

  /* Handle request by ID */
  id = *(request->ux_slave_transfer_request_setup + UX_SETUP_REQUEST);
  switch (id)
  {
    case UX_SLAVE_CLASS_CDC_ACM_SET_LINE_CODING:
      status = ux_device_class_cdc_acm_ioctl(_cdc_ctx, UX_SLAVE_CLASS_CDC_ACM_IOCTL_GET_LINE_CODING, &_cdc_vcom_config);
      if (status != UX_SUCCESS)
      {
        Error_Handler();
      }
      break;

    case UX_SLAVE_CLASS_CDC_ACM_GET_LINE_CODING:
      status = ux_device_class_cdc_acm_ioctl(_cdc_ctx, UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_LINE_CODING, &_cdc_vcom_config);
      if (status != UX_SUCCESS)
      {
        Error_Handler();
      }
      break;

    default:
      /* Not handling... yet! */
      break;
  }
  return;
}

/*-->> Private Functions <<---------------------------------------------------*/

static int32_t _cdc_read(uint8_t *buff, size_t size, uint32_t timeout)
{
  int32_t recv;
  int32_t status;

  /* Wait until connected */
  status = _cdc_wait_available(timeout);
  if (status != UX_SUCCESS)
  {
    return 0;
  }

  /* Acquire */
  rtos_mutex_acquire(&_cdc_mtx_rx, true);

  /* Configure timeout */
  ux_device_class_cdc_acm_ioctl(_cdc_ctx, UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_READ_TIMEOUT, (VOID*)timeout);

#if !defined(UX_DEVICE_CLASS_CDC_ACM_TRANSMISSION_DISABLE)
  /* Set transmission_status to UX_FALSE for the first time */
  _cdc_ctx->ux_slave_class_cdc_acm_transmission_status = UX_FALSE;
#endif

  /* Handle RX */
  /* Receive data from VCOM */
  status = ux_device_class_cdc_acm_read(_cdc_ctx, (UCHAR*)buff, (ULONG)size, (ULONG*)&recv);
  if (
    (status == UX_CONFIGURATION_HANDLE_UNKNOWN) ||
    (status == UX_TRANSFER_BUS_RESET)           ||
    (status == TX_NO_INSTANCE)                      /* Introduced by IOCTL timeout */
  )
  {
    recv = 0;
  }
  else if (status != UX_SUCCESS)
  {
    /* Release and fail */
    rtos_mutex_acquire(&_cdc_mtx_rx, false);
    Error_Handler();
  }

  /* Release and return */
  rtos_mutex_acquire(&_cdc_mtx_rx, false);
  return recv;
}

static int32_t _cdc_write(const uint8_t *buff, size_t size, uint32_t timeout)
{
  int32_t sent;
  int32_t status;

  UNUSED(timeout);

  /* Wait until ready (fail fast) */
  status = _cdc_wait_available(0);
  if (status != UX_SUCCESS)
  {
    return 0;
  }

  /* Acquire */
  rtos_mutex_acquire(&_cdc_mtx_tx, true);

#if !defined(UX_DEVICE_CLASS_CDC_ACM_TRANSMISSION_DISABLE)
  /* Set transmission_status to UX_FALSE for the first time */
  _cdc_ctx->ux_slave_class_cdc_acm_transmission_status = UX_FALSE;
#endif

  /* Transmit chunk */
  status = ux_device_class_cdc_acm_write(_cdc_ctx, (UCHAR*)buff, (ULONG)size, (ULONG*)&sent);
  if ((status == UX_CONFIGURATION_HANDLE_UNKNOWN) || (status == UX_TRANSFER_NO_ANSWER))
  {
    sent = 0;
  }
  else if (status != UX_SUCCESS)
  {
    /* Release and fail */
    rtos_mutex_acquire(&_cdc_mtx_tx, false);
    Error_Handler();
  }

  /* Release and return */
  rtos_mutex_acquire(&_cdc_mtx_tx, false);
  return sent;
}

static int32_t _cdc_wait_available(uint32_t timeout)
{
  UX_SLAVE_DEVICE *device;
  uint32_t        start = tx_time_get();

  /* Wait connection */
  do
  {
    /* Check if connected */
    if (_cdc_ctx != NULL)
    {
      device = &_ux_system_slave->ux_system_slave_device;
      if (device->ux_slave_device_state == UX_DEVICE_CONFIGURED)
      {
        return UX_SUCCESS;
      }
    }
    /* Wait */
    tx_thread_sleep(1);
  } while (!tick_is_expired(start, timeout));

  /* Timeout */
  return UX_TIMEOUT;
}

/* -------------------------------------------------------------------------- */
#endif /* USBD_CDC_CLASS_ACTIVATED */
