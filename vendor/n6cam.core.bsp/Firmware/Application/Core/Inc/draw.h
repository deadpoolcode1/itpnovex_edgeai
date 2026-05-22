/**
 ******************************************************************************
 * @file    draw.h
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
#ifndef _DRAW_H_
#define _DRAW_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "fonts.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Utilities
* @{
* @addtogroup Draw
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* Common alpha definitions */
#define ALPHA_0             0x00UL   /*   0% */
#define ALPHA_25            0x3FUL   /*  25% */
#define ALPHA_50            0x7FUL   /*  50% */
#define ALPHA_75            0xBFUL   /*  75% */
#define ALPHA_100           0xFFUL   /* 100% */

/* Common color definitions (ARGB) */
#define COLOR_BLACK         0xFF000000UL
#define COLOR_BLUE          0xFF0000FFUL
#define COLOR_BROWN         0xFFA52A2AUL
#define COLOR_CYAN          0xFF00FFFFUL
#define COLOR_DARKBLUE      0xFF000080UL
#define COLOR_DARKCYAN      0xFF008080UL
#define COLOR_DARKGRAY      0xFF404040UL
#define COLOR_DARKGREEN     0xFF008000UL
#define COLOR_DARKMAGENTA   0xFF800080UL
#define COLOR_DARKRED       0xFF800000UL
#define COLOR_DARKYELLOW    0xFF808000UL
#define COLOR_GRAY          0xFF808080UL
#define COLOR_GREEN         0xFF00FF00UL
#define COLOR_LIGHTBLUE     0xFF8080FFUL
#define COLOR_LIGHTCYAN     0xFF80FFFFUL
#define COLOR_LIGHTGRAY     0xFFD3D3D3UL
#define COLOR_LIGHTGREEN    0xFF80FF80UL
#define COLOR_LIGHTMAGENTA  0xFFFF80FFUL
#define COLOR_LIGHTRED      0xFFFF8080UL
#define COLOR_LIGHTYELLOW   0xFFFFFF80UL
#define COLOR_MAGENTA       0xFFFF00FFUL
#define COLOR_ORANGE        0xFFFFA500UL
#define COLOR_RED           0xFFFF0000UL
#define COLOR_WHITE         0xFFFFFFFFUL
#define COLOR_YELLOW        0xFFFFFF00UL

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Alpha from percentage */
#ifndef ALPHA
  #define ALPHA(level) \
    ((ALPHA_100 * (level)) / 100UL)
#endif

/** Apply alpha to color */
#ifndef COLOR_WITH_ALPHA
  #define COLOR_WITH_ALPHA(color, alpha) \
    (((alpha) << 24) | (0x00FFFFFFUL & (color)))
#endif

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Font instance */
typedef sFONT t_font;

/** Draw font instance */
typedef struct
{
  uint8_t  *data;
  uint16_t width;
  uint16_t height;
  uint32_t csize;
} t_draw_font;

/** Draw instance */
typedef struct
{
  uint8_t  *buff;
  uint32_t width;
  uint32_t height;
  uint32_t input_mode;
  uint32_t output_mode;
  uint8_t  bpp;
} t_draw;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t draw_init(void);
int32_t draw_font_init(t_draw_font *font, t_font *src);

void draw_copy_to_hw(t_draw *src, uint32_t dma_mode, uint32_t color_mode, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint8_t *dst);
void draw_copy_from_hw(t_draw *dst, uint32_t dma_mode, uint32_t color_mode, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint8_t *src);

void draw_fill_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint32_t color);
void draw_rect_hw(t_draw *dst, uint32_t xpos, uint32_t ypos, uint32_t width, uint32_t height, uint32_t color);

void draw_printf_hw(t_draw *dst, t_draw_font *font, uint32_t xpos, uint32_t ypos, uint32_t width, const char *format, ...);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Utilities -->
* @} <!-- End: Draw -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _DRAW_H_ */
