/**
 ******************************************************************************
 * @file    draw.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Drawing utilities for the N6Cam firmware.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT 2024 SIANA Systems</center></h2>
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "draw.h"
#include "n6cam_rtos.h"
#include "slib32_core.h"
#include "stm32n6xx_hal.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Utilities
* @{
* @addtogroup Draw
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

#define DRAW_TEXT_COLOR       COLOR_WHITE
#define DRAW_TEXT_BACKGROUND  COLOR_WITH_ALPHA(COLOR_BLACK, ALPHA(40U))

#define DRAW_MAX_LINE_CHAR    64U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define DRAW_EVT_ALL          0xFFFFFFFFU
#define DRAW_EVT_READY        BIT(0U)

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static DMA2D_HandleTypeDef  *_draw_hdma2d = NULL;
static bool                 _draw_ready   = false;
static TX_SEMAPHORE         _draw_sem;
static TX_EVENT_FLAGS_GROUP _draw_evt;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void     _draw_hline_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t len, uint32_t color);
static void     _draw_vline_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t len, uint32_t color);

static void     _draw_font_cvt(t_font *src, uint32_t *dst, uint8_t *din);
static uint32_t _draw_font_puts_hw(t_draw *dst, t_draw_font *font, uint32_t xpos, uint32_t ypos, char *line);

static void     _draw_lock(DMA2D_HandleTypeDef *hdma2d);
static void     _draw_unlock(void);
static void     _draw_dma2d_cplt_fn(DMA2D_HandleTypeDef *hdma2d);
static void     _draw_dma2d_error_fn(DMA2D_HandleTypeDef *hdma2d);

extern void     DMA2D_IRQHandler(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t draw_init(void)
{
  int32_t status;

  /* Validate */
  if (_draw_ready)
  {
    return 0;
  }

  /* Configure DMA2D */
  __HAL_RCC_DMA2D_CLK_ENABLE();
  __HAL_RCC_DMA2D_FORCE_RESET();
  __HAL_RCC_DMA2D_RELEASE_RESET();

  /* Initialize */
  status = tx_semaphore_create(&_draw_sem, "tx.sem.draw", 1U);
  if (status != TX_SUCCESS)
  {
    return -1;
  }
  status = tx_event_flags_create(&_draw_evt, "tx.evt.draw");
  if (status != TX_SUCCESS)
  {
    return -1;
  }
  _draw_ready = true;
  return 0;
}

int32_t draw_font_init(t_draw_font *font, t_font *src)
{
  /* Assume ARGB chars */
  const uint32_t count    = '~' - ' ' + 1U;
  uint32_t       csize_in = src->Height * ((src->Width + 7U) / 8U);

  /* Create font buffer */
  font->width = src->Width;
  font->height= src->Height;
  font->csize = src->Height * src->Width * 4U;
  font->data  = malloc(count * font->csize);
  if (!font->data)
  {
    return -1;
  }

  /* Draw fonts */
  for (size_t idx = 0; idx < count; idx++)
  {
    _draw_font_cvt(src, (uint32_t*)&font->data[idx * font->csize], (uint8_t*)&src->table[idx * csize_in]);
  }
  return 0;
}

void draw_copy_to_hw(t_draw *src, uint32_t dma_mode, uint32_t color_mode, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint8_t *dst)
{
  DMA2D_HandleTypeDef hdma2d = { 0 };
  HAL_StatusTypeDef   status;
  uint32_t            address;

  /* Validate */
  if (!_draw_ready || !src || !dst || (width == 0) || (height == 0))
  {
    return;
  }
  if (!src->buff)
  {
    return;
  }

  /* Prepare instance */
  hdma2d.Instance                   = DMA2D;
  hdma2d.Init.Mode                  = dma_mode;
  hdma2d.Init.ColorMode             = color_mode;
  hdma2d.Init.OutputOffset          = width - src->width;

  /* Prepare background (dst) */
  hdma2d.LayerCfg[0].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[0].AlphaInverted  = DMA2D_REGULAR_ALPHA;
  hdma2d.LayerCfg[0].InputAlpha     = 0xFFU;
  hdma2d.LayerCfg[0].InputColorMode = color_mode;
  hdma2d.LayerCfg[0].InputOffset    = width - src->width;
  hdma2d.LayerCfg[0].RedBlueSwap    = DMA2D_RB_REGULAR;

  /* Prepare foreground (src) */
  hdma2d.LayerCfg[1].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[1].AlphaInverted  = DMA2D_REGULAR_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha     = 0xFFU;
  hdma2d.LayerCfg[1].InputColorMode = src->output_mode;
  hdma2d.LayerCfg[1].InputOffset    = 0x00U;
  hdma2d.LayerCfg[1].RedBlueSwap    = DMA2D_RB_REGULAR;

  /* Capture and init */
  _draw_lock(&hdma2d);
  status = HAL_DMA2D_Init(&hdma2d);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  status = HAL_DMA2D_ConfigLayer(&hdma2d, 1);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  status = HAL_DMA2D_ConfigLayer(&hdma2d, 0);
  if (status != HAL_OK)
  {
    Error_Handler();
  }

  /* Set control callbacks */
  hdma2d.XferCpltCallback  = _draw_dma2d_cplt_fn;
  hdma2d.XferErrorCallback = _draw_dma2d_error_fn;

  /* Start process */
  HAL_NVIC_EnableIRQ(DMA2D_IRQn);
  address = (uint32_t) (dst + ((width * ypos + xpos) * src->bpp));
  switch (dma_mode)
  {
    case DMA2D_M2M:
      status = HAL_DMA2D_Start_IT(&hdma2d, (uint32_t)src->buff, address, width, height);
      break;

    case DMA2D_M2M_BLEND:
      status = HAL_DMA2D_BlendingStart_IT(&hdma2d, (uint32_t)src->buff, address, address, width, height);
      break;

    default:
      HAL_NVIC_DisableIRQ(DMA2D_IRQn);
      status = HAL_DMA2D_DeInit(&hdma2d);
      return;
  }
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  rtos_wait_any_event(&_draw_evt, DRAW_EVT_READY, true);

  /* Handle process ready */
  HAL_NVIC_DisableIRQ(DMA2D_IRQn);
  status = HAL_DMA2D_DeInit(&hdma2d);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  _draw_unlock();
}

void draw_copy_from_hw(t_draw *dst, uint32_t dma_mode, uint32_t color_mode, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint8_t *src)
{
  DMA2D_HandleTypeDef hdma2d = { 0 };
  HAL_StatusTypeDef   status;
  uint32_t            address;

  /* Validate */
  if (!_draw_ready || !src || !dst || (width == 0) || (height == 0))
  {
    return;
  }
  if (!dst->buff)
  {
    return;
  }

  /* Prepare instance */
  hdma2d.Instance                   = DMA2D;
  hdma2d.Init.Mode                  = dma_mode;
  hdma2d.Init.ColorMode             = dst->output_mode;
  hdma2d.Init.OutputOffset          = dst->width - width;

  /* Prepare background (dst) */
  hdma2d.LayerCfg[0].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[0].AlphaInverted  = DMA2D_REGULAR_ALPHA;
  hdma2d.LayerCfg[0].InputAlpha     = 0xFFU;
  hdma2d.LayerCfg[0].InputColorMode = dst->input_mode;
  hdma2d.LayerCfg[0].InputOffset    = dst->width - width;
  hdma2d.LayerCfg[0].RedBlueSwap    = DMA2D_RB_REGULAR;

  /* Prepare foreground (src) */
  hdma2d.LayerCfg[1].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[1].AlphaInverted  = DMA2D_REGULAR_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha     = 0xFFU;
  hdma2d.LayerCfg[1].InputColorMode = color_mode;
  hdma2d.LayerCfg[1].InputOffset    = 0x00U;
  hdma2d.LayerCfg[1].RedBlueSwap    = DMA2D_RB_SWAP;

  /* Capture and init */
  _draw_lock(&hdma2d);
  status = HAL_DMA2D_Init(&hdma2d);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  status = HAL_DMA2D_ConfigLayer(&hdma2d, 1);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  status = HAL_DMA2D_ConfigLayer(&hdma2d, 0);
  if (status != HAL_OK)
  {
    Error_Handler();
  }

  /* Set control callbacks */
  hdma2d.XferCpltCallback  = _draw_dma2d_cplt_fn;
  hdma2d.XferErrorCallback = _draw_dma2d_error_fn;

  /* Start process */
  HAL_NVIC_EnableIRQ(DMA2D_IRQn);
  address = (uint32_t) (dst->buff + ((dst->width * ypos + xpos) * dst->bpp));
  switch (dma_mode)
  {
    case DMA2D_M2M:
      status = HAL_DMA2D_Start_IT(&hdma2d, (uint32_t)src, address, width, height);
      break;

    case DMA2D_M2M_BLEND:
      status = HAL_DMA2D_BlendingStart_IT(&hdma2d, (uint32_t) src, address, address, width, height);
      break;

    default:
      HAL_NVIC_DisableIRQ(DMA2D_IRQn);
      status = HAL_DMA2D_DeInit(&hdma2d);
      return;
  }
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  rtos_wait_any_event(&_draw_evt, DRAW_EVT_READY, true);

  /* Handle process ready */
  HAL_NVIC_DisableIRQ(DMA2D_IRQn);
  status = HAL_DMA2D_DeInit(&hdma2d);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  _draw_unlock();
}

void draw_fill_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint32_t color)
{
  DMA2D_HandleTypeDef hdma2d = { 0 };
  HAL_StatusTypeDef   status;
  uint32_t            address;

  /* Validate */
  if (!_draw_ready || !dst || (width == 0) || (height == 0))
  {
    return;
  }
  if (!dst->buff)
  {
    return;
  }

  /* Prepare instance */
  hdma2d.Instance                   = DMA2D;
  hdma2d.Init.Mode                  = DMA2D_M2M_BLEND_FG;
  hdma2d.Init.ColorMode             = dst->output_mode;
  hdma2d.Init.OutputOffset          = dst->width - width;

  /* Prepare background (dst) */
  hdma2d.LayerCfg[0].AlphaMode      = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[0].AlphaInverted  = DMA2D_REGULAR_ALPHA;
  hdma2d.LayerCfg[0].InputAlpha     = 0xFFU;
  hdma2d.LayerCfg[0].InputColorMode = dst->input_mode;
  hdma2d.LayerCfg[0].InputOffset    = dst->width - width;
  hdma2d.LayerCfg[0].RedBlueSwap    = DMA2D_RB_REGULAR;

  /* Prepare foreground (src) */
  hdma2d.LayerCfg[1].AlphaMode      = DMA2D_REPLACE_ALPHA;
  hdma2d.LayerCfg[1].AlphaInverted  = DMA2D_REGULAR_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha     = 0xFF & (color >> 24U);
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_ARGB8888;
  hdma2d.LayerCfg[1].InputOffset    = 0x00U;
  hdma2d.LayerCfg[1].RedBlueSwap    = DMA2D_RB_REGULAR;

  /* Capture and init */
  _draw_lock(&hdma2d);
  status = HAL_DMA2D_Init(&hdma2d);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  status = HAL_DMA2D_ConfigLayer(&hdma2d, 1);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  status = HAL_DMA2D_ConfigLayer(&hdma2d, 0);
  if (status != HAL_OK)
  {
    Error_Handler();
  }

  /* Set control callbacks */
  hdma2d.XferCpltCallback  = _draw_dma2d_cplt_fn;
  hdma2d.XferErrorCallback = _draw_dma2d_error_fn;

  /* Run process */
  HAL_NVIC_EnableIRQ(DMA2D_IRQn);
  address = (uint32_t) (dst->buff + ((dst->width * ypos + xpos) * dst->bpp));
  status  = HAL_DMA2D_BlendingStart_IT(&hdma2d, color, address, address, width, height);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  rtos_wait_any_event(&_draw_evt, DRAW_EVT_READY, true);

  /* Handle process ready */
  HAL_NVIC_DisableIRQ(DMA2D_IRQn);
  status = HAL_DMA2D_DeInit(&hdma2d);
  if (status != HAL_OK)
  {
    Error_Handler();
  }
  _draw_unlock();
}

void draw_rect_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint32_t color)
{
  _draw_hline_hw(dst, xpos            , ypos             , width  , color);
  _draw_hline_hw(dst, xpos            , ypos + height - 1, width  , color);
  _draw_vline_hw(dst, xpos            , ypos             , height , color);
  _draw_vline_hw(dst, xpos + width - 1, ypos             , height , color);
}

void draw_printf_hw(t_draw *dst, t_draw_font *font, uint32_t xpos, uint32_t ypos, uint32_t width, const char *format, ...)
{
  va_list  args;
  uint32_t cursor;
  char     line[DRAW_MAX_LINE_CHAR + 1] = { 0 };

  /* Validate */
  if (!_draw_ready || !dst || !font)
  {
    return;
  }

  /* Prepare text */
  va_start(args, format);
  vsnprintf(line, DRAW_MAX_LINE_CHAR, format, args);
  va_end(args);

  /* Perform drawing */
  cursor = _draw_font_puts_hw(dst, font, xpos, ypos, line);
  if ((cursor - xpos) < width)
  {
    draw_fill_hw(dst, cursor, ypos, width - (cursor - xpos), font->height, DRAW_TEXT_BACKGROUND);
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _draw_hline_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t len, uint32_t color)
{
  draw_fill_hw(dst, xpos, ypos, len, 1, color);
}

static void _draw_vline_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t len, uint32_t color)
{
  draw_fill_hw(dst, xpos, ypos, 1, len, color);
}

static void _draw_font_cvt(t_font *src, uint32_t *dout, uint8_t *din)
{
  uint8_t  *pchar;
  uint32_t line;
  uint32_t width  = src->Width;
  uint32_t height = src->Height;
  uint32_t scale  = (width + 7U) / 8U;
  uint32_t offset = 8U * scale - width;
  for(uint32_t idx = 0; idx < height; idx++)
  {
    pchar = (din + scale * idx);
    switch(scale)
    {
      case 1:
        line =  pchar[0];
        break;

      case 2:
        line =  (pchar[0] << 8) | pchar[1];
        break;

      case 3:
      default:
        line =  (pchar[0] << 16) | (pchar[1] << 8) | pchar[2];
        break;
    }
    for (uint32_t j = 0; j < width; j++)
    {
      if (line & (1 << (width - j + offset - 1)))
      {
        *dout++ = DRAW_TEXT_COLOR;
      }
      else
      {
        *dout++ = DRAW_TEXT_BACKGROUND;
      }
    }
  }
}

static uint32_t _draw_font_puts_hw(t_draw *dst, t_draw_font *font, uint32_t xpos, uint32_t ypos, char *line)
{
  while (*line != '\0')
  {
    draw_copy_from_hw(dst, DMA2D_M2M_BLEND, DMA2D_INPUT_ARGB8888, xpos, ypos, font->width, font->height, &font->data[(*line - ' ') * font->csize]);
    xpos += font->width;
    line++;
  }
  return xpos;
}

static void _draw_lock(DMA2D_HandleTypeDef *hdma2d)
{
  rtos_semaphore_acquire(&_draw_sem, true);
  _draw_hdma2d = hdma2d;
}

static void _draw_unlock(void)
{
  _draw_hdma2d = NULL;
  rtos_semaphore_acquire(&_draw_sem, false);
}

static void _draw_dma2d_cplt_fn(DMA2D_HandleTypeDef *hdma2d)
{
  rtos_raise_event(&_draw_evt, DRAW_EVT_READY);
}

static void _draw_dma2d_error_fn(DMA2D_HandleTypeDef *hdma2d)
{
  Error_Handler();
}

void DMA2D_IRQHandler(void)
{
  HAL_DMA2D_IRQHandler(_draw_hdma2d);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Utilities -->
* @} <!-- End: Draw -->
*//*--------------------------------------------------------------------------*/
