/**
 *******************************************************************************
 * @file    slib32_lwshell.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements the lwshell module
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
#include <stdio.h>
#include <string.h>

#include "slib32_lwshell.h"
#include "slib32_sysutils.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Controllers
* @{
* @addtogroup LWShell
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

/** LWShell prompt */
#ifndef SLIB32_LWSHELL_PROMPT
  #define SLIB32_LWSHELL_PROMPT         ">"
#endif

/** LWShell End-Of-Line */
#ifndef SLIB32_LWSHELL_EOL
  #define SLIB32_LWSHELL_EOL            "\n"
#endif

/** LWShell command max args */
#ifndef SLIB32_LWSHELL_ARG_MAX
  #define SLIB32_LWSHELL_ARG_MAX        8U
#endif

/** LWShell command string size */
#ifndef SLIB32_LWSHELL_CMD_SIZE
  #define SLIB32_LWSHELL_CMD_SIZE       128U
#endif

/** LWShell command help format */
#ifndef SLIB32_LWSHELL_HELP_FMT
  #define SLIB32_LWSHELL_HELP_FMT       "  %-8s\t%s"
#endif

/** LWShell reboot reason */
#ifndef SLIB32_SHELL_REBOOT_REASON
  #define SLIB32_SHELL_REBOOT_REASON    0xFF
#endif

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define LWSHELL_CHAR_NULL         0x00U   /*!< Null character */
#define LWSHELL_CHAR_BACKSPACE    0x08U   /*!< Backspace */
#define LWSHELL_CHAR_LF           0x0AU   /*!< Line feed */
#define LWSHELL_CHAR_CR           0x0DU   /*!< Carriage return */
#define LWSHELL_CHAR_SPACE        0x20U   /*!< Space character */
#define LWSHELL_CHAR_QUOTE        0x22U   /*!< Quote character */
#define LWSHELL_CHAR_BACKSLASH    0x5CU   /*!< Backslash character */
#define LWSHELL_CHAR_DEL          0x7FU   /*!< Delete character */
#define LWSHELL_DELETE            "\b \b" /*!< Delete characters */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Helper macro for unused parameters on built-in commands */
#define LWSHELL_UNUSED(a, b, c) \
  { (void)(a); (void)(b); (void)(c); }

/** Helper macro for shell printf */
#define LWSHELL_PRINTF(fmt, ...) \
  stream_printf(_shell.stream, _shell.out, _shell.out_size, fmt, ##__VA_ARGS__)

/** Helper macro for shell vprintf */
#define LWSHELL_VPRINTF(fmt, args) \
  stream_vprintf(_shell.stream, _shell.out, _shell.out_size, fmt, args)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Shell instance */
typedef struct
{
  bool                init;                               /*!< Initialization flag */
  bool                echo;                               /*!< Echo input chars + prompt (SoW §4.1) */
  /* Commands */
  const t_lwshell_cmd *builtin;                           /*!< Array of built-in commands */
  size_t              builtin_size;                       /*!< Number of built-in commands */
  const t_lwshell_cmd *user;                              /*!< Array of user commands */
  size_t              user_size;                          /*!< Number of user commands */
  /* Parser */
  uint8_t             line[SLIB32_LWSHELL_CMD_SIZE + 1U]; /*!< Input line */
  uint8_t             *argv[SLIB32_LWSHELL_ARG_MAX];      /*!< Arguments */
  uint8_t             *cursor;                            /*!< Cursor position */
  size_t              argc;                               /*!< Number of arguments */
  /* Streaming */
  uint8_t             *in;                                /*!< Input buffer */
  size_t              in_size;                            /*!< Input buffer size */
  uint8_t             *out;                               /*!< Output buffer */
  size_t              out_size;                           /*!< Output buffer size */
  const t_stream      *stream;                            /*!< Output stream */
} t_shell;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _lwshell_cmd_run(void);
static void _lwshell_line_parse(void);
static void _lwshell_input_reset(void);

/* Built-in -----------------------------------*/
static int32_t _builtin_cmd_help(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t _builtin_cmd_fw(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t _builtin_cmd_hw(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t _builtin_cmd_uid(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t _builtin_cmd_uptime(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t _builtin_cmd_reboot(const t_stream *stream, uint8_t **argv, size_t argc);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_shell      _shell = { 0 };
const t_lwshell_cmd _shell_cmd_builtin[] = {
  {.run = _builtin_cmd_help   , .name = "help"  , .help = "Print this info"     },
  {.run = _builtin_cmd_fw     , .name = "fw"    , .help = "Print the FW version"},
  {.run = _builtin_cmd_hw     , .name = "hw"    , .help = "Print the HW version"},
  {.run = _builtin_cmd_uid    , .name = "uid"   , .help = "Print the MCU UID"   },
  {.run = _builtin_cmd_uptime , .name = "uptime", .help = "Print the MCU uptime"},
  {.run = _builtin_cmd_reboot , .name = "reboot", .help = "Reboot the MCU"      },
};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t lwshell_init(const t_stream *stream, uint8_t* in, size_t in_size, uint8_t* out, size_t out_size)
{
  /* Validate */
  if (!stream || !in || !out || (in_size == 0U) || (out_size == 0U))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Reset shell */
  memset(&_shell, 0U, sizeof(t_shell));
  _shell.init         = true;
  _shell.echo         = true;  /* default: echo on (matches legacy behavior) */
  _shell.stream       = stream;
  _shell.in           = in;
  _shell.in_size      = in_size;
  _shell.out          = out;
  _shell.out_size     = out_size;
  _shell.user         = NULL;
  _shell.user_size    = 0U;
  _lwshell_input_reset();

  /* Register built-in commands */
  _shell.builtin      = _shell_cmd_builtin;
  _shell.builtin_size = ARRAY_SIZE(_shell_cmd_builtin);
  return SLIB32_OK;
}

int32_t lwshell_cmd_register(const t_lwshell_cmd *cmd, size_t size)
{
  /* Validate */
  if (!_shell.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!cmd || (size == 0U))
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Register commands */
  _shell.user      = cmd;
  _shell.user_size = size;
  return SLIB32_OK;
}

WEAK void lwshell_cmd_on_error(const t_stream *stream, const t_lwshell_cmd *cmd, int32_t error)
{
  UNUSED(stream);

  /* Show error */
  switch (error)
  {
    case LWSHELL_OK:
      break;

    case LWSHELL_ERROR_SYNTAX_CMD:
      LWSHELL_PRINTF("Bad syntax! Use: %s %s%s", cmd->name, cmd->help, SLIB32_LWSHELL_EOL);
      break;

    case LWSHELL_ERROR_SYNTAX_SUBCMD:
      LWSHELL_PRINTF("Bad syntax! Use: %s <subcommand>%s", cmd->name, SLIB32_LWSHELL_EOL);
      break;

    case LWSHELL_ERROR_UNKNOWN:
      LWSHELL_PRINTF("Unknown command! Use \"help\" to show available commands%s", SLIB32_LWSHELL_EOL);
      break;

    default:
      LWSHELL_PRINTF("Error (%d)!%s", error, SLIB32_LWSHELL_EOL);
      break;
  }
}

int32_t lwshell_subcmd_run(const t_stream *stream, uint8_t **argv, size_t argc, size_t nest, const char *name, const t_lwshell_cmd *cmd, size_t size)
{
  /* Validate */
  if (!_shell.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!argv || !name || !cmd || (size == 0U))
  {
    return SLIB32_ERROR_PARAMETER;
  }
  if (argc < (nest + 1U))
  {
    return LWSHELL_ERROR_SYNTAX_SUBCMD;
  }

  /* Find command */
  size_t idx;
  for (idx = 0U; idx < size; idx++)
  {
    if (strcmp((const char*)argv[nest], cmd[idx].name) == 0)
    {
      break;
    }
  }
  /* Handle command */
  if (idx < size)
  {
    int32_t status = cmd[idx].run(stream, argv, argc);
    lwshell_cmd_on_error(stream, &cmd[idx], status);
  }
  else if (strcmp((const char*)argv[nest], "help") == 0)
  {
    LWSHELL_PRINTF("%s commands:%s", name, SLIB32_LWSHELL_EOL);
    for (idx = 0U; idx < size; idx++)
    {
      LWSHELL_PRINTF(SLIB32_LWSHELL_HELP_FMT"%s", cmd[idx].name, cmd[idx].help, SLIB32_LWSHELL_EOL);
    }
  }
  else
  {
    lwshell_cmd_on_error(stream, &cmd[idx], LWSHELL_ERROR_UNKNOWN);
  }
  return LWSHELL_OK;
}

int32_t lwshell_stream_change(const t_stream *stream)
{
  /* Validate */
  if (!_shell.init)
  {
    return SLIB32_ERROR_INIT;
  }
  if (!stream)
  {
    return SLIB32_ERROR_PARAMETER;
  }

  /* Change stream */
  _shell.stream = stream;
  return SLIB32_OK;
}

int32_t lwshell_update(uint32_t timeout)
{
  /* Validate */
  if (!_shell.init)
  {
    return SLIB32_ERROR_INIT;
  }

  /* Get new bytes (and return error, if any) */
  int32_t size = stream_read(_shell.stream, _shell.in, _shell.in_size, timeout);
  if (size <= 0)
  {
    return size;
  }

  /* Process all bytes */
  const uint8_t *buff = _shell.in;
  const uint8_t *end  = buff + size;
  while (buff < end)
  {
    uint8_t byte = *buff++;
    switch (byte)
    {
      case LWSHELL_CHAR_CR:
      case LWSHELL_CHAR_LF:
        if (_shell.echo)
        {
          LWSHELL_PRINTF(SLIB32_LWSHELL_EOL);
        }
        _lwshell_line_parse();
        _lwshell_cmd_run();
        if (_shell.echo)
        {
          LWSHELL_PRINTF("%s ", SLIB32_LWSHELL_PROMPT);
        }
        _lwshell_input_reset();
        break;

      case LWSHELL_CHAR_BACKSPACE:
      case LWSHELL_CHAR_DEL:
        if (_shell.cursor > _shell.line)
        {
          *--_shell.cursor = LWSHELL_CHAR_NULL;
          if (_shell.echo)
          {
            LWSHELL_PRINTF(LWSHELL_DELETE);
          }
        }
        break;

      default:
        if (
          (_shell.cursor < (_shell.line + SLIB32_LWSHELL_CMD_SIZE - 1U)) &&
          (byte >= LWSHELL_CHAR_SPACE) &&
          (byte < LWSHELL_CHAR_DEL)
        )
        {
          if (_shell.echo)
          {
            LWSHELL_PRINTF("%c", byte);
          }
          *_shell.cursor++ = byte;
          *_shell.cursor = LWSHELL_CHAR_NULL;
        }
        break;
    }
  }
  return SLIB32_OK;
}

const char *lwshell_fmt(void)
{
  return SLIB32_LWSHELL_HELP_FMT;
}

const char* lwshell_eol(void)
{
  return SLIB32_LWSHELL_EOL;
}

void lwshell_echo_set(bool enable)
{
  _shell.echo = enable;
}

bool lwshell_echo_get(void)
{
  return _shell.echo;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Run a registered command
 */
static void _lwshell_cmd_run(void)
{
  if (_shell.argc)
  {
    /* Find command */
    const t_lwshell_cmd *cmd = NULL;
    for (size_t idx = 0; idx < _shell.builtin_size; idx++)
    {
      if (strcmp((const char*)_shell.argv[0], _shell.builtin[idx].name) == 0)
      {
        cmd = &_shell.builtin[idx];
        break;
      }
    }
    for (size_t idx = 0; !cmd && (idx < _shell.user_size); idx++)
    {
      if (strcmp((const char*)_shell.argv[0], _shell.user[idx].name) == 0)
      {
        cmd = &_shell.user[idx];
        break;
      }
    }
    /* Handle command */
    if (cmd)
    {
      int32_t status = cmd->run(_shell.stream, _shell.argv, _shell.argc);
      lwshell_cmd_on_error(_shell.stream, cmd, status);
    }
    else
    {
      lwshell_cmd_on_error(_shell.stream, cmd, LWSHELL_ERROR_UNKNOWN);
    }
    LWSHELL_PRINTF(SLIB32_LWSHELL_EOL);
  }
}

/**
 * @brief Parse arguments (tokens) from the input line
 */
static void _lwshell_line_parse(void)
{
  /* Reset arguments */
  _shell.argv[0] = _shell.line;
  _shell.argc = 0U;

  /* Process input */
  uint8_t *ptr = _shell.line;
  while (*ptr != LWSHELL_CHAR_NULL)
  {
    /* Remove leading spaces */
    while ((*ptr == LWSHELL_CHAR_SPACE) && ++ptr)
    {
      /* Do nothing, skipping */
    }

    /* Validate we still have text */
    if (*ptr == LWSHELL_CHAR_NULL)
    {
      break;
    }

    /* Check quotes (to handle escaped arguments) */
    if (*ptr == LWSHELL_CHAR_QUOTE)
    {
      /* Set start of argument after quotes */
      _shell.argv[_shell.argc++] = ++ptr;

      /* Process until end of quotes */
      while (*ptr != LWSHELL_CHAR_NULL)
      {
        if (*ptr == LWSHELL_CHAR_BACKSLASH)
        {
          /* Include quotes if escaped */
          ++ptr;
          if (*ptr == LWSHELL_CHAR_QUOTE)
          {
            ++ptr;
          }
        }
        else if (*ptr == LWSHELL_CHAR_QUOTE)
        {
          /* Found last item */
          *ptr++ = LWSHELL_CHAR_NULL;
          break;
        }
        else
        {
          ++ptr;
        }
      }
    }
    else
    {
      /* Normal token */
      _shell.argv[_shell.argc++] = ptr;
      while ((*ptr != LWSHELL_CHAR_SPACE) && (*ptr != LWSHELL_CHAR_NULL))
      {
        /* Quote not allowed mid-token */
        if (*ptr == LWSHELL_CHAR_QUOTE)
        {
          *ptr = LWSHELL_CHAR_NULL;
        }
        ++ptr;
      }
      if (*ptr == LWSHELL_CHAR_NULL)
      {
        break;
      }
      *ptr++ = LWSHELL_CHAR_NULL;
    }

    /* Check for number of arguments */
    if (_shell.argc >= SLIB32_LWSHELL_ARG_MAX)
    {
      break;
    }
  }
}

/**
 * @brief Reset the shell buffers
 */
static void _lwshell_input_reset(void)
{
  memset(_shell.line, 0U, sizeof(_shell.line));
  memset(_shell.argv, 0U, sizeof(_shell.argv));
  _shell.cursor = _shell.line;
  _shell.argc = 0U;
}

/* Built-in -----------------------------------*/
/**
 * @brief Built-in command: Help
 * @param stream Output stream
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @return Error code
 */
static int32_t _builtin_cmd_help(const t_stream *stream, uint8_t **argv, size_t argc)
{
  LWSHELL_UNUSED(stream, argv, argc);
  LWSHELL_PRINTF("Built-in commands:%s", SLIB32_LWSHELL_EOL);
  for (size_t idx = 0U; idx < _shell.builtin_size; idx++)
  {
    LWSHELL_PRINTF(SLIB32_LWSHELL_HELP_FMT"%s", _shell.builtin[idx].name, _shell.builtin[idx].help, SLIB32_LWSHELL_EOL);
  }
  if (_shell.user && _shell.user_size)
  {
    LWSHELL_PRINTF(SLIB32_LWSHELL_EOL);
    LWSHELL_PRINTF("User-defined commands:%s", SLIB32_LWSHELL_EOL);
    for (size_t idx = 0U; idx < _shell.user_size; idx++)
    {
      LWSHELL_PRINTF(SLIB32_LWSHELL_HELP_FMT"%s", _shell.user[idx].name, _shell.user[idx].help, SLIB32_LWSHELL_EOL);
    }
  }
  return LWSHELL_OK;
}

/**
 * @brief Built-in command: FW version
 * @param stream Output stream
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @return Error code
 */
static int32_t _builtin_cmd_fw(const t_stream *stream, uint8_t **argv, size_t argc)
{
  LWSHELL_UNUSED(stream, argv, argc);
  sysutils_get_firmware_version_string(_shell.line, SLIB32_LWSHELL_CMD_SIZE);
  LWSHELL_PRINTF("FW VER: %s%s", _shell.line, SLIB32_LWSHELL_EOL);
  return LWSHELL_OK;
}

/**
 * @brief Built-in command: HW version
 * @param stream Output stream
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @return Error code
 */
static int32_t _builtin_cmd_hw(const t_stream *stream, uint8_t **argv, size_t argc)
{
  LWSHELL_UNUSED(stream, argv, argc);
  sysutils_get_hardware_version_string(_shell.line, SLIB32_LWSHELL_CMD_SIZE);
  LWSHELL_PRINTF("HW VER: %s%s", _shell.line, SLIB32_LWSHELL_EOL);
  return LWSHELL_OK;
}

/**
 * @brief Built-in command: MCU UID
 * @param stream Output stream
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @return Error code
 */
static int32_t _builtin_cmd_uid(const t_stream *stream, uint8_t **argv, size_t argc)
{
  LWSHELL_UNUSED(stream, argv, argc);
  sysutils_get_mcu_uid_string(_shell.line, SLIB32_LWSHELL_CMD_SIZE);
  LWSHELL_PRINTF("UID   : %s%s", _shell.line, SLIB32_LWSHELL_EOL);
  return LWSHELL_OK;
}

/**
 * @brief Built-in command: MCU uptime
 * @param stream Output stream
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @return Error code
 */
static int32_t _builtin_cmd_uptime(const t_stream *stream, uint8_t **argv, size_t argc)
{
  LWSHELL_UNUSED(stream, argv, argc);
  sysutils_get_mcu_uptime_string(_shell.line, SLIB32_LWSHELL_CMD_SIZE);
  LWSHELL_PRINTF("Uptime: %s%s", _shell.line, SLIB32_LWSHELL_EOL);
  return LWSHELL_OK;
}

/**
 * @brief Built-in command: MCU reboot
 * @param stream Output stream
 * @param argv Arguments (tokens)
 * @param argc Number of arguments
 * @return Error code
 */
static int32_t _builtin_cmd_reboot(const t_stream *stream, uint8_t **argv, size_t argc)
{
  LWSHELL_UNUSED(stream, argv, argc);
  LWSHELL_PRINTF("Rebooting MCU...%s", SLIB32_LWSHELL_EOL);
  sysutils_mcu_reboot(_shell.stream, SLIB32_SHELL_REBOOT_REASON);
  return LWSHELL_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Controllers -->
* @} <!-- End: LWShell -->
*//*--------------------------------------------------------------------------*/
