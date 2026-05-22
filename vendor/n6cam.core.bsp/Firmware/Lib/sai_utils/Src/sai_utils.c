/**
 ******************************************************************************
 * @file    sai_utils.c
 * @author  SIANA Systems
 * @date    06/2025
 * @brief   Implement SAI utilities module.
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

#include "app_config.h"
#include "sai_utils.h"

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

/* API: Gets the class information for a given class index 
 *      if present in the selected classes. */
t_sai_class * sai_utils_get_info(int32_t class_index)
{
  if (class_index < 0 || class_index >= SAI_CLASS_NB) 
  { 
    return NULL; /* Invalid class index */
  }
  /* Get the class information */
  t_sai_class *class = (t_sai_class *) &SAI_CLASSES[class_index];

  if (class->name == NULL) 
  { 
    return NULL; /* Class not selected */
  }
  return (t_sai_class *) &SAI_CLASSES[class_index];
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
