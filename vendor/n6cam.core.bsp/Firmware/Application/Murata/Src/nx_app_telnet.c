/**
 ******************************************************************************
 * @file    nx_app_telnet.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the Telnet NetX utility
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
#include "nx_app_telnet.h"
#include "nxd_telnet_server.h"
#include "shell_task.h"

/*-->> Private Tunables <<----------------------------------------------------*/

#define TELNET_SERVER_STACK_SIZE  (2U * 1024U)
#define TELNET_PACKET_NUM         (5U)

/* Telnet server task
 * TODO: Optimize stack size
 */
#define TELNET_TASK_STACK_SIZE    (2U * 1024U)
#define TELNET_TASK_PRIO          APP_PRIO_THREADX_STACK
#define TELNET_TASK_TIME_SLICE    APP_TIME_SLICE_DEFAULT

/* Telnet control events */
#define TELNET_EVT_CONNECTED      BIT(0U)
#define TELNET_EVT_DISCONNECTED   BIT(1U)
#define TELNET_EVT_DATA           BIT(2U)
#define TELNET_EVT_ALL            BITMASK(3U)

/*-->> Private Definitions <<-------------------------------------------------*/

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/** Telnet instance */
typedef struct
{
  int32_t               conn;
  size_t                count;
  size_t                offset;
  NX_PACKET*            packet[TELNET_PACKET_NUM];
  TX_MUTEX              mtx_rx;
  TX_MUTEX              mtx_tx;
  /* NetX */
  NX_IP                 *ip;
  NX_PACKET_POOL        *pkt_pool;
  NX_TELNET_SERVER      server;
  uint8_t               server_stack[TELNET_SERVER_STACK_SIZE];
  /* Task */
  TX_EVENT_FLAGS_GROUP  evt;
  TX_THREAD             task;
  uint8_t               task_stack[TELNET_TASK_STACK_SIZE];
} t_telnet;

/*-->> Private Data <<--------------------------------------------------------*/

static t_telnet _telnet = {
  .conn = -1,
};

/*-->> Private Functions <<---------------------------------------------------*/

static int32_t    _telnet_read(uint8_t *buff, size_t size, uint32_t timeout);
static int32_t    _telnet_write(const uint8_t *buff, size_t size, uint32_t timeout);

static void       _telnet_task_run(uint32_t args);

static void       _telnet_packet_push(NX_PACKET *packet);
static NX_PACKET* _telnet_packet_pop(void);

static void       _telnet_connect_callback(NX_TELNET_SERVER *server, UINT connection);
static void       _telnet_disconnect_callback(NX_TELNET_SERVER *server, UINT connection);
static void       _telnet_data_callback(NX_TELNET_SERVER *server, UINT connection, NX_PACKET *packet);

/*-->> Public API <<----------------------------------------------------------*/

void nx_app_telnet(void)
{
  int32_t status;

  /* Create control events */
  status = tx_event_flags_create(&_telnet.evt, "nx.evt.telnet");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
  status = tx_mutex_create(&_telnet.mtx_rx, "nx.mtx.telnet.rx", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
  status = tx_mutex_create(&_telnet.mtx_tx, "nx.mtx.telnet.tx", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Get IP */
  _telnet.ip = (NX_IP*)cy_network_get_nw_interface(CY_NETWORK_WIFI_STA_INTERFACE, 0);
  status = cy_network_get_packet_pool(CY_NETWORK_PACKET_TX, (void**)&_telnet.pkt_pool);
  if (status != CY_RSLT_SUCCESS)
  {
    LERROR(TRACE_NX, "[TELNET] Unable to get byte pool");
    Error_Handler();
  }

  /* Create the main thread */
  status = tx_thread_create(
    &_telnet.task, "nx.task.telnet",
    _telnet_task_run, 0,
    _telnet.task_stack, TELNET_TASK_STACK_SIZE,
    TELNET_TASK_PRIO, TELNET_TASK_PRIO,
    TX_NO_TIME_SLICE, TX_AUTO_START
  );
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

void nx_telnet_diconnect(void)
{
  /* Force disconnect */
  if (_telnet.conn != -1)
  {
    int32_t conn = _telnet.conn;
    _telnet_disconnect_callback(&_telnet.server, (UINT)conn);
    nx_telnet_server_disconnect(&_telnet.server, (UINT)conn);
  }
}

t_stream* nx_telnet_get_stream(void)
{
  static t_stream _telnet_stream = { 0 };
  if (!_telnet_stream.init)
  {
    stream_init(&_telnet_stream, _telnet_read, _telnet_write);
  }
  return &_telnet_stream;
}

/* Private user code ---------------------------------------------------------*/

static int32_t _telnet_read(uint8_t *buff, size_t size, uint32_t timeout)
{
  NX_PACKET *packet;
  uint32_t  flags;
  int32_t   recv;
  int32_t   status;

  /* Validate */

  if (_telnet.conn == -1)
  {
    return 0;
  }

  /* Acquire */
  rtos_mutex_acquire(&_telnet.mtx_rx, true);

  /* Wait for data */
  status = tx_event_flags_get(&_telnet.evt, TELNET_EVT_DATA, (_telnet.count > 1U)? TX_OR : TX_OR_CLEAR, &flags, timeout);
  if (status == TX_NO_EVENTS)
  {
    /* On timeout, release and return */
    rtos_mutex_acquire(&_telnet.mtx_rx, false);
    return 0;
  }

  /* Pop packet */
  packet = _telnet_packet_pop();
  status = nx_packet_data_extract_offset(packet, 0U, buff, (ULONG)size, (ULONG*)&recv);
  if (status != NX_SUCCESS)
  {
    recv = 0;
  }
  nx_packet_release(packet);

  /* Release */
  rtos_mutex_acquire(&_telnet.mtx_rx, false);
  return recv;
}

static int32_t _telnet_write(const uint8_t *buff, size_t size, uint32_t timeout)
{
  NX_PACKET *packet;
  int32_t   status;

  /* Validate */
  if (_telnet.conn == -1)
  {
    return 0;
  }

  /* Acquire */
  rtos_mutex_acquire(&_telnet.mtx_tx, true);

  /* Prepare packet */
  status = nx_packet_allocate(_telnet.pkt_pool, &packet, NX_TCP_PACKET, NX_NO_WAIT);
  if (status != NX_SUCCESS)
  {
    return BSP_ERROR_COMPONENT;
  }
  status = nx_packet_data_append(packet, (void*)buff, (ULONG)size, _telnet.pkt_pool, NX_NO_WAIT);
  if (status != NX_SUCCESS)
  {
    nx_packet_release(packet);
    return BSP_ERROR_COMPONENT;
  }

  /* Send packet */
  status = nx_telnet_server_packet_send(&_telnet.server, (UINT)_telnet.conn, packet, timeout);
  nx_packet_release(packet);
  if (status != NX_SUCCESS)
  {
    return BSP_ERROR_UNKNOWN;
  }

  /* Release */
  rtos_mutex_acquire(&_telnet.mtx_tx, false);
  return (int32_t)size;
}

static void _telnet_task_run(uint32_t args)
{
  int32_t  status;

  /* Create server */
  status = nx_telnet_server_create(
    &_telnet.server, "nx.telnet.server", _telnet.ip,
    _telnet.server_stack, TELNET_SERVER_STACK_SIZE,
    _telnet_connect_callback,
    _telnet_data_callback,
    _telnet_disconnect_callback
  );
  if (status != NX_SUCCESS)
  {
    LERROR(TRACE_NX, "[TELNET] Failed to create server");
    Error_Handler();
  }

  /* Start server */
  status = nx_telnet_server_start(&_telnet.server);
  if (status != NX_SUCCESS)
  {
    LERROR(TRACE_NX, "[TELNET] Failed to start server");
    Error_Handler();
  }

  /* Telnet task */
  while (1)
  {
    /* Wait for connection */
    rtos_wait_any_event(&_telnet.evt, TELNET_EVT_CONNECTED, true);
    LINFO(TRACE_NX, "[TELNET] Connected (%d)", _telnet.conn);
    shell_stream_set(nx_telnet_get_stream());

    /* Wait for disconnection */
    rtos_wait_any_event(&_telnet.evt, TELNET_EVT_DISCONNECTED, true);
    LINFO(TRACE_NX, "[TELNET] Disconnected", _telnet.conn);
    shell_stream_set(NULL);
  }
}

static void _telnet_packet_push(NX_PACKET *packet)
{
  size_t write = (_telnet.offset + _telnet.count) % TELNET_PACKET_NUM;

  /* If about to overflow, remove oldest */
  if (_telnet.count == TELNET_PACKET_NUM)
  {
    nx_packet_release(_telnet.packet[_telnet.offset]);
    _telnet.offset = (_telnet.offset + 1U) % TELNET_PACKET_NUM;
  }

  /* Write new packet */
  _telnet.packet[write] = packet;
  if (_telnet.count < TELNET_PACKET_NUM)
  {
    _telnet.count++;
  }
}

static NX_PACKET* _telnet_packet_pop(void)
{
  NX_PACKET *packet = NULL;

  /* If available, read packet */
  if (_telnet.count > 0U)
  {
    packet = _telnet.packet[_telnet.offset];
    _telnet.packet[_telnet.offset] = NULL;
    _telnet.offset = (_telnet.offset + 1U) % TELNET_PACKET_NUM;
    _telnet.count--;
  }
  return packet;
}

static void _telnet_connect_callback(NX_TELNET_SERVER *server, UINT connection)
{
  /* Validate */
  if ((server != &_telnet.server) || (_telnet.conn != -1))
  {
    return;
  }

  /* Connected! */
  _telnet.conn = (int32_t)connection;
  rtos_clear_event(&_telnet.evt, TELNET_EVT_ALL);
  rtos_raise_event(&_telnet.evt, TELNET_EVT_CONNECTED);
}

static void _telnet_disconnect_callback(NX_TELNET_SERVER *server, UINT connection)
{
  /* Validate */
  if ((server != &_telnet.server) || (connection != _telnet.conn))
  {
    return;
  }

  /* Disconnected! */
  _telnet.conn = -1;
  rtos_clear_event(&_telnet.evt, TELNET_EVT_ALL);
  rtos_raise_event(&_telnet.evt, TELNET_EVT_DISCONNECTED);

  /* Release all pending packets */
  NX_PACKET *packet;
  while (_telnet.count)
  {
    packet = _telnet_packet_pop();
    if (packet == NULL)
    {
      continue;
    }
    nx_packet_release(packet);
  }
  _telnet.offset = 0U;
}

static void _telnet_data_callback(NX_TELNET_SERVER *server, UINT connection, NX_PACKET *packet)
{
  /* Validate */
  if ((server != &_telnet.server) || (connection != _telnet.conn))
  {
    nx_packet_release(packet);
    return;
  }

  /* Append and report new data */
  _telnet_packet_push(packet);
  rtos_raise_event(&_telnet.evt, TELNET_EVT_DATA);
}
