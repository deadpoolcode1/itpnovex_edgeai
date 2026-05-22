/**
 ******************************************************************************
 * @file    sai_transform_utils.h
 * @author  SIANA Systems
 * @date    06/2025
 * @brief   Utility functions for transform preprocessing.
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
#ifndef _INC_SAI_TRANSFORM_UTILS_H_
#define _INC_SAI_TRANSFORM_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup SIANA
 * @{
 */
/** @addtogroup Controllers
 * @{
 */
/** @addtogroup AI
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "arm_math.h"

/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Definitions                                            */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Types                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** Generic float rectangle */
typedef struct
{
  float32_t x;
  float32_t y;
  float32_t width;
  float32_t height;
} t_frect;


/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_Data                                                  */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_API                                                    */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/**
 * @brief  Converts a Rectangle defined in Center-Width-Height format to 
 *          Top-Left-Width-Height format
 * @param src Source rectangle
 * @param dst Destiny rectangle
 * @return 0 if success, 1 otherwise
 */
int8_t sai_transform_cwh_to_tlwh(const t_frect * src, t_frect * dst);

/**
 * @brief  Scales a TLWH rectangle from an original frame of reference 
 *         to a destiny frame of reference. 
 * @param src Source rectangle in TLWH format
 * @param dst Scaled rectangle in TLWH format
 * @param ref_orig Original Frame of reference in TLWH format
 * @param ref_dest Destiny Frame of reference in TLWH format
 * @return 0 if success, 1 otherwise
 */
int8_t sai_transform_scale(const t_frect * src, t_frect * dst, const t_frect * ref_orig, const t_frect * ref_dest);

/**
 * @brief  Clips a TLWH rectangle to a given frame of reference 
 * @param src Source rectangle in TLWH format
 * @param dst Clipped rectangle in TLWH format
 * @param ref Frame of reference in TLWH format
 * @return 0 if success, 1 otherwise
 */
int8_t sai_transform_clip(const t_frect * src, t_frect * dst, const t_frect * ref);

/**
 * @brief  Change the frame of reference used to define a TLWH rectangle
 * @param src Source rectangle in TLWH format
 * @param dst Re-framed rectangle in TLWH format
 * @param ref_orig Original Frame of reference in TLWH format
 * @param ref_dest Destiny Frame of reference in TLWH format
 * @return 0 if success, 1 otherwise
 */
int8_t sai_transform_reframe(const t_frect * src, t_frect * dst, const t_frect * ref_orig, const t_frect * ref_dest);


/** @} */
/*----------------------------------------------------------------------------*/
/** @addtogroup PUBLIC_WEAK                                                   */
/**@{                                                                         */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/** @} */
/*--->> END: PUBLIC API <<----------------------------------------------------*/

/** @} */
/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif   //  _INC_SAI_TRANSFORM_UTILS_H_
