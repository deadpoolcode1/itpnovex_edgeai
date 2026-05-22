/**
 *******************************************************************************
 * @file    n6cam_npu.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam NPU API
 *******************************************************************************
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
 *******************************************************************************
 */
#ifndef _N6CAM_NPU_H_
#define _N6CAM_NPU_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_error.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup NPU
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

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

/**
 * @brief Initialize NPU
 */
void bsp_npu_init(void);

/**
 * @brief Enable NPU SRAM (AXISRAM3-6 - 4x448KB)
 */
void bsp_npu_ram_enable(void);

/**
 * @brief NPU cache enable
 */
void bsp_npu_cache_enable(void);

/**
 * @brief NPU cache disable
 */
void bsp_npu_cache_disable(void);

/**
 * @brief Invalidate NPU cache
 */
void bsp_npu_cache_invalidate(void);

/**
 * @brief Clean NPU cache
 * @param addr Data address
 * @param size Size of memory block
 */
void bsp_npu_cache_clean(uint32_t *addr, int32_t size);

/**
 * @brief Clean NPU cache in range
 * @param start Start address
 * @param end   End address
 */
void bsp_npu_cache_clean_range(uint32_t start, uint32_t end);

/**
 * @brief Clean and invalidate NPU cache
 * @param addr Data address
 * @param size Size of memory block
 */
void bsp_npu_cache_clean_invalidate(uint32_t *addr, int32_t size);

/**
 * @brief Clean and invalidate NPU cache in range
 * @param start Start address
 * @param end   End address
 */
void bsp_npu_cache_clean_invalidate_range(uint32_t start, uint32_t end);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: NPU -->
*//*--------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif /* _N6CAM_NPU_H_ */
