/**
 ******************************************************************************
 * @file    nx_app_rtsp_server.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the RTSP/RTP NetX server
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

#include "camera_config.h"
#include "cy_network_mw_core.h"
#include "nx_app.h"
#include "nx_app_rtsp_server.h"
#include "nx_rtp_sender.h"
#include "nx_rtsp_server.h"
#include "whd_types.h"

#if NX_APP_ACTIVE == NX_APP_RTSP_SERVER
/*-->> Private Tunables <<----------------------------------------------------*/

/* Encoder settings */
#define H264_FRAME_MAX_SIZE       (192U * 1024U)              /*!< H264 encoder (max) frame size */

/* RTP settings */
#define RTP_CNAME                 "N6Cam@siana-systems.com"   /*!< Stream name for RTCP */

/* RTSP settings */
#define RTSP_SERVER_PORT          554                         /*!< RTSP listening port */
#define RTSP_SERVER_PRIORITY      3                           /*!< RTSP Server priority */
#define RTSP_SERVER_STACK_SIZE    (2U * 1024U)                /*!< RTSP Server stack size */

/* Server task
 * TODO: Optimize stack size
 */
#define RTSP_TASK_STACK_SIZE      (2U * 1024U)
#define RTSP_TASK_PRIO            APP_PRIO_THREADX_STACK
#define RTSP_TASK_TIME_SLICE      APP_TIME_SLICE_DEFAULT

/*-->> Private Definitions <<-------------------------------------------------*/

/* RTSP control events. */
#define RTSP_EVT_ALL              0xFFFFFFFFU
#define RTSP_EVT_PLAY             BIT(0U)
#define RTSP_EVT_TEARDOWN         BIT(1U)
#define RTSP_EVT_VIDEO_DATA       BIT(2U)

/* RTP settings */
#define RTP_VIDEO_FPS             CAMERA_SENSOR_FPS                             /*!< Depends on camera sensor */
#define RTP_VIDEO_TYPE            96                                            /*!< H264: Use dynamic type range from 96 to 127 */
#define RTP_VIDEO_CLOCK           90000                                         /*!< Assumes RTP clock of 90000 */
#define RTP_VIDEO_SAMPLING_PERIOD (RTP_VIDEO_CLOCK / RTP_VIDEO_FPS)

#define RTP_TIMER_PLAY_INTERVAL   10                                            /*!< Timer update each 10[msec] */
#define RTP_VIDEO_PLAY_INTERVAL   (TX_TIMER_TICKS_PER_SECOND / RTP_VIDEO_FPS)   /*!< Interval in ticks */

/* RTSP settings */
#define RTSP_VIDEO_FILE_NAME      "trackID=0"                                   /*!< The file name shown in RTSP SETUP request */

/* SDP string options. */
#define RTSP_SDP \
  "v=0\r\n"\
  "s=H264 video, streamed by N6Cam RTSP Server\r\n"\
  "m=video 0 RTP/AVP "STRINGIFY(RTP_VIDEO_TYPE)"\r\n"\
  "a=rtpmap:"STRINGIFY(RTP_VIDEO_TYPE)" H264/"STRINGIFY(RTP_VIDEO_CLOCK)"\r\n"\
  "a=fmtp:"STRINGIFY(RTP_VIDEO_TYPE)" profile-level-id=4D6020; packetization-mode=1\r\n"\
  "a=control:"RTSP_VIDEO_FILE_NAME"\r\n"

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/** RTSP client instance */
typedef struct
{
  NX_RTSP_CLIENT  *client;
  NX_RTP_SESSION  video;
  size_t          video_client_count;
  uint32_t        video_timestamp;
} t_rtsp_client;

/*-->> Private Data <<--------------------------------------------------------*/

/* Declare the prototypes for the test entry points. */
static NX_IP                *ip_ptr;
static NX_PACKET_POOL       *ip_tx_pool;

/* RTP/RTSP instance */
static NX_RTP_SENDER        _rtp_sender;
static TX_TIMER             _rtp_timer;
static uint32_t             _rtp_ticks = 0;

static TX_EVENT_FLAGS_GROUP _rtsp_evt;
static NX_RTSP_SERVER       _rtsp_server;
static uint8_t              _rtsp_server_stack[RTSP_SERVER_STACK_SIZE];
static TX_THREAD            _rtsp_task;
static uint8_t              _rtsp_task_stack[RTSP_TASK_STACK_SIZE];
static t_rtsp_client        _rtsp_client[NX_RTSP_SERVER_MAX_CLIENTS];

/* Frame handler */
static uint8_t              _h264_enc_buff[H264_FRAME_MAX_SIZE] DMA_ALIGN IN_PSRAM;
static size_t               _h264_enc_size = 0;

/*-->> Private Functions <<---------------------------------------------------*/

static void _rtsp_task_run(uint32_t args);

static void _rtp_timer_callback(ULONG addr);
static UINT _rtp_receiver_report_callback(NX_RTP_SESSION *session, NX_RTCP_RECEIVER_REPORT *report);
static UINT _rtp_sdes_callback(NX_RTCP_SDES_INFO *info);

static UINT _rtsp_describe_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize);
static UINT _rtsp_disconnect_callback(NX_RTSP_CLIENT *client);
static UINT _rtsp_pause_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, UCHAR *range, UINT rsize);
static UINT _rtsp_play_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, UCHAR *range, UINT rsize);
static UINT _rtsp_set_parameter_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, UCHAR *param, ULONG psize);
static UINT _rtsp_setup_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, NX_RTSP_TRANSPORT *tpt);
static UINT _rtsp_teardown_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize);

/*-->> Public API <<----------------------------------------------------------*/

void nx_app_rtsp_server(void)
{
  int32_t status;

  /* Create event for the play thread */
  status = tx_event_flags_create(&_rtsp_evt, "nx.evt.rstp");
  if (status)
  {
    Error_Handler();
  }

  /* Create timer for play event */
  status = tx_timer_create(
    &_rtp_timer, "nx.tim.rtp", _rtp_timer_callback, 0,
    (RTP_TIMER_PLAY_INTERVAL * NX_IP_PERIODIC_RATE / 1000U),
    (RTP_TIMER_PLAY_INTERVAL * NX_IP_PERIODIC_RATE / 1000U),
    TX_AUTO_ACTIVATE
  );
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Get IP */
  ip_ptr = (NX_IP*)cy_network_get_nw_interface(CY_NETWORK_WIFI_STA_INTERFACE, 0);
  status = cy_network_get_packet_pool(CY_NETWORK_PACKET_TX, (void**)&ip_tx_pool);
  if (status != CY_RSLT_SUCCESS)
  {
    LERROR(TRACE_NX, "[RTSP] Unable to get byte pool");
    Error_Handler();
  }

  /* Create the main thread */
  status = tx_thread_create(
    &_rtsp_task, "nx.task.rtsp",
    _rtsp_task_run, 0,
    _rtsp_task_stack, RTSP_TASK_STACK_SIZE,
    RTSP_TASK_PRIO, RTSP_TASK_PRIO,
    RTSP_TASK_TIME_SLICE, TX_AUTO_START
  );
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }
}

int32_t nx_rtsp_stream_send_frame(uint8_t *frame, size_t size)
{
  /* sanity check... */
  if (!frame || (size == 0))
  {
	  LERROR(TRACE_NX, "[RTSP] Invalid H264 buffer");
    return 1;
  }

  /* input frame is too large? */
  if (size > H264_FRAME_MAX_SIZE)
  {
	  LERROR(TRACE_NX, "[RTSP] H264 frame size exceeds RTP buffer");
	  return 1;
  }

  /* previous frame is still in-flight? */
  if (_h264_enc_size > 0)
  {
	  LWARNING(TRACE_NX, "[RTSP] Previous H264 frame still in-flight");
	  return 2;
  }

  /* local cache of the h264 frame */
  memcpy(_h264_enc_buff, frame, size);
  _h264_enc_size = size;

  /* Notify streaming threads */
  rtos_raise_event(&_rtsp_evt, RTSP_EVT_VIDEO_DATA);
  return 0;
}

/* Private user code ---------------------------------------------------------*/

static void _rtsp_task_run(uint32_t args)
{
  t_rtsp_client *client_ptr = NULL;
  uint32_t      timer_msw   = 0;
  uint32_t      timer_lsw   = 0;
  uint32_t      events      = 0;
  uint8_t       initialized = NX_FALSE;
  size_t        idx         = 0;
  int32_t       status;

  UNUSED(args);

  /* Create RTP sender */
  status = nx_rtp_sender_create(&_rtp_sender, ip_ptr, ip_tx_pool, RTP_CNAME, sizeof(RTP_CNAME) - 1U);
  if (status != NX_SUCCESS)
  {
    LERROR(TRACE_NX, "[RTSP] Failed to create RTP sender");
    Error_Handler();
  }
  nx_rtp_sender_rtcp_receiver_report_callback_set(&_rtp_sender, _rtp_receiver_report_callback);
  nx_rtp_sender_rtcp_sdes_callback_set(&_rtp_sender, _rtp_sdes_callback);

  /* Create RTSP server */
  status = nx_rtsp_server_create(
    &_rtsp_server, "nx.server.rtsp", sizeof("nx.server.rtsp") - 1U,
    ip_ptr, ip_tx_pool, _rtsp_server_stack,
    RTSP_SERVER_STACK_SIZE, RTSP_SERVER_PRIORITY, RTSP_SERVER_PORT,
    _rtsp_disconnect_callback
  );
  if (status != NX_SUCCESS)
  {
    LERROR(TRACE_NX, "[RTSP] Failed to create server");
    Error_Handler();
  }

  /* Set callback functions */
  nx_rtsp_server_describe_callback_set(&_rtsp_server, _rtsp_describe_callback);
  nx_rtsp_server_setup_callback_set(&_rtsp_server, _rtsp_setup_callback);
  nx_rtsp_server_play_callback_set(&_rtsp_server, _rtsp_play_callback);
  nx_rtsp_server_teardown_callback_set(&_rtsp_server, _rtsp_teardown_callback);
  nx_rtsp_server_pause_callback_set(&_rtsp_server, _rtsp_pause_callback);
  nx_rtsp_server_set_parameter_callback_set(&_rtsp_server, _rtsp_set_parameter_callback);

  /* Task loop */
  nx_rtsp_server_start(&_rtsp_server);
  LINFO(TRACE_NX, "[RTSP] Server started!");
  while (1)
  {
    tx_event_flags_get(&_rtsp_evt, RTSP_EVT_ALL, TX_OR_CLEAR, &events, TX_WAIT_FOREVER);

    /*********************************************
    ************** DEMO_PLAY_EVENT ***************
    *********************************************/
    if (events & RTSP_EVT_PLAY)
    {
      /* Set the initialized flag */
      initialized = NX_TRUE;
      nx_stream_on_state_change(NX_STREAMING_ACTIVE);
    }

    /*********************************************
    ************ DEMO_TEARDOWN_EVENT *************
    *********************************************/
    if (events & RTSP_EVT_TEARDOWN)
    {
      /* Check all client count to determine whether to clear the initialized flag */
      for (idx = 0; idx < NX_RTSP_SERVER_MAX_CLIENTS; idx++)
      {
        client_ptr = &_rtsp_client[idx];
        if (client_ptr->video_client_count)
        {
          break;
        }
      }
      if (idx == NX_RTSP_SERVER_MAX_CLIENTS)
      {
        initialized = NX_FALSE;
        nx_stream_on_state_change(NX_STREAMING_INACTIVE);
      }

      _h264_enc_size = 0; /* reset the H264 buffer */
    }

    /*********************************************
    ******** DEMO_VIDEO_DATA_READY_EVENT *********
    *********************************************/
    if (events & RTSP_EVT_VIDEO_DATA)
    {
      /* Check if a play event has already triggered. */
      if (initialized == NX_TRUE)
      {
        /* Read video data and transmit it */
        if (_h264_enc_size > 0)
        {
          for (idx = 0; idx < NX_RTSP_SERVER_MAX_CLIENTS; idx++)
          {
            client_ptr = &_rtsp_client[idx];

            /* Make sure at least one client having setup the connection. */
            if (client_ptr->video_client_count == 0)
            {
              continue;
            }

            /* Send video data to the client */
            timer_msw = _rtp_ticks / (1000U / RTP_TIMER_PLAY_INTERVAL);
            timer_lsw = ((uint64_t)_rtp_ticks << 32U) / NX_IP_PERIODIC_RATE;
            status = nx_rtp_sender_session_h264_send(
              &(client_ptr->video), _h264_enc_buff, _h264_enc_size,
              client_ptr->video_timestamp, timer_msw, timer_lsw, NX_TRUE
            );
            if (status)
            {
              LINFO(TRACE_NX, "[RTSP] Failed to send video frame: %d, %d", idx, status);
            }

            /* Update rtp timestamp video sampling period. */
            client_ptr->video_timestamp += RTP_VIDEO_SAMPLING_PERIOD;
          }

          /* end of frame sent */
          nx_stream_on_frame_sent(_h264_enc_buff);
          _h264_enc_size = 0;
        }
      }
    }
  }
}

static void _rtp_timer_callback(ULONG addr)
{
  UNUSED(addr);
  _rtp_ticks++;
}

static UINT _rtp_receiver_report_callback(NX_RTP_SESSION *session, NX_RTCP_RECEIVER_REPORT *report)
{
  size_t idx;

  UNUSED(report);

  /* Search the rtsp client table and find which session it is */
  for (idx = 0; idx < NX_RTSP_SERVER_MAX_CLIENTS; idx++)
  {
    if (session == &(_rtsp_client[idx].video))
    {
      break;
    }
  }
  if (idx == NX_RTSP_SERVER_MAX_CLIENTS)
  {
    /* Unknown session, return directly. */
    return (NX_SUCCESS);
  }

  /* Update the timeout of RTSP server since the RTCP message proves liveness.  */
  nx_rtsp_server_keepalive_update(_rtsp_client[idx].client);
  return (NX_SUCCESS);
}

static UINT _rtp_sdes_callback(NX_RTCP_SDES_INFO *info)
{
  UNUSED(info);

  return NX_SUCCESS;
}

static UINT _rtsp_describe_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize)
{
  UINT status;

  UNUSED(uri);
  UNUSED(usize);

  /* Set the SDP information. */
  LINFO(TRACE_NX, "[RTSP] Request: DESCRIBE");
  status = nx_rtsp_server_sdp_set(client, (UCHAR *)RTSP_SDP, sizeof(RTSP_SDP));
  return(status);
}

static UINT _rtsp_disconnect_callback(NX_RTSP_CLIENT *client)
{
  LINFO(TRACE_NX, "[RTSP] Request: DISCONNECT (TEARDOWN)");
  _rtsp_teardown_callback(client, NULL, 0);
  return(NX_SUCCESS);
}

static UINT _rtsp_pause_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, UCHAR *range, UINT rsize)
{
  UNUSED(client);
  UNUSED(uri);
  UNUSED(usize);
  UNUSED(range);
  UNUSED(rsize);

  LINFO(TRACE_NX, "[RTSP] Request: PAUSE (NOT IMPLEMENTED)");
  return(NX_SUCCESS);
}

static UINT _rtsp_play_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, UCHAR *range, UINT rsize)
{
  t_rtsp_client *client_ptr = NULL;
  uint32_t      video_time;
  uint32_t      video_seq;
  int32_t       status;
  size_t        idx;

  UNUSED(uri);
  UNUSED(usize);
  UNUSED(range);
  UNUSED(rsize);

  LINFO(TRACE_NX, "[RTSP] Request: PLAY");

  /* Search and find the RTSP client. */
  for (idx = 0; idx < NX_RTSP_SERVER_MAX_CLIENTS; idx++)
  {
    if (_rtsp_client[idx].client == client)
    {
      client_ptr = &_rtsp_client[idx];
      break;
    }
  }
  if (idx == NX_RTSP_SERVER_MAX_CLIENTS)
  {
    LINFO(TRACE_NX, "[RTSP] No clients available");
    return(NX_NOT_SUCCESSFUL);
  }

  /* If streaming is ON, return directly. */
  if (client_ptr->video_client_count)
  {
    /* Retrieve the sequence number through RTP sender functions */
    nx_rtp_sender_session_sequence_number_get(&(client_ptr->video), (UINT*)&video_seq);

    /* Assign recorded timestamps */
    video_time = client_ptr->video_timestamp;

    /* Set RTP information into RTSP client */
    status = nx_rtsp_server_rtp_info_set(client, (UCHAR*)RTSP_VIDEO_FILE_NAME, sizeof(RTSP_VIDEO_FILE_NAME) - 1U, video_seq, video_time);
    if (status)
    {
      return(status);
    }
  }

  /* Trigger the play event */
  tx_event_flags_set(&_rtsp_evt, RTSP_EVT_PLAY, TX_OR);
  return(NX_SUCCESS);
}

static UINT _rtsp_set_parameter_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, UCHAR *param, ULONG psize)
{
  UNUSED(client);
  UNUSED(uri);
  UNUSED(usize);
  UNUSED(param);
  UNUSED(psize);

  LINFO(TRACE_NX, "[RTSP] Request: SET PARAMETER (NOT IMPLEMENTED).");
  return(NX_SUCCESS);
}

static UINT _rtsp_setup_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize, NX_RTSP_TRANSPORT *tpt)
{
  t_rtsp_client *client_ptr = NX_NULL;
  uint32_t      rtp_port;
  uint32_t      rtcp_port;
  int32_t       status;
  size_t        idx;

  UNUSED(usize);

  /* Get the created and found ports */
  status = nx_rtp_sender_port_get(&_rtp_sender, (UINT*)&rtp_port, (UINT*)&rtcp_port);
  if (status)
  {
    return(status);
  }
  tpt->server_rtp_port = rtp_port;
  tpt->server_rtcp_port = rtcp_port;

  /* Print information from the client */
  LINFO(TRACE_NX, "[RTSP] Request: SETUP.\r\nuri: %s\r\nclient RTP port "FMT_PORT", RTCP port "FMT_PORT", IP "FMT_IPV4,
    uri, rtp_port, rtcp_port, PARSE_IPV4(REVERSE_IPV4(tpt->client_ip_address.nxd_ip_address.v4))
  );

  /* Find and store the RTSP client pointer.  */
  for (idx = 0; idx < NX_RTSP_SERVER_MAX_CLIENTS; idx++)
  {
    /* Check if the client is already linked. */
    if (_rtsp_client[idx].client == client)
    {
      client_ptr = &(_rtsp_client[idx]);
      break;
    }
    if ((client_ptr == NX_NULL) && (_rtsp_client[idx].client == NX_NULL))
    {
      /* Record the unused position. */
      client_ptr = &(_rtsp_client[idx]);
    }
  }

  /* Check and return error if reach max connected clients limitation. */
  if (client_ptr == NX_NULL)
  {
    return(NX_NOT_SUCCESSFUL);
  }

  /* Link the client pointer if it is newly assigned. */
  if (idx == NX_RTSP_SERVER_MAX_CLIENTS)
  {
    client_ptr->client = client;
  }
  if (strstr((const char *)uri, RTSP_VIDEO_FILE_NAME))
  {
    LINFO(TRACE_NX, "[RTSP] Setup Video (track 0)..");

    /* Setup rtp sender video session */
    status = nx_rtp_sender_session_create(
      &_rtp_sender, &(client_ptr->video), RTP_VIDEO_TYPE,
      tpt->interface_index, &(tpt->client_ip_address),
      tpt->client_rtp_port, tpt->client_rtcp_port
    );
    if (status)
    {
      LINFO(TRACE_NX, "[RTSP] Failed to create session");

      /* Reset the client pointer if error status happens */
      client_ptr->client = NX_NULL;
      return (status);
    }

    /* Obtain generated ssrc */
    status = nx_rtp_sender_session_ssrc_get(&(client_ptr->video), &(tpt->rtp_ssrc));
    if (status)
    {
      /* Reset the client pointer if error status happens */
      client_ptr->client = NX_NULL;
      return (status);
    }

    /* Reset corresponding variables */
    client_ptr->video_timestamp = (ULONG)NX_RAND();

    /* Increase the number of setup client. */
    client_ptr->video_client_count++;
  }
  else
  {
    LINFO(TRACE_NX, "[RTSP] Invalid track ID");
    return (NX_NOT_SUCCESSFUL);
  }
  return (NX_SUCCESS);
}

static UINT _rtsp_teardown_callback(NX_RTSP_CLIENT *client, UCHAR *uri, UINT usize)
{
  t_rtsp_client *client_ptr = NULL;
  size_t        idx;

  UNUSED(uri);
  UNUSED(usize);

  LINFO(TRACE_NX, "[RTSP] Request: TEARDOWN");

  /* Find the RTSP client pointer.  */
  for (idx = 0; idx < NX_RTSP_SERVER_MAX_CLIENTS; idx++)
  {
    if (_rtsp_client[idx].client == client)
    {
      client_ptr = &(_rtsp_client[idx]);
      break;
    }
  }
  if (idx == NX_RTSP_SERVER_MAX_CLIENTS)
  {
    LINFO(TRACE_NX, "[RTSP] Failed to find client");
    return (NX_NOT_SUCCESSFUL);
  }

  /* Decrease session client count */
  if (client_ptr->video_client_count > 0)
  {
    client_ptr->video_client_count--;
    {
      client_ptr->client = NX_NULL;
      nx_rtp_sender_session_delete(&(client_ptr->video));
    }
  }

  /* Trigger the tear down event */
  tx_event_flags_set(&_rtsp_evt, RTSP_EVT_TEARDOWN, TX_OR);
  return(NX_SUCCESS);
}

/* -------------------------------------------------------------------------- */
#endif /* NX_APP_RTSP */
