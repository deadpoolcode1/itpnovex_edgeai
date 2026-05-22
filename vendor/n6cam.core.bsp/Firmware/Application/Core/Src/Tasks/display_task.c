/**
 ******************************************************************************
 * @file    display_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the display module.
 *          This module is responsible for:
 *          - Initialize drawing, encoding, and UVC
 *          - Enable/transmit stream
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
#include "display_task.h"
#include "draw.h"
#include "enc_h264.h"
#include "isp_services.h"
#include "n6cam_core.h"
#if ENABLE_NN == 1U
#include "nn_task.h"
#endif /* ENABLE_NN */
#if defined(N6CAM_WIFI_MURATA)
#include "nx_app.h"
#endif /* N6CAM_WIFI_MURATA */
#include "sai_transform.h"
#include "sai_utils.h"
#include "snapshot_task.h"
#include "stm32n6xx_ll_venc.h"
#include "ux_device_uvc.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Display
* @{
* @defgroup PUBLIC_Definitions          PUBLIC constants
* @defgroup PUBLIC_Macros               PUBLIC macros
* @defgroup PUBLIC_Types                PUBLIC data-types
* @defgroup PUBLIC_Data                 PUBLIC data / variables
* @defgroup PUBLIC_API                  PUBLIC API
* @defgroup PRIVATE_TUNABLES            PRIVATE compile-time tunables
* @defgroup PRIVATE_Definitions         PRIVATE constants
* @defgroup PRIVATE_Macros              PRIVATE macros
* @defgroup PRIVATE_Types               PRIVATE data-types
* @defgroup PRIVATE_Data                PRIVATE data / variables
* @defgroup PRIVATE_Functions           PRIVATE functions
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_TUNABLES
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* Encoder configuration */
#define H264_FRAME_MAX_SIZE   (240U * 1024U)

/* Style configuration */
#define BORDER_PADDING        10U
#define FONT_TITLE            _draw_f20
#define FONT_INFO             _draw_f16
#define FONT_DEBUG            _draw_f12

#define HEADER_LEFT_TITLE     "People Detection"
#define HEADER_LEFT_WIDTH     270U

#define HEADER_RIGHT_TITLE    "E2IP Technologies"
#define HEADER_RIGHT_SUBTITLE "Edge AI Sensing Kit"
#define HEADER_RIGHT_WIDTH    270U

#define STATISTICS_WIDTH      270U
#define STATISTICS_LABEL_COLS 18U
#define STATISTICS_INDENT     4U

/** Transparency Control. */
#define DISPLAY_ALPHA_VALUE   ALPHA(100U)  // 100% alpha value

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/* Display task tunables
 * TODO: Optimize stack size
 */
#define DISPLAY_TASK_STACK_SIZE     (2U * 1024U)
#define DISPLAY_TASK_PRIO           APP_PRIO_IMPORTANT
#define DISPLAY_TASK_TIME_SLICE     APP_TIME_SLICE_DEFAULT

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Display task handler */
typedef struct
{
  TX_THREAD thread;
  uint8_t   stack[DISPLAY_TASK_STACK_SIZE];
} t_display_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/* Task handler */
static t_display_task   _display_task = { 0 };

/* Streaming */
static volatile bool    _nwk_active = false;
static volatile bool    _nwk_flying = false;
static volatile bool    _uvc_active = false;
static volatile bool    _uvc_flying = false;

/* Drawing */
static volatile uint8_t _show_header_left  = 1;
static volatile uint8_t _show_header_right = 1;
static volatile uint8_t _show_statistics   = 1;

#if ENABLE_NN == 1U
static volatile uint8_t _show_ancillary    = 0;
static volatile uint8_t _show_detections   = 1;
static t_nn_box         _display_box_buff[NN_BOXES_MAX_NUM];
#endif /* ENABLE_NN */

static t_draw           _draw = { 0 };
static t_draw_font      _draw_f12 = { 0 };
static t_draw_font      _draw_f16 = { 0 };
static t_draw_font      _draw_f20 = { 0 };

/* Encoder */
static t_enc_h264       _enc_h264 = { 0 };
static uint8_t          _enc_h264_buff[H264_FRAME_MAX_SIZE] DMA_ALIGN IN_SRAM_UNCACHED;
static size_t           _enc_h264_size = 0;
static uint32_t         _enc_h264_bps = 0;
static uint32_t         _enc_h264_bps_tick = 0;
static size_t           _enc_h264_bps_total = 0;
static uint32_t         _enc_h264_skipped_frames = 0;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void     _display_task_run(uint32_t args);
static void     _display_task_init(void);

static void     _display_frame(void);
static int32_t  _frame_encode(void);
static void     _frame_stream(void);
static uint32_t _update_h264_bps(int32_t size_inc);

static void     _display_add_header_left(void);
static void     _display_add_header_right(void);
static void     _display_add_statistics(void);
static void     _display_print_statistics(t_stat_id id, const char *label, uint32_t width, size_t line, size_t lcols, size_t icols);

#if ENABLE_NN == 1U
static void     _display_add_ancillary(void);
static void     _display_add_detections(void);
static int8_t   _display_detection(t_sai_object * det, t_draw * dst, t_draw_font * font);
#endif /* ENABLE_NN */

#if defined(N6CAM_WIFI_MURATA)
extern void     nx_stream_on_frame_sent(uint8_t *buff);
extern void     nx_stream_on_state_change(t_nx_stream state);
#endif /* N6CAM_WIFI_MURATA */

extern void     ux_uvc_on_frame_sent(uint8_t *buff);
extern void     ux_uvc_on_state_change(t_ux_uvc_state state);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t display_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_display_task.thread, "tx.task.display",
    _display_task_run, 0,
    _display_task.stack, DISPLAY_TASK_STACK_SIZE,
    DISPLAY_TASK_PRIO, DISPLAY_TASK_PRIO,
    DISPLAY_TASK_TIME_SLICE, TX_AUTO_START
  );
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Display task entry point.
 * @param args Task arguments
 */
static void _display_task_run(uint32_t args)
{
  UNUSED(args);

  /* Initialize task */
  _display_task_init();

  /* Display task */
  while (1)
  {
    /* Wait for a new frame */
    camera_wait_event(CAMERA_EVT_FRAME_MAIN, true);

    /* Display a new frame */
    _draw.buff = camera_get_buffer(camera.main.id);
    if (_draw.buff != NULL)
    {
      _display_frame();
    }
  }
}

/**
 * @brief Display task initialization.
 */
static void _display_task_init(void)
{
  UINT status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_DISPLAY_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Initialize drawing utilities */
  status = draw_init();
  if (status != 0x00)
  {
    Error_Handler();
  }
  status = draw_font_init(&_draw_f12, &Font12);
  if (status != 0x00)
  {
    Error_Handler();
  }
  status = draw_font_init(&_draw_f16, &Font16);
  if (status != 0x00)
  {
    Error_Handler();
  }
  status = draw_font_init(&_draw_f20, &Font20);
  if (status != 0x00)
  {
    Error_Handler();
  }

  /* Initialize draw and encoder */
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  LL_VENC_Init();
  _draw.bpp        = camera.main.bpp;
  _draw.width      = camera.main.width;
  _draw.height     = camera.main.height;
  _enc_h264.fps    = camera.sensor.fps;
  _enc_h264.width  = camera.main.width;
  _enc_h264.height = camera.main.height;
  switch (camera.main.packer)
  {
    case DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1:
      _draw.input_mode   = DMA2D_INPUT_RGB565;
      _draw.output_mode  = DMA2D_OUTPUT_RGB565;
      _enc_h264.encoding = H264ENC_RGB565;
      break;

    case DCMIPP_PIXEL_PACKER_FORMAT_ARGB8888:
      _draw.input_mode   = DMA2D_INPUT_ARGB8888;
      _draw.output_mode  = DMA2D_OUTPUT_ARGB8888;
      _enc_h264.encoding = H264ENC_RGB888;
      break;

    default:
      LERROR(TRACE_DISPLAY, "Unsupported pixel packer format");
      Error_Handler();
  }
  enc_h264_init(&_enc_h264);

  /*-->> READY <<--*/
  LINFO(TRACE_DISPLAY, "Task started");
  task_raise_event(TX_EVT_DISPLAY_READY);
}

/**
 * @brief Display a frame.
 */
static void _display_frame(void)
{
  /* Create snapshot (no overlay) */
  snapshot_create(&_draw);

  /* Is there any streaming active? */
  if (_nwk_active || _uvc_active)
  {
    stat_time_start(STAT_TIME_DISPLAY_TOTAL);

    /* Draw overlays */
    stat_time_start(STAT_TIME_DISPLAY_DRAW);
    #if ENABLE_NN == 1U
    _display_add_detections();
    _display_add_ancillary();
    #endif /* ENABLE_NN */
    _display_add_statistics();
    _display_add_header_left();
    _display_add_header_right();
    stat_time_stop(STAT_TIME_DISPLAY_DRAW);

    /* Encode frame */
    stat_time_start(STAT_TIME_ENC_H264);
    int32_t size = _frame_encode();
    stat_time_stop(STAT_TIME_ENC_H264);

    /* Any data to stream? */
    if (size > 0)
    {
      _update_h264_bps(size);
      _frame_stream();
    }
    stat_time_stop(STAT_TIME_DISPLAY_TOTAL);
  }
}

/**
 * @brief Encode a raw frame to h264.
 *
 * @return Number of bytes in h264 frame or error code (negative)
 */
static int32_t _frame_encode(void)
{
  /* force i-frame if streaming is lagging */
  /* note:
   * probably not needed since internal will force an i-frame every second!
   * see: _enc_internal(...) in enc_h64.c
   */
  bool is_intra_force = false;      /* (_nwk_flying || _uvc_flying) */

  /* skip frame if previous still in flight */
  if (_nwk_flying || _uvc_flying)
  {
    _enc_h264_skipped_frames++;
    return 0;
  }
  _enc_h264_size = enc_h264_process(_draw.buff, _enc_h264_buff, H264_FRAME_MAX_SIZE, is_intra_force);
  return _enc_h264_size;
}

/**
 * @brief Send the new h264 frame to the streaming interfaces.
 */
static void _frame_stream(void)
{
  UINT status;

  /* Send network */
  #if defined(N6CAM_WIFI_MURATA)
  if (_nwk_active)
  {
    status = nx_stream_send_frame(_enc_h264_buff, _enc_h264_size);
    _nwk_flying = (status == 0);  /* network frame is in transit... */
  }
  #endif /* N6CAM_WIFI_MURATA */

  /* Send UVC */
  if (_uvc_active)
  {
    status = ux_uvc_send_frame(_enc_h264_buff, _enc_h264_size);
    _uvc_flying = (status == 0);   /* uvc frame is in transit... */
  }
}

/**
 * @brief Compute the h264 data rate.
 *
 * @param size_inc  Size of the new h264 frame
 * @return Data rate in bits per second
 */
static uint32_t _update_h264_bps(int32_t size_inc)
{
  uint32_t now = HAL_GetTick();

  /* update accumulated h264 payload size */
  _enc_h264_bps_total += (uint32_t)size_inc;

  /* update bit.per.second every second */
  if ((now - _enc_h264_bps_tick) > TX_TIMER_TICKS_PER_SECOND)
  {
    /* delta in seconds */
    float delta_sec = (float)(now - _enc_h264_bps_tick) / TX_TIMER_TICKS_PER_SECOND;

    /* compute bit per second */
    _enc_h264_bps = (_enc_h264_bps_total * 8) / delta_sec;

    /* reset for next round */
    _enc_h264_bps_tick = now;
    _enc_h264_bps_total = 0;
  }
  return _enc_h264_bps;
}

/**
 * @brief Add left header to display.
 */
static void _display_add_header_left(void)
{
  /* Validate: Check if required */
  if (!_show_header_left)
  {
    return;
  }

  /* Display */
  draw_printf_hw(&_draw, &FONT_TITLE, BORDER_PADDING, BORDER_PADDING, HEADER_LEFT_WIDTH, HEADER_LEFT_TITLE);
}

/**
 * @brief Add right header to display.
 */
static void _display_add_header_right(void)
{
  /* Validate: Check if required */
  if (!_show_header_right)
  {
    return;
  }

  uint32_t xpos = _draw.width - HEADER_RIGHT_WIDTH - BORDER_PADDING;
  uint32_t ypos = BORDER_PADDING;

  /* Display */
  draw_printf_hw(&_draw, &FONT_TITLE, xpos, ypos, HEADER_RIGHT_WIDTH, HEADER_RIGHT_TITLE);

  ypos += FONT_TITLE.height;
  draw_printf_hw(&_draw, &FONT_INFO, xpos, ypos, HEADER_RIGHT_WIDTH, HEADER_RIGHT_SUBTITLE);
}

/**
 * @brief Add statistics to display.
 */
static void _display_add_statistics(void)
{
  /* Validate: Check if required */
  if (!_show_statistics)
  {
    return;
  }

  /* Prepare */
  uint32_t line = 8U;
  uint32_t ypos = _draw.height - BORDER_PADDING - (line * FONT_DEBUG.height);

  /* Print statistics */
  _display_print_statistics(STAT_FREQ_CAMERA        , "FPS"             , STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, 0U);
  _display_print_statistics(STAT_TIME_NN_TOTAL      , "NN thread stats" , STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, 0U);
  _display_print_statistics(STAT_TIME_NN_COPY       , "copy"            , STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, STATISTICS_INDENT);
  _display_print_statistics(STAT_TIME_NN_MODEL      , "inference"       , STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, STATISTICS_INDENT);
  _display_print_statistics(STAT_TIME_NN_PP         , "pp"              , STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, STATISTICS_INDENT);
  _display_print_statistics(STAT_TIME_DISPLAY_TOTAL , "UVC thread stats", STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, 0U);
  _display_print_statistics(STAT_TIME_DISPLAY_DRAW  , "draw"            , STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, STATISTICS_INDENT);
  _display_print_statistics(STAT_TIME_ENC_H264      , "h264"            , STATISTICS_WIDTH, line--, STATISTICS_LABEL_COLS, STATISTICS_INDENT);

#if 0
  /* Draw H264 bitrate */
  ypos -= FONT_DEBUG.height;
  draw_printf_hw(
    &_draw, &FONT_DEBUG,
    BORDER_PADDING, ypos, STATISTICS_WIDTH,
    "h264 stream      :  %.1f Mbps", ((float)_enc_h264_bps / (float)(1024 * 1024))
  );
#endif

  /* Draw camera color info */
  ypos = _draw.height - BORDER_PADDING;
  if (camera_use_isp())
  {
    ISP_AWBAlgoTypeDef awb;
    camera_get_awb(&awb, NULL, NULL);
    ypos -= BORDER_PADDING;
    draw_fill_hw(
      &_draw, _draw.width - (2U * BORDER_PADDING), ypos,
      BORDER_PADDING, BORDER_PADDING,
      (awb.enable)? COLOR_GREEN : COLOR_RED
    );
  }
  else
  {
    uint16_t value;
    ypos -= (FONT_INFO.height + BORDER_PADDING);
    camera_get_brightness(&value);
    draw_printf_hw(
      &_draw, &FONT_INFO,
      _draw.width - (2U * FONT_INFO.width) - BORDER_PADDING, ypos, 0,
      "%u", value
    );
  }
}

/**
 * @brief Print statistics on the display.
 * @param id      Entry ID
 * @param label   Entry name
 * @param width   Print width (pixels)
 * @param line    Line to draw
 * @param lcols   Label columns
 * @param icols   Indent columns
 */
static void _display_print_statistics(t_stat_id id, const char *label, uint32_t width, size_t line, size_t lcols, size_t icols)
{
  /* Validate */
  if (id >= STAT_ENTRY_NUM)
  {
    return;
  }

  /* Prepare */
  uint32_t ypos = _draw.height - BORDER_PADDING - line * (FONT_DEBUG.height);

  /* Print */
  lcols -= icols;
  switch (id)
  {
    case STAT_FREQ_CAMERA:
    {
      draw_printf_hw(&_draw, &FONT_DEBUG, BORDER_PADDING, ypos, width, "%*s%-*s: %3lu Hz "          , icols, "", lcols, label, (uint32_t)stat_value(id));
      break;
    }

    default:
    {
      /* Assume time */
      t_stat_entry entry;
      stat_entry(id, &entry);
      draw_printf_hw(&_draw, &FONT_DEBUG, BORDER_PADDING, ypos, width, "%*s%-*s: %3d ms / %5.1f ms ", icols, "", lcols, label, entry.last, entry.value);
      break;
    }
  }
}

#if ENABLE_NN == 1U
/**
 * @brief Add ancillary preview to display.
 */
static void _display_add_ancillary(void)
{
  /* Validate: Check if required */
  if (!_show_ancillary)
  {
    return;
  }

  uint32_t xpos = _draw.width - camera.ancillary.width;
  uint32_t ypos = _draw.height - camera.ancillary.height;

  /* Display */
  /* Draw ancillary */
  draw_copy_from_hw(
    &_draw, DMA2D_M2M_BLEND, DMA2D_INPUT_RGB888, xpos, ypos,
    camera.ancillary.width, camera.ancillary.height,
    camera_get_buffer(camera.ancillary.id)
  );

  /* Draw margins */
  draw_rect_hw(
    &_draw, xpos, ypos,
    camera.ancillary.width, camera.ancillary.height,
    COLOR_RED
  );
}

/**
 * @brief Add detections to display.
 */
static void _display_add_detections(void)
{
  /* Validate: Check if required */
  if (!_show_detections)
  {
    return;
  }

  size_t count = nn_get_detections(_display_box_buff);

  /* Display */
  uint32_t selected_count = 0;
  for (size_t idx = 0; idx < count; idx++)
  {
    /* Draw detection box if class is included */
    t_sai_object det = { 0 };
    int8_t ret = sai_transform(&_display_box_buff[idx], &det);
    if (ret != 0)
    { 
      LDEBUG(TRACE_DISPLAY, "Failed to transform detection %d", idx);
      continue;
    }
    selected_count += _display_detection(&det, &_draw, &FONT_INFO);
  }

  /* Draw detections number */
  draw_printf_hw(
    &_draw, &FONT_INFO,
    BORDER_PADDING, BORDER_PADDING + (_show_header_left? FONT_TITLE.height: 0U), HEADER_LEFT_WIDTH,
    "Objects: %2d ", selected_count
  );
}

/**
 * @brief  Display a detection box on the screen.
 * @param det  Bounding box to display, in Top-Left-Width-Height format.
 * @param dst   Destination draw context.
 * @param font  Font to use for displaying text.
 * @retval 0 if successful but skipped unselected class, 
 *         1 if successful and displayed,
 *         -1 if any error ocurred.
 */
static int8_t _display_detection(t_sai_object * det, t_draw * dst, t_draw_font * font)
{
  int8_t ret = 1;

  /* Sanity Check */
  if (det == NULL || font == NULL) 
  { 
    LERROR(TRACE_DISPLAY, "Invalid font"); 
    return -1;
  }

  /* Get the color */
  t_sai_class *class = sai_utils_get_info(det->class);
  if (class == NULL) 
  { 
    LDEBUG(TRACE_DISPLAY, "Unselected class");
    return 0;
  }
  uint32_t color = COLOR_WITH_ALPHA(class->color, DISPLAY_ALPHA_VALUE);

#if POSTPROCESS_TYPE == POSTPROCESS_OD_YOLO_V8_UF

  /* Draw bounding box */
  draw_rect_hw( dst, det->x,  det->y, det->width,  det->height, color);
  draw_printf_hw(dst, font, det->x, det->y, det->width, "C: %s - %5.1f %%", class->name, det->score *100.0f);

# else 
  #error " Display not implemented for this post-processing type"
#endif /* POSTPROCESS_TYPE */

  return ret;
}
#endif /* ENABLE_NN */

#if defined(N6CAM_WIFI_MURATA)
void nx_stream_on_frame_sent(uint8_t *buff)
{
  UNUSED(buff);
  if (!_nwk_flying)
  {
    LWARNING(TRACE_DISPLAY, "WHD frame sent (but not in-flight)");
  }
  _nwk_flying = false;  /* network is idle! */
}

void nx_stream_on_state_change(t_nx_stream state)
{
  /* Validate */
  if (state > NX_STREAMING_ACTIVE)
  {
    Error_Handler();
  }

  /* Update state */
  _nwk_flying = false;
  _nwk_active = (state == NX_STREAMING_ACTIVE);
  bsp_led_set_state(LED_USER2, _nwk_active || _uvc_active);
}
#endif /* N6CAM_WIFI_MURATA */

void ux_uvc_on_frame_sent(uint8_t *buff)
{
  UNUSED(buff);
  if (!_uvc_flying)
  {
    LWARNING(TRACE_DISPLAY, "UVC frame sent (but not in-flight)");
  }
  _uvc_flying = false;  /* uvc is idle! */
}

void ux_uvc_on_state_change(t_ux_uvc_state state)
{
  /* Validate */
  if (state > UVC_STREAMING_ACTIVE)
  {
    Error_Handler();
  }

  /* Update state */
  _uvc_flying = false;
  _uvc_active = (state == UVC_STREAMING_ACTIVE);
  bsp_led_set_state(LED_USER2, _nwk_active || _uvc_active);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Display -->
*//*--------------------------------------------------------------------------*/
