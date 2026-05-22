/**
 ******************************************************************************
 * @file    nx_app_tcp_stream.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the TCP NetX server
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
#include <string.h>

#include "cy_network_mw_core.h"
#include "nx_app.h"
#include "nx_app_tcp_stream.h"
#include "whd_types.h"

#if NX_APP_ACTIVE == NX_APP_TCP_STREAM
/*-->> Private Tunables <<----------------------------------------------------*/

/*-->> Private Definitions <<-------------------------------------------------*/

#define TCP_EVT_ALL             0xFFFFFFFFU
#define TCP_EVT_TEARDOWN        BIT(0)
#define TCP_EVT_VIDEO_DATA      BIT(1)

#define PACKET_SIZE             (WHD_PAYLOAD_MTU)
#define PAYLOAD_SIZE            (PACKET_SIZE - NX_TCP_PACKET)

#define CLIENT_INFO_SIZE        (64U)

#define TCP_PORT                (80U)
#define TCP_WINDOW              (6U * PACKET_SIZE)

#define STREAM_THREAD_PRIO      APP_PRIO_THREADX_STACK
#define STREAM_TASK_STACK_SIZE  (2U * 1024U)
#define STREAM_TASK_TIME_SLICE  TX_NO_TIME_SLICE

/*-->> Private Definitions <<-------------------------------------------------*/

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/*-->> Private Data <<--------------------------------------------------------*/

static NX_IP                *ip_ptr;
static NX_PACKET_POOL       *ip_tx_pool;

static TX_EVENT_FLAGS_GROUP _tcp_evt;
static NX_TCP_SOCKET        _tcp_server;

static TX_THREAD            _stream_task;
static uint8_t              _stream_stack[STREAM_TASK_STACK_SIZE];
static uint8_t              *_stream_frame = NULL;
static size_t               _stream_size   = 0;

static uint8_t              _client_info[CLIENT_INFO_SIZE];

/*-->> Private Functions <<---------------------------------------------------*/

static void     _tcp_listen(void);

static void     _tcp_disconnect_callback(NX_TCP_SOCKET *socket);
static void     _tcp_listen_callback(NX_TCP_SOCKET *socket, UINT port);
static void     _tcp_receive_callback(NX_TCP_SOCKET *socket);

static int32_t  _tcp_send_data(NX_TCP_SOCKET *socket, const uint8_t *data, size_t size);
static int32_t  _tcp_send_packet(NX_TCP_SOCKET *socket, const uint8_t *data, size_t size);

static void     _stream_task_create(NX_TCP_SOCKET *socket);
static void     _stream_task_run(uint32_t arg);
static void     _stream_task_exit_callback(TX_THREAD *thread, UINT condition);

/*-->> Public API <<----------------------------------------------------------*/

void nx_app_tcp_stream(void)
{
  int32_t status;

  /* Create event flags */
  status = tx_event_flags_create(&_tcp_evt, "nx.evt.tcp");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Get IP */
  ip_ptr = (NX_IP*)cy_network_get_nw_interface(CY_NETWORK_WIFI_STA_INTERFACE, 0);
  status = cy_network_get_packet_pool(CY_NETWORK_PACKET_TX, (void**)&ip_tx_pool);
  if (status != CY_RSLT_SUCCESS)
  {
    LERROR(TRACE_NX, "[TCP] Unable to get byte pool");
    Error_Handler();
  }

  /* Create socket */
  status = nx_tcp_socket_create(
    ip_ptr, &_tcp_server, "nx.socket.tcp", NX_IP_NORMAL, NX_FRAGMENT_OKAY,
    NX_IP_TIME_TO_LIVE, TCP_WINDOW, NX_NULL, _tcp_disconnect_callback
  );
  if (NX_SUCCESS != status)
  {
    LERROR(TRACE_NX, "[TCP] Failed to create socket");
    Error_Handler();
  }
  nx_tcp_socket_receive_notify(&_tcp_server, _tcp_receive_callback);
  LDEBUG(TRACE_NX, "[TCP %p] Socket created on port %d", &_tcp_server, TCP_PORT);

  /* Start listening */
  _tcp_listen();
}

int32_t nx_tcp_stream_send_frame(uint8_t *frame, size_t size)
{
  /* Validate */
  if (!frame || (size == 0))
  {
    return 1;
  }

  /* Update frame */
  _stream_frame = frame;
  _stream_size  = size;

  /* Notify streaming threads */
  rtos_raise_event(&_tcp_evt, TCP_EVT_VIDEO_DATA);
  return 0;
}

/* Private user code ---------------------------------------------------------*/

static void _tcp_listen(void)
{
  int32_t status;

  /* Get socket state. If idle/closed, start listening */
  nx_tcp_socket_info_get(&_tcp_server, NX_NULL, NX_NULL, NX_NULL, NX_NULL, NX_NULL, NX_NULL, NX_NULL, (uint32_t*)&status, NX_NULL, NX_NULL, NX_NULL);
  if (status == NX_TCP_CLOSED)
  {
    status = nx_tcp_server_socket_listen(ip_ptr, TCP_PORT, &_tcp_server, 5, _tcp_listen_callback);
    if (status != NX_SUCCESS)
    {
      LERROR(TRACE_NX, "[TCP %p] Unable to listen (0x%X)", &_tcp_server, (uint32_t)status);
      Error_Handler();
    }
    LDEBUG(TRACE_NX, "[TCP %p] Socket listening on port %d", &_tcp_server, TCP_PORT);
  }
}

static void _tcp_disconnect_callback(NX_TCP_SOCKET *socket)
{
  nx_tcp_socket_disconnect(socket, NX_IP_PERIODIC_RATE);
  nx_tcp_server_socket_unaccept(socket);
  rtos_raise_event(&_tcp_evt, TCP_EVT_TEARDOWN);
  LDEBUG(TRACE_NX, "[TCP %p] Socket disconnected");
}

static void _tcp_listen_callback(NX_TCP_SOCKET *socket, UINT port)
{
  /* Stop listening and accept connection */
  nx_tcp_server_socket_accept(socket, NX_WAIT_FOREVER);
  nx_tcp_server_socket_unlisten(ip_ptr, port);

  /* Start streaming */
  _stream_task_create(socket);
}

static void _tcp_receive_callback(NX_TCP_SOCKET *socket)
{
  NX_PACKET *packet;

  /* Unexpected... Print info */
  nx_tcp_socket_receive(socket, &packet, NX_NO_WAIT);
  LDEBUG(TRACE_NX, "[TCP %p] Received %d bytes from %s", socket, packet->nx_packet_length, _client_info);
}

static int32_t _tcp_send_data(NX_TCP_SOCKET *socket, const uint8_t *data, size_t size)
{
  /* Send all packets */
  const uint8_t *end = (data + size);
  while (data < end)
  {
    size_t chunk = MIN(end - data, PAYLOAD_SIZE);
    if (_tcp_send_packet(socket, data, chunk) != NX_SUCCESS)
    {
      return 1;
    }
    data += chunk;
  }
  return 0;
}

static int32_t _tcp_send_packet(NX_TCP_SOCKET *socket, const uint8_t *data, size_t size)
{
  NX_PACKET *packet;
  int32_t   status;

  /* Create */
  status = nx_packet_allocate(ip_tx_pool, &packet, NX_TCP_PACKET, NX_WAIT_FOREVER);
  if (status != NX_SUCCESS)
  {
    LERROR(TRACE_NX, "[TCP %p] Packet allocation failed", socket);
    return status;
  }

  /* Append data */
  status = nx_packet_data_append(packet, (void*)data, (ULONG)size, ip_tx_pool, NX_WAIT_FOREVER);
  if (status != NX_SUCCESS)
  {
    LERROR(TRACE_NX, "[TCP %p] Packet data append failed", socket);
    if (nx_packet_release(packet) != NX_SUCCESS)
    {
      LERROR(TRACE_NX, "[TCP %p] Packet release failed", socket);
    }
    return status;
  }

  /* Send with timeout */
  status = nx_tcp_socket_send(socket, packet, NX_IP_PERIODIC_RATE);
  if (status != NX_SUCCESS)
  {
    /* Connection can be already closed by peer. */
    LWARNING(TRACE_NX, "[TCP %p] Packet send failed: 0x%X", socket, (uint32_t)status);
    if (nx_packet_release(packet) != NX_SUCCESS)
    {
      LERROR(TRACE_NX, "[TCP %p] Packet release failed", socket);
    }
  }
  return status;
}

static void _stream_task_create(NX_TCP_SOCKET *socket)
{
  uint32_t ip;
  uint32_t port;
  int32_t  status;

  /* Get client info */
  nx_tcp_socket_peer_info_get(socket, &ip, &port);
  snprintf((char*)_client_info, CLIENT_INFO_SIZE, FMT_IPV4":"FMT_PORT, PARSE_IPV4(ip), port);
  LDEBUG(TRACE_NX, "[TCP %p] Client connected: %s", socket, _client_info);

  /* Create streaming task */
  status = tx_thread_create(
    &_stream_task, "nx.task.tcp",
    _stream_task_run, (uint32_t)socket,
    _stream_stack, STREAM_TASK_STACK_SIZE,
    STREAM_THREAD_PRIO, STREAM_THREAD_PRIO,
    STREAM_TASK_TIME_SLICE, TX_AUTO_START
  );
  if (status != TX_SUCCESS)
  {
    LERROR(TRACE_NX, "[TCP %p] Unable to create streaming task", socket);
    _tcp_disconnect_callback(socket);
    _tcp_listen();
    return;
  }
  tx_thread_entry_exit_notify(&_stream_task, _stream_task_exit_callback);
  tx_thread_resume(&_stream_task);

  /* Notify stream start */
  nx_stream_on_state_change(NX_STREAMING_ACTIVE);
}

static void _stream_task_run(uint32_t arg)
{
  NX_TCP_SOCKET *socket = (NX_TCP_SOCKET*)arg;
  uint32_t      flags;
  int32_t       status;

  /* Wait for dependencies */
  task_wait_event(TX_EVT_DISPLAY_READY | TX_EVT_JPEG_READY);

  /* Reset stream events */
  rtos_clear_event(&_tcp_evt, TCP_EVT_ALL);

  /* Streaming loop */
  while (1)
  {
    flags = rtos_wait_any_event(&_tcp_evt, TCP_EVT_ALL, true);
    if (flags & TCP_EVT_TEARDOWN)
    {
      nx_stream_on_state_change(NX_STREAMING_INACTIVE);
      break;
    }
    else if (flags & TCP_EVT_VIDEO_DATA)
    {
      status = _tcp_send_data(socket, _stream_frame, _stream_size);
      if (status != 0)
      {
        /* Check if TCP is still connected */
        nx_tcp_socket_info_get(socket, NX_NULL, NX_NULL, NX_NULL, NX_NULL, NX_NULL, NX_NULL, NX_NULL, &flags, NX_NULL, NX_NULL, NX_NULL);
        if ((status == NX_NOT_CONNECTED) || (flags == NX_TCP_CLOSED))
        {
          LERROR(TRACE_NX, "[TCP %p] Unable to send frame. Assume disconnected", socket);
          _tcp_disconnect_callback(socket);
        }
      }
      nx_stream_on_frame_sent(_stream_frame);
    }
  }

  /* Start listening before exit */
  _tcp_listen();
}

static void _stream_task_exit_callback(TX_THREAD *thread, UINT condition)
{
  int32_t  status;

  /* Only exit is supported */
  if (condition != TX_THREAD_EXIT)
  {
    return;
  }

  /* Run exit operations */
  status = tx_thread_delete(thread);
  if (status != TX_SUCCESS)
  {
    LERROR(TRACE_NX, "[TCP.TASK %p] Unable to delete thread", thread);
    Error_Handler();
  }
}

/* -------------------------------------------------------------------------- */
#endif /* NX_APP_TCP */
