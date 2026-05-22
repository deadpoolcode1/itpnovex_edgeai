/**
 ******************************************************************************
 * @file    jpeg_task.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the JPEG encoding module.
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
#include "camera_task.h"
#include "jpeg_task.h"
#include "jpeg_utils.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup JPEG
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

/* JPEG task tunables
 * TODO: Optimize stack size
 */
#define JPEG_TASK_STACK_SIZE  (4U * 1024U)
#define JPEG_TASK_PRIO        APP_PRIO_DEFAULT
#define JPEG_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* JPEG task events */
#define JPEG_EVT_ALL          0xFFFFFFFFU
#define JPEG_EVT_ENCODE       BIT(0U)
#define JPEG_EVT_READY        BIT(1U)

/* Image configuration */
#define IMAGE_SIZE            CAMERA_MAIN_BUFFER_SIZE
#define IMAGE_WIDTH           CAMERA_MAIN_WIDTH
#define IMAGE_HEIGHT          CAMERA_MAIN_HEIGHT
#define IMAGE_BPP             CAMERA_MAIN_BPP
#define IMAGE_MAX_LINES       8U

/* JPEG configuration */
#define JPEG_USE_HPDMA        1U
#define JPEG_BUFFER_SIZE      ((1024U + 512U) * 1024U)              /* 1.5MB max image size for 1080p @ 90 quality for JPEG */
#define JPEG_CHUNK_IN_SIZE    (16U * IMAGE_WIDTH * CAMERA_MAIN_BPP) /* TODO: Review: Only work with double BPP */
#define JPEG_CHUNK_OUT_SIZE   (4U * 1024U)
#define JPEG_CHROMA_SAMPLING  JPEG_444_SUBSAMPLING                  /* JPEG_420_SUBSAMPLING, JPEG_422_SUBSAMPLING, JPEG_444_SUBSAMPLING   */
#define JPEG_COLOR_SPACE      JPEG_YCBCR_COLORSPACE                 /* JPEG_YCBCR_COLORSPACE, JPEG_GRAYSCALE_COLORSPACE, JPEG_CMYK_COLORSPACE */
#define JPEG_IMAGE_QUALITY    90U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

typedef JPEG_RGBToYCbCr_Convert_Function JPEG_RGBToYCbCr_Fn;

typedef enum
{
  BUFFER_EMPTY = 0,
  BUFFER_FULL,
} t_jpeg_state;

typedef struct
{
  uint8_t state;
  uint8_t *buff;
  size_t  size;
} t_jpeg_chunk;

/** JPEG task handler */
typedef struct
{
  TX_EVENT_FLAGS_GROUP  evt;
  TX_THREAD             thread;
  uint8_t               stack[JPEG_TASK_STACK_SIZE];
} t_jpeg_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/* Task handler */
static t_jpeg_task    _jpeg_task = { 0 };

/* Peripheral */
JPEG_HandleTypeDef    hjpeg;
JPEG_ConfTypeDef      jpeg_config;
JPEG_RGBToYCbCr_Fn    jpeg_rgb_to_ycbcr_fn;

#if JPEG_USE_HPDMA == 1U
DMA_HandleTypeDef     hhpdma1_ch0;
DMA_HandleTypeDef     hhpdma1_ch1;
#endif /* JPEG_USE_HPDMA */

/* Internals */
static uint8_t        _img_buff[CAMERA_MAIN_BUFFER_SIZE] DMA_ALIGN IN_PSRAM;
static uint32_t       _img_idx;

static uint8_t        _jpeg_buff[JPEG_BUFFER_SIZE] DMA_ALIGN IN_PSRAM;
static uint8_t        _jpeg_chunk_in_buff[JPEG_CHUNK_IN_SIZE] DMA_ALIGN;
static t_jpeg_chunk   _jpeg_chunk_in = {BUFFER_EMPTY, _jpeg_chunk_in_buff, 0};
static uint8_t        _jpeg_chunk_out_buff[JPEG_CHUNK_OUT_SIZE] DMA_ALIGN;
static t_jpeg_chunk   _jpeg_chunk_out = {BUFFER_EMPTY, _jpeg_chunk_out_buff , 0};

static uint8_t        *_jpeg_head;
static uint32_t       _jpeg_block_num   = 0;
static uint32_t       _jpeg_block_idx   = 0;
static __IO uint32_t  _jpeg_ready       = 0;
static __IO uint32_t  _jpeg_in_paused   = 0;
static __IO uint32_t  _jpeg_out_paused  = 0;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _jpeg_task_run(uint32_t args);
static void _jpeg_task_init(void);

static void _jpeg_encode_start(JPEG_HandleTypeDef *hjpeg);
static void _jpeg_encode_input_handler(JPEG_HandleTypeDef *hjpeg);
static bool _jpeg_encode_output_handler(JPEG_HandleTypeDef *hjpeg);

extern void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg);
extern void HAL_JPEG_MspDeInit(JPEG_HandleTypeDef *hjpeg);
extern void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t encoded);
extern void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg, uint8_t *buff, uint32_t size);
extern void HAL_JPEG_EncodeCpltCallback(JPEG_HandleTypeDef *hjpeg);
extern void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg);
extern void JPEG_IRQHandler(void);

#if JPEG_USE_HPDMA == 1U
extern void HPDMA1_Channel0_IRQHandler(void);
extern void HPDMA1_Channel1_IRQHandler(void);
#endif /* JPEG_USE_HPDMA */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t jpeg_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_jpeg_task.thread, "tx.jpeg.display",
    _jpeg_task_run, 0,
    _jpeg_task.stack, JPEG_TASK_STACK_SIZE,
    JPEG_TASK_PRIO, JPEG_TASK_PRIO,
    JPEG_TASK_TIME_SLICE, TX_AUTO_START
  );
}

void jpeg_configure(t_draw *draw)
{
  draw_copy_to_hw(
    draw, DMA2D_M2M, draw->output_mode,
    0, 0, draw->width, draw->height,
    _img_buff
  );
}

uint8_t *jpeg_encode(size_t* size)
{
  rtos_raise_event(&_jpeg_task.evt, JPEG_EVT_ENCODE);
  rtos_wait_any_event(&_jpeg_task.evt, JPEG_EVT_READY, true);
  *size = (size_t)(_jpeg_head - _jpeg_buff);
  return _jpeg_buff;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief JPEG task entry point.
 * @param args Task arguments
 */
static void _jpeg_task_run(uint32_t args)
{
  bool ready;

  UNUSED(args);

  /* Initialize task */
  _jpeg_task_init();

  /* JPEG task */
  while (1)
  {
    /* Wait for encode signal */
    rtos_wait_any_event(&_jpeg_task.evt, JPEG_EVT_ENCODE, true);
    stat_time_start(STAT_TIME_ENC_JPEG);

    /* Run JPEG encoding */
    ready = false;
    _jpeg_encode_start(&hjpeg);
    do
    {
      tx_thread_sleep(1);
      _jpeg_encode_input_handler(&hjpeg);
      ready = _jpeg_encode_output_handler(&hjpeg);
    } while (!ready);

    /* Encoding ready! */
    stat_time_stop(STAT_TIME_ENC_JPEG);
    rtos_raise_event(&_jpeg_task.evt, JPEG_EVT_READY);
  }
}

/**
 * @brief JPEG task initialization.
 */
static void _jpeg_task_init(void)
{
  int32_t status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_JPEG_REQUIRE);

  /*-->> INITIALIZE <<--*/
  status = tx_event_flags_create(&_jpeg_task.evt, "tx.evt.jpeg");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize hardware */
  hjpeg.Instance = JPEG;
  status = HAL_JPEG_Init(&hjpeg);
  if (status != HAL_OK)
  {
    LERROR(TRACE_JPEG, "JPEG init failed");
    Error_Handler();
  }

  /* Initialize The JPEG Color Look Up Tables used for YCbCr to RGB conversion */
  JPEG_InitColorTables();

  /*-->> READY <<--*/
  LINFO(TRACE_JPEG, "Task started");
  task_raise_event(TX_EVT_JPEG_READY);
}

/**
 * @brief Starts a new JPEG Encode operation.
 */
void _jpeg_encode_start(JPEG_HandleTypeDef *hjpeg)
{
  uint32_t chunk_size;

  /* Reset all Global variables */
  _jpeg_head         = _jpeg_buff;
  _jpeg_block_num   = 0;
  _jpeg_block_idx   = 0;
  _jpeg_ready   = 0;
  _jpeg_out_paused  = 0;
  _jpeg_in_paused   = 0;

  /* Get RGB Info */
  /* Read Images Sizes */
  jpeg_config.ImageWidth        = IMAGE_WIDTH;
  jpeg_config.ImageHeight       = IMAGE_HEIGHT;

  /* Jpeg Encoding Setting to be set by users */
  jpeg_config.ChromaSubsampling = JPEG_CHROMA_SAMPLING;
  jpeg_config.ColorSpace        = JPEG_COLOR_SPACE;
  jpeg_config.ImageQuality      = JPEG_IMAGE_QUALITY;

  /* Check if Image Sizes meets the requirements */
  if (
    ((jpeg_config.ImageWidth % 8U) != 0U) ||
    ((jpeg_config.ImageHeight % 8U) != 0U) ||
    (((jpeg_config.ImageWidth % 16U) != 0U) && (jpeg_config.ColorSpace == JPEG_YCBCR_COLORSPACE) && (jpeg_config.ChromaSubsampling != JPEG_444_SUBSAMPLING)) ||
    (((jpeg_config.ImageHeight % 16U) != 0U) && (jpeg_config.ColorSpace == JPEG_YCBCR_COLORSPACE) && (jpeg_config.ChromaSubsampling == JPEG_420_SUBSAMPLING))
  )
  {
    Error_Handler();
  }
  JPEG_GetEncodeColorConvertFunc(&jpeg_config, &jpeg_rgb_to_ycbcr_fn, &_jpeg_block_num);

  /* Clear Output Buffer */
  _jpeg_chunk_out.size = 0;
  _jpeg_chunk_out.state = BUFFER_EMPTY;

  /* Fill input Buffers */
  _img_idx   = 0;
  chunk_size = jpeg_config.ImageWidth * IMAGE_BPP * IMAGE_MAX_LINES;
  if (_img_idx < IMAGE_SIZE)
  {
    /* Pre-Processing */
    _jpeg_block_idx += jpeg_rgb_to_ycbcr_fn((uint8_t*)(_img_buff + _img_idx), _jpeg_chunk_in.buff, 0, chunk_size, (uint32_t*)(&_jpeg_chunk_in.size));
    _jpeg_chunk_in.state = BUFFER_FULL;
    _img_idx += chunk_size;
  }

  /* Fill Encoding Params */
  HAL_JPEG_ConfigEncoding(hjpeg, &jpeg_config);

  /* Start JPEG encoding */
  #if JPEG_USE_HPDMA == 1U
  SCB_CleanInvalidateDCache_by_Addr(_jpeg_chunk_in.buff, JPEG_CHUNK_IN_SIZE);
  HAL_JPEG_Encode_DMA(hjpeg, _jpeg_chunk_in.buff, _jpeg_chunk_in.size, _jpeg_chunk_out.buff, JPEG_CHUNK_OUT_SIZE);
  #else
  HAL_JPEG_Encode_IT(hjpeg, _jpeg_chunk_in.buff, _jpeg_chunk_in.size, _jpeg_chunk_out.buff, JPEG_CHUNK_OUT_SIZE);
  #endif /* JPEG_USE_HPDMA */
}

/**
 * @brief JPEG Input Data BackGround Preprocessing
 * @param hjpeg JPEG handler
 */
void _jpeg_encode_input_handler(JPEG_HandleTypeDef *hjpeg)
{
  uint32_t chunk_size = jpeg_config.ImageWidth * IMAGE_BPP * IMAGE_MAX_LINES;
  if ((_jpeg_chunk_in.state == BUFFER_EMPTY) && (_jpeg_block_idx <= _jpeg_block_num))
  {
    /* Read and reorder lines from RGB input and fill data buffer */
    if (_img_idx < IMAGE_SIZE)
    {
      /* Pre-Processing */
      _jpeg_block_idx += jpeg_rgb_to_ycbcr_fn((uint8_t*)(_img_buff + _img_idx), _jpeg_chunk_in.buff, 0, chunk_size, (uint32_t*)(&_jpeg_chunk_in.size));
      _jpeg_chunk_in.state = BUFFER_FULL;
      _img_idx += chunk_size;
      if (_jpeg_in_paused == 1)
      {
        _jpeg_in_paused = 0;
        #if JPEG_USE_HPDMA == 1U
        SCB_CleanInvalidateDCache_by_Addr(_jpeg_chunk_in.buff, JPEG_CHUNK_IN_SIZE);
        #endif /* JPEG_USE_HPDMA */
        HAL_JPEG_ConfigInputBuffer(hjpeg, _jpeg_chunk_in.buff, _jpeg_chunk_in.size);
        HAL_JPEG_Resume(hjpeg, JPEG_PAUSE_RESUME_INPUT);
      }
    }
    else
    {
      _jpeg_block_idx++;
    }
  }
}

/**
 * @brief JPEG Output Data BackGround processing
 * @param hjpeg JPEG handler
 * @retval true if finished, false otherwise
 */
bool _jpeg_encode_output_handler(JPEG_HandleTypeDef *hjpeg)
{
  if (_jpeg_chunk_out.state == BUFFER_FULL)
  {
    /* Copy encoded shunk from Jpeg_OUT_BufferTab to JpegBuffer */
    #if JPEG_USE_HPDMA == 1U
    SCB_CleanInvalidateDCache_by_Addr(_jpeg_chunk_out.buff, JPEG_CHUNK_OUT_SIZE);
    #endif /* JPEG_USE_HPDMA */
    memcpy(_jpeg_head, _jpeg_chunk_out.buff, _jpeg_chunk_out.size);
    _jpeg_head += _jpeg_chunk_out.size;
    _jpeg_chunk_out.state = BUFFER_EMPTY;
    _jpeg_chunk_out.size  = 0;
    if (_jpeg_ready != 0)
    {
      return true;
    }
    else if ((_jpeg_out_paused == 1) && (_jpeg_chunk_out.state == BUFFER_EMPTY))
    {
      _jpeg_out_paused = 0;
      HAL_JPEG_Resume(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
    }
 }
 return false;
}

/**
 * @brief Initializes the JPEG MSP
 * @param hjpeg JPEG handler
 */
void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg)
{
  DMA_IsolationConfigTypeDef  config;
  int32_t                     status;

  /* Initialize JPEG */
  if (hjpeg->Instance == JPEG)
  {
    /* Peripheral clock enable */
    __HAL_RCC_JPEG_CLK_ENABLE();

    #if JPEG_USE_HPDMA == 1U
    __HAL_RCC_HPDMA1_CLK_ENABLE();

    /* Init JPEG_RX DMA  */
    hhpdma1_ch0.Instance                  = HPDMA1_Channel0;
    hhpdma1_ch0.Init.Request              = HPDMA1_REQUEST_JPEG_RX;
    hhpdma1_ch0.Init.BlkHWRequest         = DMA_BREQ_SINGLE_BURST;
    hhpdma1_ch0.Init.Direction            = DMA_MEMORY_TO_PERIPH;
    hhpdma1_ch0.Init.SrcInc               = DMA_SINC_INCREMENTED;
    hhpdma1_ch0.Init.DestInc              = DMA_DINC_FIXED;
    hhpdma1_ch0.Init.SrcDataWidth         = DMA_SRC_DATAWIDTH_WORD;
    hhpdma1_ch0.Init.DestDataWidth        = DMA_DEST_DATAWIDTH_WORD;
    hhpdma1_ch0.Init.Priority             = DMA_LOW_PRIORITY_LOW_WEIGHT;
    hhpdma1_ch0.Init.SrcBurstLength       = 8;
    hhpdma1_ch0.Init.DestBurstLength      = 8;
    hhpdma1_ch0.Init.TransferAllocatedPort= DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1;
    hhpdma1_ch0.Init.TransferEventMode    = DMA_TCEM_BLOCK_TRANSFER;
    hhpdma1_ch0.Init.Mode                 = DMA_NORMAL;
    status = HAL_DMA_Init(&hhpdma1_ch0);
    if (status != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(hjpeg, hdmain, hhpdma1_ch0);
    status = HAL_DMA_ConfigChannelAttributes(&hhpdma1_ch0, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
    if (status != HAL_OK)
    {
      Error_Handler();
    }

    config.CidFiltering = DMA_ISOLATION_ON;
    config.StaticCid    = DMA_CHANNEL_STATIC_CID_1;
    status = HAL_DMA_SetIsolationAttributes(&hhpdma1_ch0, &config);
    if (status != HAL_OK)
    {
      Error_Handler();
    }

    /* Init JPEG_TX DMA */
    hhpdma1_ch1.Instance                  = HPDMA1_Channel1;
    hhpdma1_ch1.Init.Request              = HPDMA1_REQUEST_JPEG_TX;
    hhpdma1_ch1.Init.BlkHWRequest         = DMA_BREQ_SINGLE_BURST;
    hhpdma1_ch1.Init.Direction            = DMA_PERIPH_TO_MEMORY;
    hhpdma1_ch1.Init.SrcInc               = DMA_SINC_FIXED;
    hhpdma1_ch1.Init.DestInc              = DMA_DINC_INCREMENTED;
    hhpdma1_ch1.Init.SrcDataWidth         = DMA_SRC_DATAWIDTH_WORD;
    hhpdma1_ch1.Init.DestDataWidth        = DMA_DEST_DATAWIDTH_WORD;
    hhpdma1_ch1.Init.Priority             = DMA_LOW_PRIORITY_LOW_WEIGHT;
    hhpdma1_ch1.Init.SrcBurstLength       = 8;
    hhpdma1_ch1.Init.DestBurstLength      = 8;
    hhpdma1_ch1.Init.TransferAllocatedPort= DMA_SRC_ALLOCATED_PORT1|DMA_DEST_ALLOCATED_PORT0;
    hhpdma1_ch1.Init.TransferEventMode    = DMA_TCEM_BLOCK_TRANSFER;
    hhpdma1_ch1.Init.Mode                 = DMA_NORMAL;
    status = HAL_DMA_Init(&hhpdma1_ch1);
    if (status != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(hjpeg, hdmaout, hhpdma1_ch1);
    status = HAL_DMA_ConfigChannelAttributes(&hhpdma1_ch1, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
    if (status != HAL_OK)
    {
      Error_Handler();
    }

    config.CidFiltering = DMA_ISOLATION_ON;
    config.StaticCid    = DMA_CHANNEL_STATIC_CID_1;
    status = HAL_DMA_SetIsolationAttributes(&hhpdma1_ch1, &config);
    if (status != HAL_OK)
    {
      Error_Handler();
    }

    /* JPEG interrupt Init */
    HAL_NVIC_SetPriority(HPDMA1_Channel0_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel0_IRQn);
    HAL_NVIC_SetPriority(HPDMA1_Channel1_IRQn, 7U, 0U);
    HAL_NVIC_EnableIRQ(HPDMA1_Channel1_IRQn);
    #endif /* JPEG_USE_HPDMA */
    HAL_NVIC_SetPriority(JPEG_IRQn, 8U, 0U);
    HAL_NVIC_EnableIRQ(JPEG_IRQn);
  }
}

/**
 * @brief DeInitializes JPEG MSP
 * @param hjpeg JPEG handler
 */
void HAL_JPEG_MspDeInit(JPEG_HandleTypeDef *hjpeg)
{
  if (hjpeg->Instance==JPEG)
  {
    /* Disable clocks */
    __HAL_RCC_JPEG_CLK_DISABLE();

    #if JPEG_USE_HPDMA == 1U
    /* Deinit JPEG DMA */
    HAL_DMA_DeInit(hjpeg->hdmain);
    HAL_DMA_DeInit(hjpeg->hdmaout);
    #endif /* JPEG_USE_HPDMA */

    /* Stop JPEG interrupts */
    HAL_NVIC_DisableIRQ(JPEG_IRQn);
  }
}

/**
 * @brief JPEG Get Data callback
 * @param hjpeg JPEG handler
 * @param encoded Number of encoded (consumed) bytes from buffer
 */
void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hjpeg, uint32_t encoded)
{
  if (encoded == _jpeg_chunk_in.size)
  {
    _jpeg_chunk_in.state = BUFFER_EMPTY;
    _jpeg_chunk_in.size = 0;

    HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_INPUT);
    _jpeg_in_paused = 1;
  }
  else
  {
    HAL_JPEG_ConfigInputBuffer(hjpeg, _jpeg_chunk_in.buff + encoded, _jpeg_chunk_in.size - encoded);
  }
}

/**
 * @brief JPEG Data Ready callback
 * @param hjpeg JPEG handler
 * @param buff Output data buffer
 * @param size Output buffer size in bytes
 */
void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hjpeg, uint8_t *buff, uint32_t size)
{
  _jpeg_chunk_out.state = BUFFER_FULL;
  _jpeg_chunk_out.size = size;

  HAL_JPEG_Pause(hjpeg, JPEG_PAUSE_RESUME_OUTPUT);
  _jpeg_out_paused = 1;

  HAL_JPEG_ConfigOutputBuffer(hjpeg, _jpeg_chunk_out.buff, JPEG_CHUNK_OUT_SIZE);
}

/**
 * @brief  Encoding complete callback
 * @param  hjpeg JPEG handler
 */
void HAL_JPEG_EncodeCpltCallback(JPEG_HandleTypeDef *hjpeg)
{
  _jpeg_ready = 1;
}

/**
 * @brief  JPEG error callback
 * @param  hjpeg JPEG handler
 */
void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hjpeg)
{
  Error_Handler();
}

/**
 * @brief This function handles JPEG global interrupt.
 */
void JPEG_IRQHandler(void)
{
  HAL_JPEG_IRQHandler(&hjpeg);
}

#if JPEG_USE_HPDMA == 1U
/**
 * @brief This function handles HPDMA1 Channel 0 global interrupt.
 */
void HPDMA1_Channel0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hhpdma1_ch0);
}

/**
 * @brief This function handles HPDMA1 Channel 1 global interrupt.
 */
void HPDMA1_Channel1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hhpdma1_ch1);
}
#endif /* JPEG_USE_HPDMA */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: JPEG -->
*//*--------------------------------------------------------------------------*/
