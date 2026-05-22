/**
 ******************************************************************************
 * @file    nx_app_mdns.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the mDNS NetX utility
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
#include "cy_network_mw_core.h"
#include "cy_wcm.h"
#include "nx_app.h"
#include "nx_app_mdns.h"
#include "nxd_mdns.h"

/*-->> Private Tunables <<----------------------------------------------------*/

#define MDNS_SERVER_PRIORITY    3             /*!< mDNS server priority */
#define MDNS_SERVER_STACK_SIZE  (2U * 1024U)  /*!< mDNS server stack size */
#define MDNS_LOCAL_CACHE_SIZE   (2U * 1024U)  /*!< mDNS local cache size */
#define MDNS_PEER_CACHE_SIZE    (2U * 1024U)  /*!< mDNS peer cache size */

/*-->> Private Definitions <<-------------------------------------------------*/

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/*-->> Private Data <<--------------------------------------------------------*/

/* Declare the prototypes for the test entry points. */
static NX_IP            *ip_ptr;
static NX_PACKET_POOL   *ip_tx_pool;

/* mDNS instance */
static NX_MDNS          _mdns_server;
static uint8_t          _mdns_server_stack[MDNS_SERVER_STACK_SIZE];
static uint8_t          _mdns_local_cache[MDNS_LOCAL_CACHE_SIZE] ALIGN(4);
static uint8_t          _mdns_peer_cache[MDNS_PEER_CACHE_SIZE] ALIGN(4);

/* mDNS settings */
static t_mdns_settings  _mdns_settings = { 0 };

/*-->> Private Functions <<---------------------------------------------------*/

static void _mdns_probe_callback(struct NX_MDNS_STRUCT *hmdns, UCHAR *name, UINT state);
static void _mdns_cache_callback(NX_MDNS *hmdns, UINT state, UINT type);

/*-->> Public API <<----------------------------------------------------------*/

void nx_app_mdns(void)
{
  int32_t status;

  /* Get mDNS settings */
  status = nx_mdns_get_stored(&_mdns_settings);
  if (status != 0)
  {
    LERROR(TRACE_NX, "[MDNS] Unable to get settings");
    Error_Handler();
  }

  /* Only if enabled */
  if (!_mdns_settings.enable)
  {
    return;
  }

  /* Get IP */
  ip_ptr = (NX_IP*)cy_network_get_nw_interface(CY_NETWORK_WIFI_STA_INTERFACE, 0);
  status = cy_network_get_packet_pool(CY_NETWORK_PACKET_TX, (void**)&ip_tx_pool);
  if (status != CY_RSLT_SUCCESS)
  {
    LERROR(TRACE_NX, "[MDNS] Unable to get byte pool");
    Error_Handler();
  }

  /* Create a mDNS instance */
  status = nx_mdns_create(
    &_mdns_server, ip_ptr, ip_tx_pool, MDNS_SERVER_PRIORITY,
    _mdns_server_stack, MDNS_SERVER_STACK_SIZE,
    (UCHAR*)_mdns_settings.hostname,
    (void*)_mdns_local_cache, MDNS_LOCAL_CACHE_SIZE,
    (void*)_mdns_peer_cache, MDNS_PEER_CACHE_SIZE,
    _mdns_probe_callback
  );
  if (status != NX_SUCCESS)
  {
    Error_Handler();
  }

  /* Set the cache notify */
  status = nx_mdns_cache_notify_set(&_mdns_server, _mdns_cache_callback);
  if (status != NX_SUCCESS)
  {
    Error_Handler();
  }

  /* Enable the mDNS function.  */
  nx_mdns_enable(&_mdns_server, CY_NETWORK_WIFI_STA_INTERFACE);
  LINFO(TRACE_NX, "[MDNS] Service started!");
}

int32_t nx_mdns_get(t_mdns_settings *settings)
{
  /* Validate */
  if (!settings)
  {
    return -1;
  }

  /* Get settings */
  memcpy(settings, &_mdns_settings, sizeof(t_mdns_settings));
  return 0;
}

int32_t nx_mdns_get_stored(t_mdns_settings *settings)
{
  t_registry_data *registry;

  /* Validate */
  if (!settings)
  {
    return -1;
  }

  /* Get mDNS settings */
  registry = registry_acquire();
  if (!registry)
  {
    LERROR(TRACE_NX, "Registry not available");
    Error_Handler();
  }

  memset(settings, 0, sizeof(t_mdns_settings));
  memcpy(settings->hostname, registry->wifi_mdns_hostname, WIFI_HOSTNAME_LEN);
  settings->enable = registry->wifi_mdns_enable;
  registry_release();
  return 0;
}

int32_t nx_mdns_update(t_mdns_settings *settings)
{
  t_registry_data *registry;

  /* Validate */
  if (!settings)
  {
    /* Invalid parameters */
    return -1;
  }
  if (memcmp(&_mdns_settings, settings, sizeof(t_mdns_settings)) == 0)
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

  /* Set hostname */
  registry->wifi_mdns_enable = settings->enable;
  memset(registry->wifi_mdns_hostname, 0, WIFI_HOSTNAME_STR_LEN);
  memcpy(registry->wifi_mdns_hostname, settings->hostname, WIFI_HOSTNAME_LEN);

  /* Release and request save */
  registry_release();
  registry_request_save();
  return 0;
}

/* Private user code ---------------------------------------------------------*/

static void _mdns_probe_callback(struct NX_MDNS_STRUCT *hmdns, UCHAR *name, UINT state)
{
  NX_PARAMETER_NOT_USED(hmdns);

  /* Check probe state */
  if (state == NX_MDNS_LOCAL_HOST_REGISTERED_FAILURE)
  {
    LERROR(TRACE_NX, "[MDNS] Failed for hostname %s", name);
  }
}

static void _mdns_cache_callback(NX_MDNS *hmdns, UINT state, UINT type)
{
  NX_PARAMETER_NOT_USED(hmdns);

  /* Check cache type.  */
  LDEBUG(TRACE_NX, "[MDNS] %s cache full%s!!!",
    (type == NX_MDNS_CACHE_TYPE_LOCAL) ? "local" : "peer",
    (state == NX_MDNS_CACHE_STATE_FULL) ? "" : " with fragment"
  );
}
