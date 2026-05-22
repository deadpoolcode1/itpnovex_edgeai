/**
 ******************************************************************************
 * @file    nx_app_telnet.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the Telnet NetX utility
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
#ifndef __NX_APP_TELNET_H__
#define __NX_APP_TELNET_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*-->> Public Definitions <<--------------------------------------------------*/

/*-->> Public Macros <<-------------------------------------------------------*/

/*-->> Public Types <<--------------------------------------------------------*/

/*-->> Public Data <<---------------------------------------------------------*/

/*-->> Public API <<----------------------------------------------------------*/

/**
 * @brief NetX Telnet task
 */
void nx_app_telnet(void);

/**
 * @brief Disconnect Telnet session
 */
void nx_telnet_diconnect(void);

/**
 * @brief Get Telnet stream
 * @return Telnet stream instance
 */
t_stream* nx_telnet_get_stream(void);

/* -------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif
#endif /* __NX_APP_TELNET_H__ */
