/**
 ******************************************************************************
 * @file    nx_app_sntp.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the SNTP NetX utility
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
#include <time.h>

#include "cy_network_mw_core.h"
#include "cy_wcm.h"
#include "n6cam_rtc.h"
#include "nx_app.h"
#include "nx_app_sntp.h"
#include "nxd_sntp_client.h"
#include "registry_task.h"

/*-->> Private Tunables <<----------------------------------------------------*/

/* SNTP client */
#define SNTP_RETRY_MAX        4U                          /*!< SNTP retry max */
#define SNTP_RETRY_DELAY      (1U * NX_IP_PERIODIC_RATE)  /*!< SNTP retry delay */
#define SNTP_TIMEOUT          (2U * NX_IP_PERIODIC_RATE)  /*!< SNTP timeout */

/* STNP client task
 * TODO: Optimize stack size
 */
#define SNTP_TASK_STACK_SIZE  (2U * 1024U)
#define SNTP_TASK_PRIO        APP_PRIO_THREADX_STACK
#define SNTP_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/*-->> Private Definitions <<-------------------------------------------------*/

/* SNTP control events */
#define SNTP_EVT_ALL              0xFFFFFFFFU
#define SNTP_EVT_UPDATE           BIT(0U)

/* SNTP conversion
 * - SNTP reference epoch is 1900
 * - UNIX reference epoch is 1970
 * Ref: https://tickelton.gitlab.io/articles/ntp-timestamps/
 */
#define SNTP_TO_UNIX_EPOCH_DIFF   2208988800UL

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/*-->> Private Data <<--------------------------------------------------------*/

/* Declare the prototypes for the test entry points. */
static NX_IP                *ip_ptr;
static NX_PACKET_POOL       *ip_tx_pool;

/* SNTP instance */
static TX_EVENT_FLAGS_GROUP _sntp_evt;
static NX_SNTP_CLIENT       _sntp_client;
static TX_THREAD            _sntp_task;
static uint8_t              _sntp_task_stack[SNTP_TASK_STACK_SIZE];

/* SNTP settings */
static t_sntp_settings      _sntp_settings = { 0 };

/*-->> Private Functions <<---------------------------------------------------*/

static void _sntp_task_run(uint32_t args);
static void _sntp_rtc_update(NX_SNTP_CLIENT *client);

/*-->> Public API <<----------------------------------------------------------*/

void nx_app_sntp(void)
{
  int32_t status;

  /* Get SNTP settings */
  status = nx_sntp_get_stored(&_sntp_settings);
  if (status != 0)
  {
    LERROR(TRACE_NX, "[SNTP] Unable to get settings");
    Error_Handler();
  }

  /* Only if enabled */
  if (!_sntp_settings.enable)
  {
    return;
  }

  /* Create control events */
  status = tx_event_flags_create(&_sntp_evt, "nx.evt.sntp");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Get IP */
  ip_ptr = (NX_IP*)cy_network_get_nw_interface(CY_NETWORK_WIFI_STA_INTERFACE, 0);
  status = cy_network_get_packet_pool(CY_NETWORK_PACKET_TX, (void**)&ip_tx_pool);
  if (status != CY_RSLT_SUCCESS)
  {
    LERROR(TRACE_NX, "[SNTP] Unable to get byte pool");
    Error_Handler();
  }

  /* Create the main thread */
  status = tx_thread_create(
    &_sntp_task, "nx.task.sntp",
    _sntp_task_run, 0,
    _sntp_task_stack, SNTP_TASK_STACK_SIZE,
    SNTP_TASK_PRIO, SNTP_TASK_PRIO,
    SNTP_TASK_TIME_SLICE, TX_AUTO_START
  );
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

int32_t nx_sntp_get(t_sntp_settings *settings)
{
  /* Validate */
  if (!settings)
  {
    return -1;
  }

  /* Get settings */
  memcpy(settings, &_sntp_settings, sizeof(t_sntp_settings));
  return 0;
}

int32_t nx_sntp_get_stored(t_sntp_settings *settings)
{
  t_registry_data *registry;

  /* Validate */
  if (!settings)
  {
    return -1;
  }

  /* Get SNTP settings */
  registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_NX, "Registry not available");
    Error_Handler();
  }

  memset(settings, 0, sizeof(t_sntp_settings));
  memcpy(settings->server, registry->wifi_sntp_server, WIFI_SERVER_LEN);
  settings->enable = registry->wifi_sntp_enable;
  settings->resync = registry->wifi_sntp_resync;
  registry_release();
  return 0;
}

int32_t nx_sntp_update(t_sntp_settings *settings)
{
  t_registry_data *registry;

  /* Validate */
  if (!settings)
  {
    /* Invalid parameters */
    return -1;
  }
  if (memcmp(&_sntp_settings, settings, sizeof(t_sntp_settings)) == 0)
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

  /* Set settings */
  registry->wifi_sntp_enable = settings->enable;
  registry->wifi_sntp_resync = settings->resync;
  memset(registry->wifi_sntp_server, 0, WIFI_SERVER_STR_LEN);
  memcpy(registry->wifi_sntp_server, settings->server, WIFI_SERVER_LEN);

  /* Release and request save */
  registry_release();
  registry_request_save();
  return 0;
}

void nx_sntp_time_update(void)
{
  /* Only if enabled */
  if (_sntp_settings.enable)
  {
    tx_event_flags_set(&_sntp_evt, SNTP_EVT_UPDATE, TX_OR);
  }
}

/* Private user code ---------------------------------------------------------*/

static void _sntp_task_run(uint32_t args)
{
  NXD_ADDRESS server_ip;
  uint32_t    server_status;
  uint32_t    fraction;
  uint32_t    seconds;
  uint32_t    flags;
  size_t      retry;
  int32_t     status;

  UNUSED(args);

  /* Create SNTP client */
  status = nx_sntp_client_create(&_sntp_client, ip_ptr, CY_NETWORK_WIFI_STA_INTERFACE, ip_tx_pool, NULL, NULL, NULL);
  if (status != NX_SUCCESS)
  {
    LERROR(TRACE_NX, "[SNTP] Failed to create client");
    Error_Handler();
  }

  /* Task loop */
  LINFO(TRACE_NX, "[SNTP] Service started!");
  while (1)
  {
    /* Wait for trigger */
    status = tx_event_flags_get(&_sntp_evt, SNTP_EVT_UPDATE, TX_OR_CLEAR, &flags, _sntp_settings.resync);
    if (!(                                                      /* Error if NOT: */
      ((status == TX_SUCCESS) && (SNTP_EVT_UPDATE & flags)) ||  /* Triggered OR  */
      (status == TX_NO_EVENTS)                                  /* Timeout       */
    ))
    {
      Error_Handler();
    }

    /* Resolve server IP */
    status = cy_network_get_hostbyname(CY_NETWORK_WIFI_STA_INTERFACE, _sntp_settings.server, NX_IP_VERSION_V4, SNTP_TIMEOUT, (void*)&server_ip);
    if (status != CY_RSLT_SUCCESS)
    {
      LERROR(TRACE_NX, "[SNTP] Unable to resolve: %s", (char*)_sntp_settings.server);
      continue;
    }

    /* Init and start client */
    status = nx_sntp_client_initialize_unicast(&_sntp_client, server_ip.nxd_ip_address.v4);
    if (status != NX_SUCCESS)
    {
      Error_Handler();
    }
    status = nx_sntp_client_run_unicast(&_sntp_client);
    if (status != NX_SUCCESS)
    {
      Error_Handler();
    }

    /* Try until server is ready */
    retry = 0;
    do
    {
      /* Verify valid SNTP service running */
      status = nx_sntp_client_receiving_updates(&_sntp_client, (UINT*)&server_status);
      if ((status == NX_SUCCESS) && (server_status == NX_TRUE))
      {
        /* Get time */
        status = nx_sntp_client_get_local_time_extended(&_sntp_client, &seconds, &fraction, NX_NULL, 0);
        if (status == NX_SUCCESS)
        {
          LINFO(TRACE_NX, "[SNTP] Updating RTC...");
          _sntp_rtc_update(&_sntp_client);
          break;
        }
      }

      /* Not ready or error */
      retry++;
      LDEBUG(TRACE_NX, "[SNTP] Wait for server (try %d/%d)", retry, SNTP_RETRY_MAX);
      tx_thread_sleep(SNTP_RETRY_DELAY);

    } while (retry < SNTP_RETRY_MAX);

    /* Stop client */
    status = nx_sntp_client_stop(&_sntp_client);
    if (status != NX_SUCCESS)
    {
      Error_Handler();
    }
  }
}

static void _sntp_rtc_update(NX_SNTP_CLIENT *client)
{
  struct tm   tinfo;
  time_t      tstamp;
  t_datetime  datetime;
  int32_t     status;

  /* Validate */
  if (!client)
  {
    return;
  }

  /* Get datetime
   * Note: Using localtime() instead of gmtime() causes issues on WHD init
   */
  tstamp = client->nx_sntp_current_server_time_message.receive_time.seconds - SNTP_TO_UNIX_EPOCH_DIFF;
  tinfo  = *gmtime(&tstamp);

  /* Convert to RTC format */
  datetime.year    = tinfo.tm_year - (DATETIME_YEAR_OFFSET - 1900);
  datetime.month   = tinfo.tm_mon + 1;
  datetime.day     = tinfo.tm_mday;
  datetime.hours   = tinfo.tm_hour;
  datetime.minutes = tinfo.tm_min;
  datetime.seconds = tinfo.tm_sec;

  /* Set RTC time */
  status = bsp_rtc_set_time(&datetime);
  if (status != BSP_OK)
  {
    LERROR(TRACE_NX, "[SNTP] Failed RTC update");
  }
}
