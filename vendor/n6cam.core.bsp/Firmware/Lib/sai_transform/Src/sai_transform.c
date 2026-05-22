/**
 ******************************************************************************
 * @file    sai_transform.c
 * @author  SIANA Systems
 * @date    06/2025
 * @brief   Transform functions for AI application detections
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

#include "sai_transform.h"
#include "app_config.h"

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

extern int8_t sai_transform_od(const t_nn_box *detect, t_sai_object * dst);


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

/* API: Transform a single Detection instance. */
int8_t sai_transform(const t_nn_box *detect, t_sai_object * dst)
{
  #if POSTPROCESS_TYPE == POSTPROCESS_OD_YOLO_V8_UF
    return sai_transform_od(detect, dst);
  #else
    #error "Transform not implemented for this post-processing type"
  #endif

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
