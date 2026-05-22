/**
 ******************************************************************************
 * @file    sai_transform_utils.c
 * @author  SIANA Systems
 * @date    06/2025
 * @brief   Utility functions for transform pre-processing in AI applications
 *
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

/** @addtogroup SIANA
 * @{
 */
/** @addtogroup Controllers
 * @{
 */
/** @addtogroup AI
 * @{
 */
/**
 * @defgroup PUBLIC_Definitions          PUBLIC constants
 * @defgroup PUBLIC_Macros               PUBLIC macros
 * @defgroup PUBLIC_Types                PUBLIC data-types
 * @defgroup PUBLIC_API                  PUBLIC API
 * @defgroup PUBLIC_WEAK                 PUBLIC Weak API
 * @defgroup PRIVATE_TUNABLES            Private compile-time tunables
 * @defgroup PRIVATE_Definitions         Private constants
 * @defgroup PRIVATE_Macros              Private macros
 * @defgroup PRIVATE_Types               Private data-types
 * @defgroup PRIVATE_Data                Private data / variables
 * @defgroup PRIVATE_Functions           Private functions
 * @defgroup PRIVATE_Weak                Private Weak functions
 */

#include <stdlib.h>
#include <stdint.h>

#include "app_config.h"
#include "sai_transform_utils.h"

/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_TUNABLES                                              */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Definitions                                           */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Macros                                                */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Types                                                 */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Functions                                             */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Data                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_API                                                    */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/* API: Converts a Rectangle defined in Center-Width-Height format to 
 *          Top-Left-Width-Height format
 */
int8_t sai_transform_cwh_to_tlwh(const t_frect * src, t_frect * dst)
{

  /* Sanity Check */
  if (src == NULL || dst == NULL) 
  { 
    LERROR(TRACE_DISPLAY, "Invalid parameters");
    return 1; 
  }
  /* Convert CWH to TLWH */
  t_frect tmp = {
    .x = src->x - src->width / 2,
    .y = src->y - src->height / 2,
    .width = src->width,
    .height = src->height
  };
  /* Copy to destination */
  dst->x = tmp.x;
  dst->y = tmp.y;
  dst->width = tmp.width;
  dst->height = tmp.height;
  return 0;
}

/* API: Scales a TLWH rectangle from an original frame of reference to a destiny
 frame of reference. */
int8_t sai_transform_scale(const t_frect * src, t_frect * dst, const t_frect * ref_orig, const t_frect * ref_dest)
{

  /* Sanity check */
  if (src == NULL || dst == NULL || ref_orig == NULL || ref_dest == NULL) 
  { 
    LERROR(TRACE_DISPLAY, "Invalid parameters");
    return 1; 
  }

  t_frect tmp = {
    .x = src->x * ref_dest->width / ref_orig->width,
    .y = src->y * ref_dest->height / ref_orig->height,
    .width = src->width * ref_dest->width / ref_orig->width,
    .height = src->height * ref_dest->height / ref_orig->height
  };

  /* Copy to destination */
  dst->x = tmp.x;
  dst->y = tmp.y;
  dst->width = tmp.width;
  dst->height = tmp.height;
  return 0;

}

/* API:  Clips a TLWH rectangle to a given frame of reference */
int8_t sai_transform_clip(const t_frect * src, t_frect * dst, const t_frect * ref)
{

  /* Sanity Check */
  if (src == NULL || dst == NULL || ref == NULL) 
  { 
    LERROR(TRACE_DISPLAY, "Invalid parameters");
    return 1; 
  }


  t_frect   tmp = {
    .x = src->x,
    .y = src->y,
    .width = src->width,
    .height = src->height
  };

  float32_t ref_x2 = ref->x + ref->width;
  float32_t ref_y2 = ref->y + ref->height;

  tmp.x      = (tmp.x < ref->x) ? ref->x : tmp.x;
  tmp.y      = (tmp.y < ref->y) ? ref->y : tmp.y;
  tmp.x      = (tmp.x >= ref_x2) ? (ref_x2 - 1) : tmp.x;
  tmp.y      = (tmp.y >= ref_y2) ? (ref_y2 - 1) : tmp.y;

  tmp.width  = ((tmp.x + tmp.width) > ref_x2) ? (ref_x2 - tmp.x) : tmp.width;
  tmp.height = ((tmp.y + tmp.height) > ref_y2) ? (ref_y2 - tmp.y) : tmp.height;

  /* Copy to destination */
  dst->x = tmp.x;
  dst->y = tmp.y;
  dst->width = tmp.width;
  dst->height = tmp.height;
  return 0;
}

/* API:  Change the frame of reference used to define a TLWH rectangle */
int8_t sai_transform_reframe(const t_frect * src, t_frect * dst, const t_frect * ref_orig, const t_frect * ref_dest)
{
  /* Sanity Check */
  if (src == NULL || dst == NULL || ref_orig == NULL || ref_dest == NULL) 
  { 
    LERROR(TRACE_DISPLAY, "Invalid parameters");
    return 1; 
  }

  t_frect   tmp = {
    .x = src->x - ref_orig->x + ref_dest->x,
    .y = src->y - ref_orig->y + ref_dest->y,
    .width = src->width,
    .height = src->height
  };
  /* Copy to destination */
  dst->x = tmp.x;
  dst->y = tmp.y;
  dst->width = tmp.width;
  dst->height = tmp.height;
  return 0;
}

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_WEAK                                                   */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Functions                                             */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PRIVATE_Weak                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/** @} */
/*--->> END: Private Functions <<---------------------------------------------*/

/** @} */
/** @} */
/** @} */
