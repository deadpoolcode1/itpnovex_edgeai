/**
 *******************************************************************************
 * @file    slib32_lwshell.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the lwshell module
 * @note    Ref: https://github.com/MaJerle/lwshell
 *******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 SIANA Systems. All rights reserved.
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
#ifndef _SLIB32_LWSHELL_H_
#define _SLIB32_LWSHELL_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "slib32_stream.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup LWShell
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define LWSHELL_OK                    0x00U     /*!< No error */
#define LWSHELL_ERROR_SYNTAX_CMD      0xFDU     /*!< Syntax error (command) */
#define LWSHELL_ERROR_SYNTAX_SUBCMD   0xFEU     /*!< Syntax error (subcommand parent)*/
#define LWSHELL_ERROR_UNKNOWN         0xFFU     /*!< Unknown command */

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

/**
 * @brief Shell command function
 * @param stream Stream instance
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @return Error code
 */
typedef int32_t (*t_lwshell_cmd_fn)(const t_stream *stream, uint8_t **argv, size_t argc);

/** Shell command instance */
typedef struct
{
  t_lwshell_cmd_fn run;     /*!< Command function */
  const char       *name;   /*!< Command name */
  const char       *help;   /*!< Command help */
} t_lwshell_cmd;

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
 * @brief Initializes the shell
 * @param stream Stream instance
 * @param in Input buffer
 * @param in_size Input buffer size
 * @param out Output buffer
 * @param out_size Output buffer size
 * @return Error code
 */
int32_t lwshell_init(const t_stream *stream, uint8_t* in, size_t in_size, uint8_t* out, size_t out_size);

/**
 * @brief Register user-defined commands to the shell
 * @param cmd Array of commands
 * @param size Number of commands
 * @return Error code
 */
int32_t lwshell_cmd_register(const t_lwshell_cmd *cmd, size_t size);

/**
 * @brief Callback function to handle command errors
 *
 * @note  WEAK. Can be reimplemented if required
 *
 * @param stream Stream instance
 * @param cmd Command that generated the error
 * @param error Error code
 */
void lwshell_cmd_on_error(const t_stream *stream, const t_lwshell_cmd *cmd, int32_t error);

/**
 * @brief Run a sub-command
 * @param stream Stream instance
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @param nest Sub-command nesting level
 * @param name Command name
 * @param cmd Array of commands
 * @param size Number of commands
 * @return Error code
 */
int32_t lwshell_subcmd_run(const t_stream *stream, uint8_t **argv, size_t argc, size_t nest, const char *name, const t_lwshell_cmd *cmd, size_t size);

/**
 * @brief Change the shell stream
 *
 * @note  Run this before/after lwshell_update()
 *
 * @param stream New stream instance
 * @return Error code
 */
int32_t lwshell_stream_change(const t_stream *stream);

/**
 * @brief Run the shell loop
 * @param timeout Timeout for IO operations
 * @return Error code
 */
int32_t lwshell_update(uint32_t timeout);

/**
 * @brief Get the help format string
 * @return Help format string
 */
const char *lwshell_fmt(void);

/**
 * @brief Get the End-Of-Line string
 * @return EOL string
 */
const char *lwshell_eol(void);

/**
 * @brief Enable or disable input echo + prompt printing (SoW §4.1).
 *        When disabled, the shell is suitable for scripted/automated callers.
 */
void lwshell_echo_set(bool enable);

/**
 * @brief Query current echo state.
 */
bool lwshell_echo_get(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Controllers -->
* @} <!-- End: LWShell -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _SLIB32_LWSHELL_H_ */
