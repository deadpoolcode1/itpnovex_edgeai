/**
 ******************************************************************************
 * @file    nx_app.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   NetX application implementation
 ******************************************************************************
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
 ******************************************************************************
 */
#include "cyabs_rtos.h"
#include "cybsp.h"
#include "cyhal.h"
#include "flash.h"
#include "nx_app.h"
#include "nx_app_mdns.h"
#include "nx_app_rtsp_server.h"
#include "nx_app_sntp.h"
#include "nx_app_tcp_stream.h"
#include "nx_app_telnet.h"
#include "registry_task.h"
#include "whd.h"
#include "wifi_bt_if.h"

/*-->> Private Tunables <<----------------------------------------------------*/

/* Wifi management */
#define WIFI_STATUS_PULL      (5U * NX_IP_PERIODIC_RATE)    /* Each 5 seconds */
#define WIFI_RETRY_DELAY      (2U * NX_IP_PERIODIC_RATE)    /* Each 2 seconds */

/*-->> Private Definitions <<-------------------------------------------------*/

/* NetX events */
#define NX_EVT_ALL            0xFFFFFFFFU
#define NX_EVT_CONNECTED      BIT(0U)
#define NX_EVT_DISCONNECTED   BIT(1U)

/* WHD helpers */
#define CY_RSLT_ERROR         ((cy_rslt_t) - 1)

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/* NetX task handler */
typedef struct
{
  /* NetX management */
  bool                  init;
  t_nx_mode             mode;
  /* Thread handling */
  TX_EVENT_FLAGS_GROUP  evt;
  TX_THREAD             thread;
  uint8_t               stack[NX_STACK_SIZE];
} t_nx_task;

/*-->> Private Data <<--------------------------------------------------------*/

static t_nx_task                _nx_task = {
  .init = false,
  .mode = NX_MODE_OFF,
};

SD_HandleTypeDef                hsd1 = { };

static cy_wcm_connect_params_t  _nx_params = { };
static cy_wcm_ip_setting_t      _nx_ip_settings = { };

/*-->> Private Functions <<---------------------------------------------------*/

static void _nx_task_init(void);
static void _nx_task_run(uint32_t input);

static void _nx_registry_init(void);

static void _whd_event_callback(cy_wcm_event_t event, cy_wcm_event_data_t *data);
static void _whd_init_gpio(void);

extern void HAL_SD_MspInit(SD_HandleTypeDef* hsd);
extern void HAL_SD_MspDeInit(SD_HandleTypeDef* hsd);

/*-->> Public API <<----------------------------------------------------------*/

void nx_app_init(void)
{
#if defined(N6CAM_WIFI_MURATA)
  int32_t status;

  /* Create the main thread */
  status = tx_thread_create(
    &_nx_task.thread, "nx.task",
    _nx_task_run, 0,
    _nx_task.stack, NX_STACK_SIZE,
    NX_TASK_PRIO, NX_TASK_PRIO,
    NX_TASK_TIME_SLICE, TX_AUTO_START
  );
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
#endif /* N6CAM_WIFI_MURATA */
}

t_nx_mode nx_mode_get(void)
{
  return _nx_task.mode;
}

t_nx_mode nx_mode_get_stored(void)
{
  t_nx_mode       mode;
  t_registry_data *registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_NX, "Registry not available");
    Error_Handler();
  }

  /* Get mode */
  mode = (t_nx_mode)registry->wifi_mode;
  registry_release();
  return mode;
}

int32_t nx_mode_set(t_nx_mode mode)
{
  t_registry_data *registry;
  int32_t         status = -2;

  /* Validate */
  if (mode >= NX_MODE_UNKNOWN)
  {
    /* Invalid parameters */
    return -1;
  }

  /* Update registry */
  registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_NX, "Registry not available");
    Error_Handler();
  }

  /* Only if changed */
  if (mode != registry->wifi_mode)
  {
    status = 0;
    registry->wifi_mode = (uint8_t)mode;
    registry_request_save();
  }

  /* Release and request save */
  registry_release();
  return status;
}

bool nx_static_is_enabled(void)
{
  return (_nx_params.static_ip_settings != NULL);
}

void nx_static_get(cy_wcm_ip_setting_t *settings)
{
  memcpy(settings, &_nx_ip_settings, sizeof(cy_wcm_ip_setting_t));
}

int32_t nx_static_update(uint8_t enabled, cy_wcm_ip_setting_t *settings)
{
  t_registry_data *registry;

  /* Validate */
  if ((enabled == 1U) && !settings)
  {
    /* Invalid parameters */
    return -1;
  }
  if (
    ((enabled == 0U) && (_nx_params.static_ip_settings == NULL)) ||
    ((enabled == 1U) && (memcmp(&_nx_ip_settings, settings, sizeof(cy_wcm_ip_setting_t)) == 0))
  )
  {
    /* No update required */
    return -2;
  }

  /* Update registry */
  registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_NX, "Registry not available");
    Error_Handler();
  }

  /* Configure static IP */
  registry->wifi_static_enable  = enabled;
  registry->wifi_static_ip      = (enabled == 1U)? settings->ip_address.ip.v4 : 0U;
  registry->wifi_static_gateway = (enabled == 1U)? settings->gateway.ip.v4    : 0U;
  registry->wifi_static_netmask = (enabled == 1U)? settings->netmask.ip.v4    : 0U;

  /* Release and request save */
  registry_release();
  registry_request_save();
  return 0;
}

void nx_credentials_get(cy_wcm_ap_credentials_t *credentials)
{
  memcpy(credentials, &_nx_params.ap_credentials, sizeof(cy_wcm_ap_credentials_t));
}

int32_t nx_credentials_update(const cy_wcm_ap_credentials_t *credentials)
{
  t_registry_data *registry;

  /* Validate */
  if (!credentials)
  {
    /* Invalid parameters */
    return -1;
  }
  if (memcmp(&_nx_params.ap_credentials, credentials, sizeof(cy_wcm_ap_credentials_t)) == 0)
  {
    /* No update required */
    return -2;
  }

  /* Update registry */
  registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_NX, "Registry not available");
    Error_Handler();
  }

  /* Set credentials */
  memset(registry->wifi_ssid, 0, WIFI_SSID_STR_LEN);
  memcpy(registry->wifi_ssid, credentials->SSID, WIFI_SSID_LEN);
  memset(registry->wifi_pass, 0, WIFI_PASS_STR_LEN);
  memcpy(registry->wifi_pass, credentials->password, WIFI_PASS_LEN);
  registry->wifi_auth = credentials->security;

  /* Release and request save */
  registry_release();
  registry_request_save();
  return 0;
}

int32_t nx_stream_send_frame(uint8_t *frame, size_t size)
{
#if NX_APP_ACTIVE == NX_APP_RTSP_SERVER
  return nx_rtsp_stream_send_frame(frame, size);
#elif NX_APP_ACTIVE == NX_APP_TCP_STREAM
  return nx_tcp_stream_send_frame(frame, size);
#else
  return 1;
#endif
}

WEAK void nx_stream_on_frame_sent(uint8_t *buff)
{
  /* Meant to be implemented by user */
  UNUSED(buff);
}

WEAK void nx_stream_on_state_change(t_nx_stream state)
{
  /* Meant to be implemented by user */
  UNUSED(state);
}

/* Private user code ---------------------------------------------------------*/

/**
 * @brief Task initialization.
 */
static void _nx_task_init(void)
{
  int32_t         status;
  cy_wcm_config_t config = { };

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_NX_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Create event flags */
  status = tx_event_flags_create(&_nx_task.evt, "nx.evt");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Retrieve parameters */
  _nx_registry_init();

  /* Initialize GPIO (control) */
  _whd_init_gpio();

  /* Initialize peripheral */
  hsd1.Instance = SDMMC1;
  status = stm32_cypal_wifi_sdio_init(&hsd1);
  if (status != CY_RSLT_SUCCESS)
  {
    LERROR(TRACE_NX, "SDIO init failed!");
    Error_Handler();
  }

  /* Acquire FLASH (WIFI FW) */
  flash_acquire(true);

  /* Wifi init
   * TODO: Update to set interface with mode.
   */
  config.interface = CY_WCM_INTERFACE_TYPE_STA;
  status = cy_wcm_init(&config);
  if (status != CY_RSLT_SUCCESS)
  {
    LERROR(TRACE_NX, "Wifi init failed!");
    flash_acquire(false);
    Error_Handler();
  }

  /* Release FLASH */
  flash_acquire(false);

  /* Finish initialization */
  if (_nx_task.mode == NX_MODE_OFF)
  {
    /* Wifi is not enabled */
    status = cy_wcm_deinit();
    if (status != CY_RSLT_SUCCESS)
    {
      LERROR(TRACE_NX, "Wifi deinit failed!");
      Error_Handler();
    }
    LINFO(TRACE_NX, "Task disabled (mode OFF)");
    return;
  }
  if (
    (_nx_task.mode == NX_MODE_STA) && (
      (strlen((char*)_nx_params.ap_credentials.SSID) == 0U) ||
      ((_nx_params.ap_credentials.security != CY_WCM_SECURITY_OPEN) && (strlen((char*)_nx_params.ap_credentials.password) == 0U))
    )
  )
  {
    /* Wifi in STA mode but not configured */
    LINFO(TRACE_NX, "Task disabled (STA not configured)");
    _nx_task.mode = NX_MODE_OFF;
    return;
  }
  cy_wcm_register_event_callback(_whd_event_callback);

  /*-->> READY <<--*/
  LINFO(TRACE_NX, "Task started");
  task_raise_event(TX_EVT_NX_READY);
}

/**
 * @brief Task loop.
 * @param input
 */
static void _nx_task_run(uint32_t input)
{
#if defined(N6CAM_WIFI_MURATA)
  uint32_t            flags;
  int32_t             status;
  cy_wcm_ip_address_t ip = { };

  UNUSED(input);

  /* Initialize task */
  _nx_task_init();

  /* NX task */
  while(_nx_task.mode != NX_MODE_OFF)
  {
    /* Handle connection */
    LINFO(TRACE_NX, "Wifi connecting to \"%s\"...", _nx_params.ap_credentials.SSID);
    status = cy_wcm_connect_ap(&_nx_params, &ip);
    LDEBUG(TRACE_NX, "Wifi connection status: T(%lx) M(%lx) C(%lx)",
       CY_RSLT_GET_TYPE(status),
       CY_RSLT_GET_MODULE(status),
       CY_RSLT_GET_CODE(status)
    );
    if (status != CY_RSLT_SUCCESS)
    {
      LERROR(TRACE_NX, "Wifi connection failed! Retrying...");
      tx_thread_sleep(WIFI_RETRY_DELAY);
      continue;
    }

    /* Start application
     * NOTE: Application should be a task!
     */
    if (!_nx_task.init)
    {
      /* Wait until connected flag is raised */
      rtos_wait_any_event(&_nx_task.evt, NX_EVT_CONNECTED, true);

      /* Start core */
      nx_app_mdns();
      nx_app_sntp();
      nx_app_telnet();

      /* Start application */
      #if NX_APP_ACTIVE == NX_APP_RTSP_SERVER
      nx_app_rtsp_server();
      #elif NX_APP_ACTIVE == NX_APP_TCP_STREAM
      nx_app_tcp_stream();
      #else
      #error "No active NetX application"
      #endif

      /* Avoid re-init */
      _nx_task.init = true;
    }

    /* Trigger on connected */
    nx_sntp_time_update();

    /* Keep checking for disconnections */
    while (1)
    {
      /* Keep pulling for disconnection */
      status = tx_event_flags_get(&_nx_task.evt, NX_EVT_DISCONNECTED, TX_AND_CLEAR, &flags, WIFI_STATUS_PULL);
      if ((status != TX_SUCCESS) && (status != TX_NO_EVENTS))
      {
        Error_Handler();
      }
      if (!cy_wcm_is_connected_to_ap() || (flags & NX_EVT_DISCONNECTED))
      {
        nx_telnet_diconnect();
        break;
      }
    }
    tx_thread_sleep(WIFI_RETRY_DELAY);
  }
#endif /* N6CAM_WIFI_MURATA */
}

/**
 * @brief Get configuration from registry.
 */
static void _nx_registry_init(void)
{
  t_registry_data *registry;

  /* Prepare */
  memset(&_nx_params, 0, sizeof(cy_wcm_connect_params_t));
  memset(&_nx_ip_settings, 0, sizeof(cy_wcm_ip_setting_t));

  /* Get data from registry */
  registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_NX, "Registry not available");
    Error_Handler();
  }

  /* Wifi mode */
  _nx_task.mode = (t_nx_mode)((registry->wifi_mode >= NX_MODE_UNKNOWN)? NX_MODE_OFF : registry->wifi_mode);

  /* Network info */
  memcpy(_nx_params.ap_credentials.SSID, registry->wifi_ssid, WIFI_SSID_LEN);
  memcpy(_nx_params.ap_credentials.password, registry->wifi_pass, WIFI_PASS_LEN);
  _nx_params.ap_credentials.security = registry->wifi_auth;

  /* Static IP */
  _nx_params.static_ip_settings = NULL;
  if (registry->wifi_static_enable)
  {
    _nx_ip_settings.ip_address.version = CY_WCM_IP_VER_V4;
    _nx_ip_settings.ip_address.ip.v4   = registry->wifi_static_ip;
    _nx_ip_settings.gateway.version    = CY_WCM_IP_VER_V4;
    _nx_ip_settings.gateway.ip.v4      = registry->wifi_static_gateway;
    _nx_ip_settings.netmask.version    = CY_WCM_IP_VER_V4;
    _nx_ip_settings.netmask.ip.v4      = registry->wifi_static_netmask;
    _nx_params.static_ip_settings      = &_nx_ip_settings;
  }

  registry_release();
}

/**
 * @brief Wifi event callback.
 * @param event Event type
 * @param data  Event data
 */
static void _whd_event_callback(cy_wcm_event_t event, cy_wcm_event_data_t *data)
{
  /* Handle event */
  switch (event)
  {
    case CY_WCM_EVENT_CONNECTED:
      LINFO(TRACE_NX, "Wifi connected!");
      rtos_raise_event(&_nx_task.evt, NX_EVT_CONNECTED);
      break;

    case CY_WCM_EVENT_DISCONNECTED:
      LINFO(TRACE_NX, "Wifi disconnected! Trying to reconnect...");
      rtos_raise_event(&_nx_task.evt, NX_EVT_DISCONNECTED);
      break;

    case CY_WCM_EVENT_IP_CHANGED:
      LINFO(TRACE_NX, "IP updated!");
      if (data)
      {
        cy_wcm_ip_address_t ip;
        cy_wcm_mac_t        mac;

        /* Assume IPv4 */
        PRINT_IPV4(data->ip_addr.ip.v4, "IP     :");
        cy_wcm_get_gateway_ip_address(CY_WCM_INTERFACE_TYPE_STA, &ip);
        PRINT_IPV4(ip.ip.v4, "Gateway:");
        cy_wcm_get_ip_netmask(CY_WCM_INTERFACE_TYPE_STA, &ip);
        PRINT_IPV4(ip.ip.v4, "Netmask:");
        cy_wcm_get_mac_addr(CY_WCM_INTERFACE_TYPE_STA, &mac);
        PRINT_MAC(mac, "MAC    :");

        /* Check IP health */
        if (cy_wcm_is_connected_to_ap() && (data->ip_addr.ip.v4 == 0))
        {
          /* We lost IP... need to reconnect */
          _whd_event_callback(CY_WCM_EVENT_DISCONNECTED, NULL);
          cy_wcm_disconnect_ap();
        }
      }
      break;

    default:
      /* Unknown event */
      break;
  }
}

/**
 * @brief Initializes WHD control GPIOs.
 *
 * @todo  SIANA: Move to GPIO on N6Cam.BSP
 */
static void _whd_init_gpio(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : WL_REG_ON PB8*/
  gpio.Pin   = GPIO_PIN_8;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);

    /*Configure GPIO pin : WL_HOST_WAKE PB9 */
  gpio.Pin   = GPIO_PIN_9;
  gpio.Mode  = GPIO_MODE_IT_RISING;
  HAL_GPIO_Init(GPIOB, &gpio);

  /* EXTI interrupt init*/
#if 0
  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);
#endif
}

/**
 * @brief  Initializes the SD MSP.
 *
 * @todo   SIANA: Move to SD on N6Cam.BSP
 *
 * @param  hsd  SD handle
 */
void HAL_SD_MspInit(SD_HandleTypeDef* hsd)
{
  if(hsd->Instance == SDMMC1)
  {
    GPIO_InitTypeDef gpio = {0};

    HAL_PWREx_EnableVddIO4();

    /* Enable SDMMC clock */
    __HAL_RCC_SDMMC1_CLK_ENABLE();

    /* Enable GPIOs clock */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* Common GPIO configuration */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_SDMMC1;

    /* D0-D1-D2-D3-CLK PC8 PC9 PC10 PC11 PC12*/
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11  | GPIO_PIN_12 ;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* CMD: PH2 */
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOH, &gpio);

    /* NVIC configuration for SDMMC1 interrupts */
    //HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0);
    //HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
  }
}

/**
 * @brief  DeInitializes the SD MSP.
 *
 * @todo   SIANA: Move to SD on N6Cam.BSP
 *
 * @param  hsd  SD handle
 */
void HAL_SD_MspDeInit(SD_HandleTypeDef* hsd)
{
  GPIO_InitTypeDef gpio;

  if(hsd->Instance == SDMMC1)
  {
    /* Disable NVIC for SDIO interrupts */
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);

    /* GPIOC configuration */
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11  | GPIO_PIN_12 ;
    HAL_GPIO_DeInit(GPIOC, gpio.Pin);

    /* GPIOD configuration */
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_DeInit(GPIOH, gpio.Pin);

    /* Disable SDMMC1 clock */
    __HAL_RCC_SDMMC1_CLK_DISABLE();
  }
}
