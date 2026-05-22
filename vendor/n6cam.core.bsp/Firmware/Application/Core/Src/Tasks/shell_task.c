/**
 ******************************************************************************
 * @file    shell_task.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the Shell module.
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
#include "shell_task.h"

#if ENABLE_SHELL == 1U
#include "registry_task.h"
#include "slib32_lwshell.h"
#include "ux_device_cdc.h"

/* System -------------------------------------*/
#include "n6cam_rtc.h"
#include "n6cam_uart.h"
#include "n6cam_xspi.h"
#include "n6cam_watchdog.h"
#include "n6cam_core.h"   /* LED_USER3, bsp_led_set_state */
#include "nn_task.h"

/* Camera -------------------------------------*/
#include "camera_task.h"

#if defined(N6CAM_WIFI_MURATA)
/* Wifi ---------------------------------------*/
#include "nx_app.h"
#include "nx_app_mdns.h"
#include "nx_app_sntp.h"
#endif /* N6CAM_WIFI_MURATA */

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup Shell
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

/* Shell task tunables
 * TODO: Optimize stack size
 */
#define SHELL_TASK_STACK_SIZE   (2U * 1024U)
#define SHELL_TASK_PRIO         APP_PRIO_USER_INTERFACE
#define SHELL_TASK_TIME_SLICE   APP_TIME_SLICE_DEFAULT

/* Shell timeouts */
#define SHELL_UPDATE_TIMEOUT    200U

/* Shell stream */
#define SHELL_IN_SIZE           256U            /*!< Shell input buffer size */
#define SHELL_OUT_SIZE          1024U           /*!< Shell output buffer size */

/* UART recovery listener (parallel to CDC shell, reads STLink VCP UART).
 * Lets us trigger recovery when the USB-C CDC link isn't connected to the host. */
#define UART_RECOV_TASK_STACK_SIZE  (1U * 1024U)
#define UART_RECOV_TASK_PRIO        APP_PRIO_USER_INTERFACE
#define UART_RECOV_TASK_TIME_SLICE  APP_TIME_SLICE_DEFAULT
#define UART_RECOV_BUF_SIZE         32U
#define UART_RECOV_MAGIC_STR        "recovery"

/* Firmware self-updater: reads a new signed Application image over CDC,
 * writes it to xSPI flash, then resets. Avoids needing the boot switch +
 * SWD for the daily-iteration flash cycle. */
#define FWUPD_MAGIC                 "UPDT"     /* 4-byte header magic */
#define FWUPD_HDR_SIZE              12U        /* magic(4) + size_le(4) + crc32_le(4) */
#define FWUPD_MAX_SIZE              (1024U * 1024U)  /* SLOT1_APP region size */
#define FWUPD_XSPI_OFFSET           0x00400000U      /* Application region, chip-relative */
#define FWUPD_HDR_READ_TIMEOUT_MS   10000U
#define FWUPD_PAYLOAD_TIMEOUT_MS    60000U

/* Common -------------------------------------*/
#define OPT_AUTO                "auto"
#define OPT_OFF                 "off"
#define OPT_UPDATE              "update"
#define STATUS_ACTIVE           "active"
#define STATUS_INACTIVE         "inactive"
#define STATUS_UNKNOWN          "unknown"
#define STATUS_NOT_SUPPORTED    "not supported"

#if defined(N6CAM_WIFI_MURATA)
/* Wifi ---------------------------------------*/
#define WIFI_MODE_STA           "sta"
#define WIFI_AUTH_OPEN          "open"
#define WIFI_AUTH_WPA2_AES      "wpa2_aes"
#endif /* N6CAM_WIFI_MURATA */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/** Helper function for command output */
#define CMD_PRINTF(stream, fmt, ...) \
  stream_printf(stream, _shell.out, SHELL_OUT_SIZE, fmt, ##__VA_ARGS__)

/** Helper function for value limiting */
#define CMD_LIMIT_VALUE(value, min, max) \
  MAX(MIN(value, max), min)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Shell task handler */
typedef struct
{
  /* Stream */
  bool      update;
  t_stream  *stream;
  uint8_t   in[SHELL_IN_SIZE];
  uint8_t   out[SHELL_OUT_SIZE];
  /* RTOS */
  TX_THREAD thread;
  uint8_t   stack[SHELL_TASK_STACK_SIZE];
} t_shell_task;

/** UART recovery listener handler */
typedef struct
{
  TX_THREAD thread;
  uint8_t   stack[UART_RECOV_TASK_STACK_SIZE];
} t_uart_recov_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void     _shell_task_init(void);
static void     _shell_task_run(uint32_t args);
static void     _uart_recov_task_run(uint32_t args);
static void     _recovery_trigger(void);

/* System -------------------------------------*/
static int32_t  _rtc_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _version_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _recovery_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _update_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _echo_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* Sensors (SoW §3.5, §4.5) */
static int32_t  _irled_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _motion_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* JPEG / photo settings (SoW §3.4, §4.4) */
static int32_t  _img_cmd(const t_stream *stream, uint8_t **argv, size_t argc);

/* IR LED runtime state. PoC: also drive LED_USER3 as a visible proxy so the
 * user can see the command landing during bring-up; the actual IR LED GPIO
 * isn't yet wired into the BSP enum and is W15 in the proposal. */
static bool _irled_state = false;

/* SoW §4.1 success-ack helper: "<cmd> [<sub>] ok". Use at the end of any
 * new command that completes successfully. */
static void _cmd_ack(const t_stream *stream, uint8_t **argv, size_t argc);

/* Recovery magic value: must match FSBL/Core/Src/main.c */
#define FSBL_RECOVERY_MAGIC      0xDEADBEEFU

/* Camera -------------------------------------*/
static int32_t  _camera_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _camera_cmd_flip(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _camera_cmd_aec(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _camera_cmd_awb(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _camera_cmd_gain(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _camera_cmd_exposure(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _camera_cmd_brightness(const t_stream *stream, uint8_t **argv, size_t argc);

static int32_t  _camera_print_status(const t_stream *stream, int32_t status, bool update);

#if defined(N6CAM_WIFI_MURATA)
/* Wifi ---------------------------------------*/
static int32_t  _wifi_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _wifi_cmd_mode(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _wifi_cmd_join(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _wifi_cmd_mdns(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _wifi_cmd_sntp(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _wifi_cmd_static(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _wifi_cmd_ifconfig(const t_stream *stream, uint8_t **argv, size_t argc);

static int32_t  _wifi_print_update_status(const t_stream *stream, int32_t status);
static int32_t  _wifi_str_to_ipv4(const uint8_t *str, cy_wcm_ip_address_t *ip);
static uint32_t _wifi_str_to_authtype(const uint8_t *str);
static uint8_t* _wifi_authtype_to_str(uint32_t type);
static uint8_t  _wifi_str_to_mode(const uint8_t *str);
static uint8_t* _wifi_mode_to_str(uint8_t mode);
#endif /* N6CAM_WIFI_MURATA */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static t_shell_task         _shell = { 0 };
static t_uart_recov_task    _uart_recov = { 0 };
/* DMA-friendly cache-line aligned buffer */
static __ALIGN_BEGIN uint8_t _uart_recov_buf[UART_RECOV_BUF_SIZE] __ALIGN_END;

/* Common -------------------------------------*/
static const t_lwshell_cmd  _shell_cmd[] = {
  {.run = _rtc_cmd                , .name = "rtc"       , .help = "[set DDMMYYYYHHMMSS] - get or set RTC" },
  {.run = _version_cmd            , .name = "version"   , .help = "Print application version" },
  {.run = _echo_cmd               , .name = "echo"      , .help = "[on | off | query]" },
  {.run = _irled_cmd              , .name = "irled"     , .help = "[on | off | query]" },
  {.run = _motion_cmd             , .name = "motion"    , .help = "[sense <0..100> <timeout_s>] | [query]" },
  {.run = _img_cmd                , .name = "img"       , .help = "[size H W | quality 1..100 | color YCBCR|RGB|CMYK | chroma 0|1 | query]" },
  {.run = _recovery_cmd           , .name = "recovery"  , .help = "Reboot into FSBL recovery (halts chip; useful with provisioned DA cert only)" },
  {.run = _update_cmd             , .name = "update"    , .help = "Receive new App firmware over CDC and reflash xSPI" },
  {.run = _camera_cmd             , .name = "camera"    , .help = "Camera control"  },
  #if defined(N6CAM_WIFI_MURATA)
  {.run = _wifi_cmd               , .name = "wifi"      , .help = "WiFi control"    },
  #endif /* N6CAM_WIFI_MURATA */
};

/* Firmware-update receive buffer: lives in PSRAM (xSPI1, 32 MB) so we have
 * room for the full 1 MB image without touching SRAM. NOLOAD section, no
 * init cost. Cache-aligned for DMA-safe CDC RX. */
static __ALIGN_BEGIN uint8_t _fwupd_buf[FWUPD_MAX_SIZE] __ALIGN_END IN_PSRAM;

/* CRC32 (zlib-compatible, poly 0xEDB88320, reflected). Software table-based;
 * vendor's bsp_crc is CRC-16-CCITT which won't match what `zlib.crc32` on the
 * host produces, so we roll our own here. */
static uint32_t _crc32_table[256];
static bool     _crc32_ready = false;

/* Camera -------------------------------------*/
static const t_lwshell_cmd  _shell_cmd_camera[] = {
  { .run = _camera_cmd_flip       , .name = "flip"      , .help = "[H | V | "OPT_OFF"]" },
  { .run = _camera_cmd_aec        , .name = "aec"       , .help = "[value | "OPT_OFF"]  - Range: -2.0 - 2.0" },
  { .run = _camera_cmd_awb        , .name = "awb"       , .help = "[value | "OPT_AUTO"] - Range: 0 - 5" },
  { .run = _camera_cmd_gain       , .name = "gain"      , .help = "[value]        - Range: 0 - 72000[mdB]" },
  { .run = _camera_cmd_exposure   , .name = "exposure"  , .help = "[value]        - Range: 0 - 33000[usec]" },
  { .run = _camera_cmd_brightness , .name = "brightness", .help = "[value]        - Range: 0 - 100" },
};

#if defined(N6CAM_WIFI_MURATA)
/* Wifi ---------------------------------------*/
static const t_lwshell_cmd  _shell_cmd_wifi[] = {
  { .run = _wifi_cmd_mode         , .name = "mode"      , .help = "["WIFI_MODE_STA" | "OPT_OFF"]" },
  { .run = _wifi_cmd_join         , .name = "join"      , .help = "<ssid> <"WIFI_AUTH_OPEN" | "WIFI_AUTH_WPA2_AES"> [password]" },
  { .run = _wifi_cmd_mdns         , .name = "mdns"      , .help = "[hostname | "OPT_OFF"]" },
  { .run = _wifi_cmd_sntp         , .name = "sntp"      , .help = "[server | "OPT_UPDATE" | "OPT_OFF"] [resync]" },
  { .run = _wifi_cmd_static       , .name = "static"    , .help = "[ip | "OPT_OFF"] [gateway] [netmask]" },
  { .run = _wifi_cmd_ifconfig     , .name = "ifconfig"  , .help = "Prints network info" },
};
#endif /* N6CAM_WIFI_MURATA */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t shell_task_start(void)
{
  int32_t status;

  status = (int32_t)tx_thread_create(
    &_shell.thread, "tx.task.shell",
    _shell_task_run, 0,
    _shell.stack, SHELL_TASK_STACK_SIZE,
    SHELL_TASK_PRIO, SHELL_TASK_PRIO,
    SHELL_TASK_TIME_SLICE, TX_AUTO_START
  );
  if (status != TX_SUCCESS)
  {
    return status;
  }

  /* Parallel UART recovery listener on the STLink VCP UART */
  return (int32_t)tx_thread_create(
    &_uart_recov.thread, "tx.task.uart_recov",
    _uart_recov_task_run, 0,
    _uart_recov.stack, UART_RECOV_TASK_STACK_SIZE,
    UART_RECOV_TASK_PRIO, UART_RECOV_TASK_PRIO,
    UART_RECOV_TASK_TIME_SLICE, TX_AUTO_START
  );
}

int32_t shell_stream_set(t_stream *stream)
{
  stream = (!stream)? ux_cdc_get_stream() : stream;
  if (stream != _shell.stream)
  {
    _shell.stream = stream;
    _shell.update = true;
  }
  return BSP_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief SHELL task initialization.
 */
void _shell_task_init(void)
{
  ULONG status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_SYSTEM_READY | TX_EVT_UX_CDC_READY);

  /*-->> INITIALIZE <<--*/
  /* Shell configuration */
  _shell.stream = ux_cdc_get_stream();
  status = lwshell_init(_shell.stream, _shell.in, SHELL_IN_SIZE, _shell.out, SHELL_OUT_SIZE);
  if (status != TX_SUCCESS)
  {
    LERROR(TRACE_SHELL, "Init failed");
    Error_Handler();
  }
  status = lwshell_cmd_register(_shell_cmd, ARRAY_SIZE(_shell_cmd));
  if (status != TX_SUCCESS)
  {
    LERROR(TRACE_SHELL, "CMD registration failed");
    Error_Handler();
  }

  /* Apply persisted shell settings (SoW §4.1) */
  {
    t_registry_data *reg = registry_acquire();
    if (reg)
    {
      lwshell_echo_set(reg->shell_echo_enable != 0U);
      registry_release();
    }
  }

  /*-->> READY <<--*/
  LINFO(TRACE_SHELL, "Task started");
  task_raise_event(TX_EVT_SHELL_READY);
}

/**
 * @brief SHELL task entry point.
 * @param args Task arguments
 */
static void _shell_task_run(uint32_t args)
{
  UNUSED(args);

  /* Initialize task */
  _shell_task_init();

  /* Shell task */
  while (1)
  {
    lwshell_update(SHELL_UPDATE_TIMEOUT);
    if (_shell.update)
    {
      _shell.update = false;
      lwshell_stream_change(_shell.stream);
    }
  }
}

/* SoW §4.1 success-ack helper */
static void _cmd_ack(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc >= 2U)
  {
    CMD_PRINTF(stream, "%s %s ok%s", (char*)argv[0], (char*)argv[1], lwshell_eol());
  }
  else
  {
    CMD_PRINTF(stream, "%s ok%s", (char*)argv[0], lwshell_eol());
  }
}

/* Echo command (SoW §4.1): toggle terminal-style echo + prompt printing.
 * Persists in registry. */
static int32_t _echo_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  bool set       = false;
  bool new_state = false;

  if (argc >= 2U)
  {
    if (strcmp((char*)argv[1], "on") == 0)       { set = true;  new_state = true;  }
    else if (strcmp((char*)argv[1], "off") == 0) { set = true;  new_state = false; }
    else if (strcmp((char*)argv[1], "query") != 0)
    {
      return LWSHELL_ERROR_SYNTAX_CMD;
    }
  }

  if (set)
  {
    lwshell_echo_set(new_state);
    t_registry_data *reg = registry_acquire();
    if (reg)
    {
      reg->shell_echo_enable = new_state ? 1U : 0U;
      registry_release();
      registry_request_save();
    }
  }

  CMD_PRINTF(stream, "echo: %s%s", lwshell_echo_get() ? "on" : "off", lwshell_eol());
  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/* System -------------------------------------*/
/**
 * @brief RTC command: Print RTC time
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
/* Helper: parse N decimal digits from string starting at `s`. Returns false on
 * non-digit. Caller pre-checks length. */
static bool _parse_n_digits(const char *s, size_t n, uint32_t *out)
{
  uint32_t v = 0U;
  for (size_t i = 0; i < n; i++)
  {
    if ((s[i] < '0') || (s[i] > '9')) return false;
    v = (v * 10U) + (uint32_t)(s[i] - '0');
  }
  *out = v;
  return true;
}

static int32_t _rtc_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  t_datetime dt;
  int32_t    status;

  /* SoW §4.2: rtc set DDMMYYYYHHMMSS — 14 decimal digits */
  if ((argc >= 3U) && (strcmp((char*)argv[1], "set") == 0))
  {
    const char *s = (const char*)argv[2];
    if (strlen(s) != 14U) return LWSHELL_ERROR_SYNTAX_CMD;
    uint32_t dd, mm, yyyy, hh, mi, ss;
    if (!_parse_n_digits(s + 0,  2, &dd) ||
        !_parse_n_digits(s + 2,  2, &mm) ||
        !_parse_n_digits(s + 4,  4, &yyyy) ||
        !_parse_n_digits(s + 8,  2, &hh) ||
        !_parse_n_digits(s + 10, 2, &mi) ||
        !_parse_n_digits(s + 12, 2, &ss))
    {
      return LWSHELL_ERROR_SYNTAX_CMD;
    }
    if ((yyyy < 2000U) || (yyyy > 2099U)) return LWSHELL_ERROR_SYNTAX_CMD;
    dt.year    = (uint8_t)(yyyy - 2000U);
    dt.month   = (uint8_t)mm;
    dt.day     = (uint8_t)dd;
    dt.hours   = (uint8_t)hh;
    dt.minutes = (uint8_t)mi;
    dt.seconds = (uint8_t)ss;
    status = bsp_rtc_set_time(&dt);
    if (status != BSP_OK)
    {
      CMD_PRINTF(stream, "rtc set: failed (%ld)%s", (long)status, lwshell_eol());
      return LWSHELL_OK;
    }
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* Get RTC time (legacy / default) */
  status = bsp_rtc_get_time(&dt);
  if (status == BSP_OK)
  {
    CMD_PRINTF(stream,
      "RTC: %02d/%02d/%02d %02d:%02d:%02d%s",
      dt.year, dt.month, dt.day,
      dt.hours, dt.minutes, dt.seconds,
      lwshell_eol()
    );
  }
  else
  {
    CMD_PRINTF(stream, "RTC: Not available (%d)%s", status, lwshell_eol());
  }
  return LWSHELL_OK;
}

static int32_t _version_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  CMD_PRINTF(stream, "Application: %s%s", FW_VERSION, lwshell_eol());
  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/* SoW §4.5 irled on|off|query.
 * PoC: drives LED_USER3 as a proxy until the IR LED GPIO is identified (W15). */
static int32_t _irled_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  bool set = false;
  bool new_state = false;

  if (argc >= 2U)
  {
    if (strcmp((char*)argv[1], "on") == 0)       { set = true; new_state = true; }
    else if (strcmp((char*)argv[1], "off") == 0) { set = true; new_state = false; }
    else if (strcmp((char*)argv[1], "query") != 0)
    {
      return LWSHELL_ERROR_SYNTAX_CMD;
    }
  }

  if (set)
  {
    _irled_state = new_state;
    /* Visible proxy on LED_USER3 until real IR LED is wired */
    bsp_led_set_state(LED_USER3, new_state);
  }

  CMD_PRINTF(stream, "irled: %u%s", _irled_state ? 1U : 0U, lwshell_eol());
  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/* SoW §3.4 / §4.4: photo settings.
 *   img size <H> <W>      - image dimensions in pixels
 *   img quality <1..100>  - JPEG quality
 *   img color YCBCR|RGB|CMYK - color space (0/1/2)
 *   img chroma 0|1        - 0 = 4:4:4, 1 = 4:2:2
 *   img query             - dump all current settings
 *
 * Values persist in registry. Vendor's jpeg_task currently uses compile-time
 * constants for buffer sizing; once the encoder is refactored to consume the
 * registry values, this shell command drives end-to-end behavior. */
static int32_t _img_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;

  const char *sub = (const char*)argv[1];

  /* QUERY ----------------------------------------------------------------*/
  if (strcmp(sub, "query") == 0)
  {
    uint16_t w = 0U, h = 0U;
    uint8_t  q = 0U, color = 0U, chroma = 0U;
    t_registry_data *reg = registry_acquire();
    if (reg)
    {
      w      = reg->img_width;
      h      = reg->img_height;
      q      = reg->img_quality;
      color  = reg->img_color;
      chroma = reg->img_chroma;
      registry_release();
    }
    const char *color_s = (color == 0U) ? "YCBCR" : (color == 1U) ? "RGB" : (color == 2U) ? "CMYK" : "?";
    CMD_PRINTF(stream, "img: %ux%u quality=%u color=%s chroma=%u%s",
               (unsigned)w, (unsigned)h, (unsigned)q, color_s, (unsigned)chroma, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* SIZE H W -------------------------------------------------------------*/
  if ((strcmp(sub, "size") == 0) && (argc >= 4U))
  {
    long h = atol((char*)argv[2]);
    long w = atol((char*)argv[3]);
    if ((h <= 0) || (w <= 0) || (h > 0xFFFF) || (w > 0xFFFF))
    {
      return LWSHELL_ERROR_SYNTAX_CMD;
    }
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->img_height = (uint16_t)h; reg->img_width = (uint16_t)w; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "img size: %lux%lu%s", (unsigned long)w, (unsigned long)h, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* QUALITY 1..100 -------------------------------------------------------*/
  if ((strcmp(sub, "quality") == 0) && (argc >= 3U))
  {
    long q = atol((char*)argv[2]);
    if ((q < 1) || (q > 100)) return LWSHELL_ERROR_SYNTAX_CMD;
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->img_quality = (uint8_t)q; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "img quality: %ld%s", q, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* COLOR YCBCR|RGB|CMYK -------------------------------------------------*/
  if ((strcmp(sub, "color") == 0) && (argc >= 3U))
  {
    uint8_t color;
    if      (strcmp((char*)argv[2], "YCBCR") == 0) color = 0U;
    else if (strcmp((char*)argv[2], "RGB")   == 0) color = 1U;
    else if (strcmp((char*)argv[2], "CMYK")  == 0) color = 2U;
    else return LWSHELL_ERROR_SYNTAX_CMD;
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->img_color = color; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "img color: %s%s", (char*)argv[2], lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* CHROMA 0|1 -----------------------------------------------------------*/
  if ((strcmp(sub, "chroma") == 0) && (argc >= 3U))
  {
    long c = atol((char*)argv[2]);
    if ((c != 0) && (c != 1)) return LWSHELL_ERROR_SYNTAX_CMD;
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->img_chroma = (uint8_t)c; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "img chroma: %ld (%s)%s", c, c == 0 ? "4:4:4" : "4:2:2", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  return LWSHELL_ERROR_SYNTAX_CMD;
}

/* SoW §4.5 motion sense <sensitivity 0-100> <no_motion_timeout_s> | motion query.
 * PoC: parameters persist in registry; sensor wiring (W16) is a future task. */
static int32_t _motion_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  uint8_t  sens = 0U;
  uint32_t timeout_s = 0U;

  if ((argc >= 4U) && (strcmp((char*)argv[1], "sense") == 0))
  {
    int s = atoi((char*)argv[2]);
    long t = atol((char*)argv[3]);
    if ((s < 0) || (s > 100) || (t < 0))
    {
      return LWSHELL_ERROR_SYNTAX_CMD;
    }
    sens = (uint8_t)s;
    timeout_s = (uint32_t)t;

    t_registry_data *reg = registry_acquire();
    if (reg)
    {
      reg->motion_sensitivity         = sens;
      reg->motion_no_motion_timeout_s = timeout_s;
      registry_release();
      registry_request_save();
    }
    CMD_PRINTF(stream, "motion: sensitivity=%u timeout=%lu%s",
               (unsigned)sens, (unsigned long)timeout_s, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  else if ((argc >= 2U) && (strcmp((char*)argv[1], "query") == 0))
  {
    t_registry_data *reg = registry_acquire();
    if (reg)
    {
      sens      = reg->motion_sensitivity;
      timeout_s = reg->motion_no_motion_timeout_s;
      registry_release();
    }
    CMD_PRINTF(stream, "motion: sensitivity=%u timeout=%lu%s",
               (unsigned)sens, (unsigned long)timeout_s, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  return LWSHELL_ERROR_SYNTAX_CMD;
}

/**
 * Stage a magic value in TAMP backup register 0, then reset.
 * FSBL reads this on the next boot and halts before configuring xSPI,
 * leaving the chip in a state that STM32_Programmer_CLI can flash via SWD
 * without the user toggling the dev-mode boot switch.
 */
static void _recovery_trigger(void)
{
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_RTCAPB_CLK_ENABLE();
  TAMP->BKP0R = FSBL_RECOVERY_MAGIC;
  /* Give CDC/UART streams a moment to drain */
  HAL_Delay(100);
  NVIC_SystemReset();
}

static int32_t _recovery_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  UNUSED(argv);
  UNUSED(argc);

  CMD_PRINTF(stream, "Entering FSBL recovery in 100ms...%s", lwshell_eol());
  _recovery_trigger();
  return LWSHELL_OK;  /* unreachable */
}

/**
 * CRC32 (zlib/Ethernet polynomial). Lazy-init the table on first use.
 */
static void _crc32_init(void)
{
  for (uint32_t i = 0; i < 256U; i++)
  {
    uint32_t c = i;
    for (uint32_t k = 0; k < 8U; k++)
    {
      c = (c >> 1) ^ ((c & 1U) ? 0xEDB88320U : 0U);
    }
    _crc32_table[i] = c;
  }
  _crc32_ready = true;
}

static uint32_t _crc32(const uint8_t *data, size_t len)
{
  if (!_crc32_ready) { _crc32_init(); }
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < len; i++)
  {
    crc = (crc >> 8) ^ _crc32_table[(crc ^ data[i]) & 0xFFU];
  }
  return crc ^ 0xFFFFFFFFU;
}

/**
 * Blocking read of exactly `size` bytes from the shell stream. CDC reads can
 * return short; loop until done or timeout.
 */
static int32_t _stream_read_exact(const t_stream *stream, uint8_t *buf, size_t size, uint32_t timeout_ms)
{
  size_t got = 0;
  while (got < size)
  {
    int32_t n = stream_read(stream, buf + got, size - got, timeout_ms);
    if (n <= 0)
    {
      return (int32_t)got;  /* return what we have on timeout/error */
    }
    got += (size_t)n;
  }
  return (int32_t)got;
}

/**
 * Danger zone: disable xSPI memory-mapped mode, erase 16x64KB blocks of the
 * Application region, write `size` bytes from _fwupd_buf, then NVIC_SystemReset.
 * Caller must have verified the buffer (magic + CRC) before calling this.
 *
 * Note: We do NOT disable interrupts globally — the CDC mutex and watchdog
 * refresh both rely on RTOS. We do kick the IWDG inside the loop so the
 * ~5s erase doesn't trigger a reset before we want one.
 */
static int32_t _fwupd_flash_and_reset(const t_stream *stream, size_t size)
{
  int32_t status;

  /* Suspend the NN task FIRST: it's the only consumer that does memory-mapped
   * reads into xSPI NOR (model weights at 0x70600000). If we disable MMP while
   * it's mid-inference, the next weight load faults and we never get to the
   * erase/write. Camera, JPEG, display tasks read from PSRAM (xSPI1), not
   * xSPI2 NOR, so they're not affected. */
  CMD_PRINTF(stream, "Suspending NN task...%s", lwshell_eol());
  (void)nn_task_suspend_thread();
  /* Give any in-flight inference a moment to wind down before we yank the bus */
  HAL_Delay(50);

  CMD_PRINTF(stream, "Disabling xSPI memory-mapped mode...%s", lwshell_eol());
  status = BSP_XSPI_NOR_DisableMemoryMappedMode(0);
  if (status != BSP_OK)
  {
    CMD_PRINTF(stream, "ERROR: DisableMMP=%ld%s", (long)status, lwshell_eol());
    return status;
  }

  /* Erase the full SLOT1_APP region (1 MB = 16 blocks of 64 KB). Iterating
   * is faster than per-4K-sector. We always erase the whole slot — keeps the
   * code simple and is only ~5s. */
  CMD_PRINTF(stream, "Erasing 16x64KB blocks at xSPI offset 0x%08lx...%s",
             (unsigned long)FWUPD_XSPI_OFFSET, lwshell_eol());
  for (uint32_t b = 0; b < 16U; b++)
  {
    /* Don't call bsp_watchdog_refresh here — it ignores the WWDG window and
     * spurious refreshes during the upper-counter region trigger reset. The
     * vendor watchdog_task (lower priority but preempts during HAL polling)
     * keeps the dog fed on its own cadence. */
    status = BSP_XSPI_NOR_Erase_Block(0, FWUPD_XSPI_OFFSET + (b * 0x10000U), BSP_XSPI_NOR_ERASE_64K);
    if (status != BSP_OK)
    {
      CMD_PRINTF(stream, "ERROR: erase block %lu failed (%ld)%s", (unsigned long)b, (long)status, lwshell_eol());
      return status;
    }
  }

  /* Write the new image. BSP_XSPI_NOR_Write handles page-boundary splitting. */
  CMD_PRINTF(stream, "Writing %lu bytes...%s", (unsigned long)size, lwshell_eol());
  status = BSP_XSPI_NOR_Write(0, _fwupd_buf, FWUPD_XSPI_OFFSET, (uint32_t)size);
  if (status != BSP_OK)
  {
    CMD_PRINTF(stream, "ERROR: write failed (%ld)%s", (long)status, lwshell_eol());
    return status;
  }

  CMD_PRINTF(stream, "Done. Resetting...%s", lwshell_eol());
  HAL_Delay(50);  /* let CDC TX drain */
  NVIC_SystemReset();
  return BSP_OK;  /* unreachable */
}

static int32_t _update_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  uint8_t  hdr[FWUPD_HDR_SIZE];
  uint32_t size;
  uint32_t expect_crc;
  uint32_t got_crc;
  int32_t  n;

  UNUSED(argv);
  UNUSED(argc);

  CMD_PRINTF(stream,
    "Ready. Send: 'UPDT' + size_le(4) + crc32_le(4) + payload (max %lu B)%s",
    (unsigned long)FWUPD_MAX_SIZE, lwshell_eol());

  /* Read 12-byte header */
  n = _stream_read_exact(stream, hdr, FWUPD_HDR_SIZE, FWUPD_HDR_READ_TIMEOUT_MS);
  if (n != (int32_t)FWUPD_HDR_SIZE)
  {
    CMD_PRINTF(stream, "ERROR: header timeout (got %ld/%u bytes)%s",
               (long)n, (unsigned)FWUPD_HDR_SIZE, lwshell_eol());
    return LWSHELL_OK;
  }
  if (memcmp(hdr, FWUPD_MAGIC, 4) != 0)
  {
    CMD_PRINTF(stream, "ERROR: bad magic (got %02x %02x %02x %02x)%s",
               hdr[0], hdr[1], hdr[2], hdr[3], lwshell_eol());
    return LWSHELL_OK;
  }
  size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
  expect_crc = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);

  if ((size == 0U) || (size > FWUPD_MAX_SIZE))
  {
    CMD_PRINTF(stream, "ERROR: bad size %lu (max %lu)%s",
               (unsigned long)size, (unsigned long)FWUPD_MAX_SIZE, lwshell_eol());
    return LWSHELL_OK;
  }
  CMD_PRINTF(stream, "Receiving %lu bytes (expected CRC32 0x%08lx)...%s",
             (unsigned long)size, (unsigned long)expect_crc, lwshell_eol());

  /* Read payload into PSRAM buffer */
  n = _stream_read_exact(stream, _fwupd_buf, (size_t)size, FWUPD_PAYLOAD_TIMEOUT_MS);
  if (n != (int32_t)size)
  {
    CMD_PRINTF(stream, "ERROR: payload short (got %ld/%lu bytes)%s",
               (long)n, (unsigned long)size, lwshell_eol());
    return LWSHELL_OK;
  }

  /* Verify CRC32 */
  got_crc = _crc32(_fwupd_buf, (size_t)size);
  if (got_crc != expect_crc)
  {
    CMD_PRINTF(stream, "ERROR: CRC32 mismatch (got 0x%08lx, expected 0x%08lx)%s",
               (unsigned long)got_crc, (unsigned long)expect_crc, lwshell_eol());
    return LWSHELL_OK;
  }
  CMD_PRINTF(stream, "CRC32 OK. Flashing...%s", lwshell_eol());

  /* Point of no return: erase + write + reset */
  _fwupd_flash_and_reset(stream, (size_t)size);
  return LWSHELL_OK;  /* unreachable */
}

/**
 * UART recovery listener. Reads from the STLink VCP UART (USART1) and
 * triggers recovery when the magic string "recovery" appears. Mirrors the
 * CDC 'recovery' command so the feature works even when the kit's USB-C
 * isn't connected to a host. Blocks in bsp_uart_read; spends almost no CPU.
 */
static void _uart_recov_task_run(uint32_t args)
{
  UNUSED(args);

  /* Wait until the trace UART is initialized (see _system_config_trace) */
  task_wait_event(TX_EVT_SYSTEM_READY);
  LINFO(TRACE_SHELL, "UART recovery listener started on USART1 RX");

  while (1)
  {
    int32_t n = bsp_uart_read(UART_STLINK, _uart_recov_buf,
                              UART_RECOV_BUF_SIZE - 1U, TX_WAIT_FOREVER);
    if (n > 0)
    {
      _uart_recov_buf[n] = '\0';
      LINFO(TRACE_SHELL, "UART recov RX %ld bytes: \"%s\" (0x%02x 0x%02x 0x%02x)",
            (long)n, (char*)_uart_recov_buf,
            (unsigned)_uart_recov_buf[0],
            (n > 1) ? (unsigned)_uart_recov_buf[1] : 0u,
            (n > 2) ? (unsigned)_uart_recov_buf[2] : 0u);
      if (strstr((char*)_uart_recov_buf, UART_RECOV_MAGIC_STR) != NULL)
      {
        LINFO(TRACE_SHELL, "Magic match — triggering recovery");
        _recovery_trigger();
      }
    }
    else
    {
      LDEBUG(TRACE_SHELL, "UART recov RX status=%ld", (long)n);
    }
  }
}

/* Camera -------------------------------------*/
/**
 * @brief Camera control command
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _camera_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  return lwshell_subcmd_run(stream, argv, argc, 1U, "Camera", _shell_cmd_camera, ARRAY_SIZE(_shell_cmd_camera));
}

/**
 * @brief Camera flip command: Edit camera flip settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _camera_cmd_flip(const t_stream *stream, uint8_t **argv, size_t argc)
{
  uint8_t value = CAMERA_FLIP_NONE;
  int32_t status;

  /* Validate */
  if (argc >= 3U)
  {
    /* Update flip */
    uint8_t *str = argv[2];
    while (*str != '\0')
    {
      if ((*str == 'h') || (*str == 'H'))
      {
        value |= CAMERA_FLIP_H;
      }
      else if ((*str == 'v') || (*str == 'V'))
      {
        value |= CAMERA_FLIP_V;
      }
      str++;
    }
    status = _camera_print_status(stream, camera_set_flip(value), true);
    if (status != 0)
    {
      return LWSHELL_OK;
    }
  }
  status = _camera_print_status(stream, camera_get_flip(&value), false);
  if (status == 0)
  {
    if (value == CAMERA_FLIP_NONE)
    {
      CMD_PRINTF(stream, "Flip      : %s%s", OPT_OFF, lwshell_eol());
    }
    else
    {
      CMD_PRINTF(stream, "Flip      : %s%s%s", (value & CAMERA_FLIP_H) ? "H" : "", (value & CAMERA_FLIP_V) ? "V" : "", lwshell_eol());
    }
  }
  return LWSHELL_OK;
}

/**
 * @brief Camera AEC command: Edit Auto-Exposure-Control settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _camera_cmd_aec(const t_stream *stream, uint8_t **argv, size_t argc)
{
  ISP_AECAlgoTypeDef config;
  int32_t            status;

  /* Get current... */
  status = _camera_print_status(stream, camera_get_aec(&config), false);
  if (status != 0)
  {
    return LWSHELL_OK;
  }

  /* Validate */
  if (argc >= 3U)
  {
    if (strcmp((char*)argv[2], OPT_OFF) == 0)
    {
      config.enable               = 0U;
    }
    else
    {
      int32_t ev                 = (int32_t)(2.0f * atof((char*)argv[2]));
      config.exposureCompensation = (ISP_ExposureCompTypeDef)CMD_LIMIT_VALUE(ev, EXPOSURE_TARGET_MINUS_2_0_EV, EXPOSURE_TARGET_PLUS_2_0_EV);
      config.enable               = 1U;
    }
    _camera_print_status(stream, camera_set_aec(&config), true);
  }
  CMD_PRINTF(stream, "Status      : %s%s", config.enable? STATUS_ACTIVE : STATUS_INACTIVE, lwshell_eol());
  CMD_PRINTF(stream, "Compensation: %.1f%s", (float)config.exposureCompensation / 2.0f, lwshell_eol());
  return LWSHELL_OK;
}

/**
 * @brief Camera AWB command: Edit Auto-White-Balance settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _camera_cmd_awb(const t_stream *stream, uint8_t **argv, size_t argc)
{
  ISP_AWBAlgoTypeDef  config;
  uint8_t             idx;
  uint8_t             count;
  uint8_t             current;
  int32_t             status;

  /* Get current... */
  status = _camera_print_status(stream, camera_get_awb(&config, &count, &current), false);
  if (status != 0)
  {
    return LWSHELL_OK;
  }

  /* Validate */
  if (argc >= 3U)
  {
    if (strcmp((char*)argv[2], OPT_AUTO) == 0)
    {
      _camera_print_status(stream, camera_set_awb(1U, 0U), true);
      CMD_PRINTF(stream, "AWB on auto mode%s", lwshell_eol());
    }
    else
    {
      idx    = (uint8_t)atol((char*)argv[2]);
      status = camera_set_awb(0U, idx);
      _camera_print_status(stream, status, true);
      if (status == -1)
      {
        CMD_PRINTF(stream, "Invalid profile ID (%u)!%s", idx, lwshell_eol());
      }
      else
      {
        CMD_PRINTF(stream, "Using: %6luK - %s%s", config.referenceColorTemp[idx], config.label[idx], lwshell_eol());
      }
    }
  }
  else
  {
    /* Get available profiles (and IF active) */
    CMD_PRINTF(stream, "Status  : %s%s", config.enable? STATUS_ACTIVE : STATUS_INACTIVE, lwshell_eol());
    CMD_PRINTF(stream, "Profiles: %s", lwshell_eol());
    for (uint8_t idx = 0; idx <= count; idx++)
    {
      /* Print profile */
      CMD_PRINTF(stream, "  %lu: %6luK - %s", idx, config.referenceColorTemp[idx], config.label[idx]);
      if (!config.enable && (idx == current))
      {
        CMD_PRINTF(stream, " (%s)", STATUS_ACTIVE);
      }
      CMD_PRINTF(stream, lwshell_eol());
    }
  }
  return LWSHELL_OK;
}

/**
 * @brief Camera gain command: Edit Gain settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _camera_cmd_gain(const t_stream *stream, uint8_t **argv, size_t argc)
{
  ISP_SensorGainTypeDef value;
  int32_t               status;

  /* Validate */
  if (argc >= 3U)
  {
    /* Update gain */
    value.gain = CMD_LIMIT_VALUE(atol((char*)argv[2]), CAMERA_GAIN_MIN, CAMERA_GAIN_MAX);
    status = _camera_print_status(stream, camera_set_gain(&value), true);
    if (status != 0)
    {
      return LWSHELL_OK;
    }
  }
  status = _camera_print_status(stream, camera_get_gain(&value), false);
  if (status == 0)
  {
    CMD_PRINTF(stream, "Gain: %lu[mdB]%s", value.gain, lwshell_eol());
  }
  return LWSHELL_OK;
}

/**
 * @brief Camera exposure command: Edit Exposure settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _camera_cmd_exposure(const t_stream *stream, uint8_t **argv, size_t argc)
{
  ISP_SensorExposureTypeDef value;
  int32_t                   status;

  /* Validate */
  if (argc >= 3U)
  {
    /* Update exposure */
    value.exposure = CMD_LIMIT_VALUE(atol((char*)argv[2]), CAMERA_EXPOSURE_MIN, CAMERA_EXPOSURE_MAX);
    status = _camera_print_status(stream, camera_set_exposure(&value), true);
    if (status != 0)
    {
      return LWSHELL_OK;
    }
  }
  status = _camera_print_status(stream, camera_get_exposure(&value), false);
  if (status == 0)
  {
    CMD_PRINTF(stream, "Exposure: %lu[usec]%s", value.exposure, lwshell_eol());
  }
  return LWSHELL_OK;
}

/**
 * @brief Camera brightness command: Edit Brightness settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t  _camera_cmd_brightness(const t_stream *stream, uint8_t **argv, size_t argc)
{
  uint16_t value;
  int32_t  status;

  /* Validate */
  if (argc >= 3U)
  {
    /* Update brightness */
    value  = CMD_LIMIT_VALUE(atol((char* )argv[2]), CAMERA_BRIGHTNESS_MIN, CAMERA_BRIGHTNESS_MAX);
    status = _camera_print_status(stream, camera_set_brightness(value), true);
    if (status != 0)
    {
      return LWSHELL_OK;
    }
  }
  status = _camera_print_status(stream, camera_get_brightness(&value), false);
  if (status == 0)
  {
    CMD_PRINTF(stream, "Brightness: %u%s", value, lwshell_eol());
  }
  return LWSHELL_OK;
}

/**
 * @brief Print the camera operation status
 * @param stream Output stream
 * @param status Operation status
 * @param update Update required
 * @return Error code
 */
static int32_t _camera_print_status(const t_stream *stream, int32_t status, bool update)
{
  switch (status)
  {
    case -1: CMD_PRINTF(stream, "ISP error!%s", lwshell_eol()); break;
    case -2: CMD_PRINTF(stream, "No update required!%s", lwshell_eol()); break;
    case -3: CMD_PRINTF(stream, "Not supported on %s!%s", camera_get_name(), lwshell_eol()); break;
    default:
      if (update)
      {
        CMD_PRINTF(stream, "Value updated!%s", lwshell_eol());
      }
      break;
  }
  return status;
}

#if defined(N6CAM_WIFI_MURATA)
/* Wifi ---------------------------------------*/
/**
 * @brief Wifi control command
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _wifi_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  return lwshell_subcmd_run(stream, argv, argc, 1U, "Wifi", _shell_cmd_wifi, ARRAY_SIZE(_shell_cmd_wifi));
}

/**
 * @brief Wifi mode command: Set operation mode
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t  _wifi_cmd_mode(const t_stream *stream, uint8_t **argv, size_t argc)
{
  uint8_t mode;

  /* Validate */
  if (argc >= 3U)
  {
    mode = _wifi_str_to_mode(argv[2]);
    _wifi_print_update_status(stream, nx_mode_set(mode));
  }
  else
  {
    bool      pending;
    t_nx_mode selected;
    t_nx_mode stored = nx_mode_get_stored();

    /* Define if pending */
    mode     = nx_mode_get();
    pending  = (mode != stored);
    selected = pending? stored : mode;
    if (pending)
    {
      CMD_PRINTF(stream, "Pending! Reboot to apply...%s", lwshell_eol());
    }

    /* Print settings */
    CMD_PRINTF(stream, "Mode    : %s%s", _wifi_mode_to_str(selected), lwshell_eol());
  }
  return LWSHELL_OK;
}

/**
 * @brief Wifi join command: Update AP credentials
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _wifi_cmd_join(const t_stream *stream, uint8_t **argv, size_t argc)
{
  cy_wcm_ap_credentials_t credentials = { 0 };

  /* Validate */
  if (argc >= 4U)
  {
    /* Get security type */
    credentials.security = _wifi_str_to_authtype(argv[3]);

    /* Validate */
    if (credentials.security == CY_WCM_SECURITY_UNKNOWN)
    {
      CMD_PRINTF(stream, "Unsupported security type!%s", lwshell_eol());
      return LWSHELL_OK;
    }
    else if ((credentials.security > CY_WCM_SECURITY_OPEN) && (argc < 5U))
    {
      CMD_PRINTF(stream, "Missing password!%s", lwshell_eol());
      return LWSHELL_OK;
    }

    /* Prepare */
    strncpy((char*)credentials.SSID, (char*)argv[2], WIFI_SSID_LEN);
    if (credentials.security > CY_WCM_SECURITY_OPEN)
    {
      strncpy((char*)credentials.password, (char*)argv[4], WIFI_PASS_LEN);
    }

    /* Update network */
    CMD_PRINTF(stream, "Updating network (%s)...", credentials.SSID);
    _wifi_print_update_status(stream, nx_credentials_update(&credentials));
    return LWSHELL_OK;
  }
  return LWSHELL_ERROR_SYNTAX_CMD;
}

/**
 * @brief Wifi mdns command: Update mDNS settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _wifi_cmd_mdns(const t_stream *stream, uint8_t **argv, size_t argc)
{
  t_mdns_settings settings = { 0 };

  /* Validate */
  if (argc >= 3U)
  {
    /* Assume enable if hostname is provided */
    if (strcmp((char*)argv[2], OPT_OFF) != 0)
    {
      settings.enable = 1U;
      strncpy((char*)settings.hostname, (char*)argv[2], WIFI_HOSTNAME_LEN);
    }

    /* Update settings */
    _wifi_print_update_status(stream, nx_mdns_update(&settings));
  }
  else
  {
    bool            pending;
    t_mdns_settings stored;
    t_mdns_settings *selected;

    /* Get settings */
    nx_mdns_get(&settings);
    nx_mdns_get_stored(&stored);

    /* Define if pending */
    pending  = memcmp(&settings, &stored, sizeof(t_mdns_settings)) != 0;
    selected = pending? &stored : &settings;
    if (pending)
    {
      CMD_PRINTF(stream, "Pending! Reboot to apply...%s", lwshell_eol());
    }

    /* Print settings */
    CMD_PRINTF(stream, "Status  : %s%s", selected->enable? STATUS_ACTIVE : STATUS_INACTIVE, lwshell_eol());
    CMD_PRINTF(stream, "Hostname: %s%s", selected->hostname, lwshell_eol());
  }
  return LWSHELL_OK;
}

/**
 * @brief Wifi sntp command: Update SNTP settings
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t  _wifi_cmd_sntp(const t_stream *stream, uint8_t **argv, size_t argc)
{
  bool            update   = false;
  t_sntp_settings settings = { 0 };

  /* Validate */
  if (argc >= 3U)
  {
    /* Parse */
    if (strcmp((char*)argv[2], OPT_OFF) == 0)
    {
      /* Disable SNTP */
      update = true;
    }
    else if (strcmp((char*)argv[2], OPT_UPDATE) == 0)
    {
      /* Trigger SNTP update */
      CMD_PRINTF(stream, "SNTP Update triggered!%s", lwshell_eol());
      nx_sntp_time_update();
    }
    else if (argc >= 4U)
    {
      update          = true;
      settings.enable = 1U;
      settings.resync = 1000U * (uint32_t)atol((char*)argv[3]);
      strncpy((char*)settings.server, (char*)argv[2], WIFI_SERVER_LEN);
    }

    /* Update SNTP */
    if (update)
    {
      _wifi_print_update_status(stream, nx_sntp_update(&settings));
      return LWSHELL_OK;
    }
  }
  else
  {
    bool            pending;
    t_sntp_settings stored;
    t_sntp_settings *selected;

    /* Get settings */
    nx_sntp_get(&settings);
    nx_sntp_get_stored(&stored);

    /* Define if pending */
    pending  = memcmp(&settings, &stored, sizeof(t_sntp_settings)) != 0;
    selected = pending? &stored : &settings;
    if (pending)
    {
      CMD_PRINTF(stream, "Pending! Reboot to apply...%s", lwshell_eol());
    }

    /* Print settings */
    CMD_PRINTF(stream, "Status  : %s%s", selected->enable? STATUS_ACTIVE : STATUS_INACTIVE, lwshell_eol());
    CMD_PRINTF(stream, "Server  : %s%s", selected->server, lwshell_eol());
    CMD_PRINTF(stream, "Resync  : %lu[sec]%s", selected->resync / 1000U, lwshell_eol());
  }
  return LWSHELL_OK;
}

/**
 * @brief Wifi static command: Enable/Disable static IP configuration
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t  _wifi_cmd_static(const t_stream *stream, uint8_t **argv, size_t argc)
{
  bool                update   = false;
  uint8_t             enabled  = 0U;
  cy_wcm_ip_setting_t settings = { 0 };

  /* Validate */
  if (argc >= 3U)
  {
    /* Parse */
    if (strcmp((char*)argv[2], OPT_OFF) == 0)
    {
      update = true;
    }
    else if (argc >= 5U)
    {
      update  = true;
      enabled = 1U;
      if (_wifi_str_to_ipv4(argv[2], &settings.ip_address) != 0)
      {
        CMD_PRINTF(stream, "Invalid IP address!%s", lwshell_eol());
        return LWSHELL_OK;
      }
      if (_wifi_str_to_ipv4(argv[3], &settings.gateway) != 0)
      {
        CMD_PRINTF(stream, "Invalid gateway address!%s", lwshell_eol());
        return LWSHELL_OK;
      }
      if (_wifi_str_to_ipv4(argv[4], &settings.netmask) != 0)
      {
        CMD_PRINTF(stream, "Invalid netmask address!%s", lwshell_eol());
        return LWSHELL_OK;
      }
    }

    /* Update static */
    if (update)
    {
      _wifi_print_update_status(stream, nx_static_update(enabled, &settings));
      return LWSHELL_OK;
    }
  }
  else
  {
    /* Get configuration */
    if (nx_static_is_enabled())
    {
      nx_static_get(&settings);
      enabled = 1U;
    }
    else
    {
      t_registry_data *registry;

      /* Capture registry */
      registry = registry_acquire();
      if (!registry)
      {
        LERROR(TRACE_SHELL, "Registry not available");
        Error_Handler();
      }

      /* Get configurations */
      enabled                     = registry->wifi_static_enable;
      settings.ip_address.version = CY_WCM_IP_VER_V4;
      settings.ip_address.ip.v4   = registry->wifi_static_ip;
      settings.gateway.version    = CY_WCM_IP_VER_V4;
      settings.gateway.ip.v4      = registry->wifi_static_gateway;
      settings.netmask.version    = CY_WCM_IP_VER_V4;
      settings.netmask.ip.v4      = registry->wifi_static_netmask;

      /* Release registry */
      registry_release();

      /* Print header (if not applied) */
      if (enabled)
      {
        CMD_PRINTF(stream, "Pending! Reboot to apply...%s", lwshell_eol());
      }
    }

    /* Print settings */
    if (enabled)
    {
      CMD_PRINTF(stream, "IP     : "FMT_IPV4"%s", PARSE_IPV4(settings.ip_address.ip.v4), lwshell_eol());
      CMD_PRINTF(stream, "Gateway: "FMT_IPV4"%s", PARSE_IPV4(settings.gateway.ip.v4), lwshell_eol());
      CMD_PRINTF(stream, "Netmask: "FMT_IPV4"%s", PARSE_IPV4(settings.netmask.ip.v4), lwshell_eol());
    }
    else
    {
      CMD_PRINTF(stream, "Static IP not configured!%s", lwshell_eol());
    }
    return LWSHELL_OK;
  }
  return LWSHELL_ERROR_SYNTAX_CMD;
}

/**
 * @brief Wifi ifconfig command: Get network info
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _wifi_cmd_ifconfig(const t_stream *stream, uint8_t **argv, size_t argc)
{
  cy_wcm_ap_credentials_t credentials = { 0 };
  cy_wcm_ip_setting_t     settings    = { 0 };
  cy_wcm_mac_t            mac         = { 0 };

  UNUSED(argv);
  UNUSED(argc);

  /* Get basic info */
  nx_credentials_get(&credentials);
  CMD_PRINTF(stream, "SSID   : %s%s", (char*)credentials.SSID, lwshell_eol());
  CMD_PRINTF(stream, "AUTH   : %s%s", _wifi_authtype_to_str(credentials.security), lwshell_eol());
  CMD_PRINTF(stream, "DHCP   : %s%s", nx_static_is_enabled()? STATUS_INACTIVE : STATUS_ACTIVE, lwshell_eol());

  /* Print network info */
  if (cy_wcm_is_connected_to_ap() == 0U)
  {
    /* Not connected */
    CMD_PRINTF(stream, "Not connected%s", lwshell_eol());
  }
  else
  {
    /* Connected */
    cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_STA, &settings.ip_address);
    cy_wcm_get_gateway_ip_address(CY_WCM_INTERFACE_TYPE_STA, &settings.gateway);
    cy_wcm_get_ip_netmask(CY_WCM_INTERFACE_TYPE_STA, &settings.netmask);
    cy_wcm_get_mac_addr(CY_WCM_INTERFACE_TYPE_STA, &mac);
    CMD_PRINTF(stream, "IP     : "FMT_IPV4"%s", PARSE_IPV4(settings.ip_address.ip.v4), lwshell_eol());
    CMD_PRINTF(stream, "Gateway: "FMT_IPV4"%s", PARSE_IPV4(settings.gateway.ip.v4), lwshell_eol());
    CMD_PRINTF(stream, "Netmask: "FMT_IPV4"%s", PARSE_IPV4(settings.netmask.ip.v4), lwshell_eol());
    CMD_PRINTF(stream, "MAC    : "FMT_MAC"%s" , PARSE_MAC(mac), lwshell_eol());
  }
  return LWSHELL_OK;
}

/**
 * @brief Print the Wifi update status
 * @param stream Output stream
 * @param status Operation status
 */
static int32_t _wifi_print_update_status(const t_stream *stream, int32_t status)
{
  switch (status)
  {
    case -1: CMD_PRINTF(stream, "Invalid parameters!%s", lwshell_eol()); break;
    case -2: CMD_PRINTF(stream, "No update required!%s", lwshell_eol()); break;
    default: CMD_PRINTF(stream, "Ready! Reboot to apply...%s", lwshell_eol()); break;
  }
  return status;
}

/**
 * @brief Convert string to IP address
 * @param str String to convert
 * @param ip IP address
 * @return Error code
 */
static int32_t _wifi_str_to_ipv4(const uint8_t *str, cy_wcm_ip_address_t *ip)
{
  char    *ptr      = NULL;
  char    *rest     = NULL;
  uint8_t bytes[4U] = { 0U };
  size_t  idx       = 0U;

  /* Validate */
  if (!str || !ip)
  {
    return -1;
  }

  /* Parse IPv4 */
  rest = (char*)str;
  do
  {
    ptr = strtok_r(rest, ".", &rest);
    if (ptr == NULL)
    {
      return -1;
    }
    bytes[idx++] = (uint8_t)atoi(ptr);
  } while (idx < 4U);

  /* Form IP address */
  ip->version = CY_WCM_IP_VER_V4;
  ip->ip.v4   = PACK_IPV4(bytes[0U], bytes[1U], bytes[2U], bytes[3U]);
  return LWSHELL_OK;
}

/**
 * @brief Convert string to Wifi's authentication type
 * @param str String to convert
 * @return Authentication type
 */
static uint32_t _wifi_str_to_authtype(const uint8_t *str)
{
  if (strcmp((char*)str, WIFI_AUTH_OPEN) == 0)
  {
    return CY_WCM_SECURITY_OPEN;
  }
  else if (strcmp((char*)str, WIFI_AUTH_WPA2_AES) == 0)
  {
    return CY_WCM_SECURITY_WPA2_AES_PSK;
  }
  else
  {
    return CY_WCM_SECURITY_UNKNOWN;
  }
}

/**
 * @brief Convert Wifi's authentication type to string
 * @param type Authentication type
 * @return String
 */
static uint8_t* _wifi_authtype_to_str(uint32_t type)
{
  switch (type)
  {
    case CY_WCM_SECURITY_OPEN:          return (uint8_t*) WIFI_AUTH_OPEN;
    case CY_WCM_SECURITY_WPA2_AES_PSK:  return (uint8_t*) WIFI_AUTH_WPA2_AES;
    case CY_WCM_SECURITY_UNKNOWN:       return (uint8_t*) STATUS_UNKNOWN;
    default:                            return (uint8_t*) STATUS_NOT_SUPPORTED;
  }
}

/**
 * @brief Convert string to Wifi's operation mode
 * @param str String to convert
 * @return Operation mode
 */
static uint8_t _wifi_str_to_mode(const uint8_t *str)
{
  if (strcmp((char*)str, OPT_OFF) == 0)
  {
    return NX_MODE_OFF;
  }
  else if (strcmp((char*)str, WIFI_MODE_STA) == 0)
  {
    return NX_MODE_STA;
  }
  else
  {
    return NX_MODE_UNKNOWN;
  }
}

/**
 * @brief Convert Wifi's operation mode to string
 * @param mode Operation mode
 * @return String
 */
static uint8_t* _wifi_mode_to_str(uint8_t mode)
{
  switch (mode)
  {
    case NX_MODE_OFF: return (uint8_t*) OPT_OFF;
    case NX_MODE_STA: return (uint8_t*) WIFI_MODE_STA;
    default:          return (uint8_t*) STATUS_NOT_SUPPORTED;
  }
}
#endif /* N6CAM_WIFI_MURATA */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Shell -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_SHELL */

