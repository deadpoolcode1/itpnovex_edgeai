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
#include "hdlc.h"
#include "modem_task.h"
#include "motion_sensor.h"
#include "nn_task.h"
#include "snapshot_task.h"
#include "fx_app.h"
#include "camera_task.h"   /* CAMERA_ANCILLARY_BUFFER_SIZE */

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

/* Firmware self-updater: reads a new signed Application image (or model
 * weights binary) over CDC, writes it to xSPI flash, then resets. Avoids
 * needing the boot switch + SWD for the daily-iteration flash cycle —
 * AND for swapping the NN model weights now too (SLOT1_WEIGHTS). */
#define FWUPD_MAGIC                 "UPDT"     /* 4-byte header magic */
#define FWUPD_HDR_SIZE              12U        /* magic(4) + size_le(4) + crc32_le(4) */
/* App target */
#define FWUPD_APP_MAX_SIZE          (1024U * 1024U)        /* SLOT1_APP region size */
#define FWUPD_APP_XSPI_OFFSET       0x00400000U            /* SLOT1_APP, chip-relative */
/* Model target */
#define FWUPD_MODEL_MAX_SIZE        (16U * 1024U * 1024U)  /* upper bound for our PSRAM stash; SLOT1_WEIGHTS is 28 MB */
#define FWUPD_MODEL_XSPI_OFFSET     0x00600000U            /* SLOT1_WEIGHTS, chip-relative */
#define FWUPD_HDR_READ_TIMEOUT_MS   10000U
#define FWUPD_PAYLOAD_TIMEOUT_MS    600000U                /* 10 min — model can be tens of MB */

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
static void     _mdm_urc_forward(const char *line, size_t len, void *ctx);
static void     _notify_drain_deferred(void);
/* Remote command channel — see the block comment above the definitions. */
static int32_t  _rcmd_stream_read(uint8_t *buff, size_t size, uint32_t timeout);
static int32_t  _rcmd_stream_write(const uint8_t *buff, size_t size, uint32_t timeout);
static bool     _shell_run_remote_cmd(void);
static void     _uart_recov_task_run(uint32_t args);
static void     _recovery_trigger(void);

/* System -------------------------------------*/
static int32_t  _rtc_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _rtc_sync_from_modem(const t_stream *stream);
static int32_t  _version_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _system_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _stacks_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _commands_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _recovery_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _safeboot_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _update_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _echo_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* Sensors (SoW §3.5, §4.5) */
static int32_t  _irled_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static int32_t  _motion_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* JPEG / photo settings (SoW §3.4, §4.4) */
static int32_t  _img_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* Object detection (SoW §3.1, §4.2) */
static int32_t  _detect_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* Notifications (SoW §3.1, §4.2, §6) */
static int32_t  _notify_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* Photo capture (SoW §3.1, §4.2, §7) */
static int32_t  _photo_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* SD card management (W10) */
static int32_t  _sd_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* NN test-frame injection (for algorithm validation when camera optics are subpar) */
static int32_t  _frame_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
static bool     _nn_run_on_buffer(uint8_t *buf, uint32_t timeout_ms);
/* Tiled inference: upload one large frame, crop it into a grid of tiles, run the
 * (unchanged 256x256 yolov8n) NN on each, remap + NMS-merge across tiles. */
static int32_t  _tile_cmd(const t_stream *stream, uint8_t **argv, size_t argc);
/* MangOH modem pass-through (SoW §4.6 — `mdm <command>`) */
static int32_t  _mdm_cmd(const t_stream *stream, uint8_t **argv, size_t argc);

/* Test-frame protocol: 'FRMI' magic + size_le(4) + crc32_le(4) + payload */
#define FRAME_MAGIC                 "FRMI"
#define FRAME_HDR_SIZE              12U
#define FRAME_EXPECTED_SIZE         (CAMERA_ANCILLARY_BUFFER_SIZE)   /* NN input size: CAMERA_ANCILLARY_WIDTH*HEIGHT*BPP = 256*256*3 */
#define FRAME_RX_TIMEOUT_MS         15000U

/* Test-frame buffer for NN injection. Same layout as the camera ancillary
 * buffer so the NN doesn't notice the swap. */
static __ALIGN_BEGIN uint8_t _frame_test_buf[FRAME_EXPECTED_SIZE] __ALIGN_END IN_PSRAM;
static bool _frame_loaded = false;

/* True while the shell is part-way through receiving a bulk binary payload
 * over CDC (a test frame, a tile, a model or firmware image).
 *
 * Notifications became asynchronous when the NN task started emitting them:
 * they are now written by a different task from the one running the shell.
 * stream_write is locked, so a +SDVRNTF line cannot be cut in half — but it
 * CAN land between a command and its response. During a bulk transfer that
 * is not cosmetic: the host is streaming a fixed byte count and reading for
 * a completion banner, so an unexpected line makes it read the notification
 * as the banner and abandon the upload mid-payload, which desyncs the shell
 * for every later command. The detection still reaches the modem — only the
 * CDC copy is suppressed, and only for the length of the transfer. */
static volatile bool _shell_binary_rx = false;

/* Upper bound on `notify period`, for the same reason as MOTION_TIMEOUT_MAX_S:
 * the shell loop compares `(now - last) >= period * TX_TIMER_TICKS_PER_SECOND`
 * in 32-bit ticks, so a large period wraps into a very short one and the unit
 * reports far more often than asked, not less. */
#define NOTIFY_PERIOD_MAX_S     (86400UL)

/* Notification numerator (rolls over at 0xFFFF per SoW §6). */
static uint32_t _notify_num = 0U;

/* Scratch for composing one notification, and the lock that owns it.
 *
 * These four buffers are ~1.2 KB together and used to be locals of
 * _notify_emit(). That was survivable while the shell task was the only
 * caller and fatal once the NN task became the second one: every task in
 * this application has a 2 KB stack, and 1.2 KB of buffers plus snprintf's
 * own frame plus the USBX write path underneath stream_write does not fit
 * in what is left. The overflow ran off the end of the NN task's stack into
 * whatever ThreadX had placed after it, and the symptom was not a crash at
 * the point of damage but the USB device dying minutes later — the shell
 * stopped answering, the kit disappeared from the host, and sometimes the
 * watchdog reset the board. It is the same fault, in the same shape, as the
 * one that took down the notify thread in modem_task: a big frame on a small
 * stack, reached only on the notification path, so it hides until a
 * detection actually fires.
 *
 * Static, therefore, and serialised — a notification is composed
 * infrequently and briefly, so one lock across the whole compose costs
 * nothing and keeps _notify_num, which is part of the message, consistent
 * with the bytes built from it. */
static TX_MUTEX _notify_mtx;
static char     _notify_json[192];
static char     _notify_line[256];
static char     _notify_enc[192];
static char     _notify_at[MODEM_NOTIFY_MAX];

/* Deferred CDC copies of notifications composed on another thread.
 *
 * A single stream_write is already atomic — slib32 takes the stream's write
 * lock and ux_device_cdc's _cdc_write takes _cdc_mtx_tx underneath it — so a
 * `+SDVRNTF:` line has never been able to be cut in half by a concurrent
 * write. What it CAN do is land *between* two lines of a multi-line command
 * response: `camera status` and friends print a line per CMD_PRINTF, and the
 * NN task emits notifications on its own thread, at any moment. The host
 * protocol is line-oriented and reads a response as a block, so an unrelated
 * line inside one desyncs the parser — the same failure `_shell_binary_rx`
 * already prevents for bulk transfers, in its cheaper text form.
 *
 * So: a notification raised ON the shell thread is written inline, because
 * the shell is by definition not part-way through printing something else;
 * one raised on any other thread is parked here and written by the shell loop
 * between two lwshell_update() calls, which is the same "drain where the
 * shell is provably idle" split shell_notify_netreg and shell_remote_cmd_post
 * already use. The modem leg is unaffected — it was already queued, and it is
 * the leg that reaches the server.
 *
 * Four slots, because the ring only fills while a single command is printing;
 * if it does overflow the line is written inline rather than dropped, since an
 * interleaved line is a parser nuisance and a lost one is a missing event. */
#define NOTIFY_DEFER_DEPTH   4U
static char     _notify_defer[NOTIFY_DEFER_DEPTH][sizeof(_notify_line)];
static uint16_t _notify_defer_len[NOTIFY_DEFER_DEPTH];
static uint8_t  _notify_defer_head;
static uint8_t  _notify_defer_count;
static uint32_t _notify_defer_inline;   /* times the ring was full */

/* One command line and its output. Sized against what the shell actually
 * prints: the longest single response, `commands`, is ~1.2 KB. Anything
 * beyond REMOTE_RSP_MAX is truncated with a visible marker rather than
 * dropped silently, because a clipped answer that says so is usable and a
 * clipped answer that does not is a bug report. */
#define REMOTE_CMD_MAX    200U
#define REMOTE_RSP_MAX    1536U

/* Per-message payload, bounded by the modem: NTFA_PAYLOAD_MAX there is 384
 * bytes and the AT line cap is 512. A response longer than this is sent as
 * several AT+SDVRCMDR commands, each published as its own message, in
 * order — the operator reads them as consecutive lines of one answer. */
#define REMOTE_SEG_MAX    360U
#define REMOTE_SEG_LIMIT  5U      /* segments per response, then truncate */

static TX_MUTEX  _rcmd_mtx;
static char      _rcmd_line[REMOTE_CMD_MAX];   /* pending line, CR-terminated */
static bool      _rcmd_pending = false;
static uint32_t  _rcmd_num = 0U;

/* Feed/capture state, touched only by the shell task while a remote
 * command is running, so it needs no lock of its own. */
static char      _rcmd_run[REMOTE_CMD_MAX + 2U];   /* + CR + NUL */
static size_t    _rcmd_feed_off = 0U;
static char      _rcmd_rsp[REMOTE_RSP_MAX + 1];
static size_t    _rcmd_rsp_len = 0U;
static t_stream  _rcmd_stream;

/* Reused for the outbound AT line; static because a 512-byte buffer on a
 * 2 KB task stack is how the notification path overflowed nn_task once
 * already. */
static char      _rcmd_enc[REMOTE_SEG_MAX + 2];
static char      _rcmd_at[MODEM_NOTIFY_MAX];


/* Emit a JSON notification on the shell stream per SoW §6.
 *   rsn — bitmask reason code
 *   rsd — extra data (e.g., detected count)
 * The §6 `mtn` field is filled in from the inertial sensor rather than passed
 * in: it reports whether the *unit* is being moved right now, which is a
 * property of the box and not of the event being sent.
 * Prefixed with '+SDVRNTF: ' so the host can parse it the same way it
 * parses the modem-side URC once the modem is wired. */
/* Set from the modem URC thread, cleared by the shell loop. */
static volatile bool _notify_netreg_pending = false;
/* Likewise: the modem has just announced itself, so its clock is worth
 * having. Kept separate from the notification latch because the two are
 * answers to different questions and one may be switched off by the mask. */
static volatile bool _rtc_sync_pending      = false;
static uint32_t      _rtc_sync_tries       = 0U;

/* The modem's answer to AT+CCLK?, caught on its way past.
 *
 * modem_send_at() cannot return it: the tunnel classifies every line starting
 * with '+' that is not a terminator as a URC and routes it to this callback
 * instead of the response buffer, so `reply` holds only "OK". That heuristic
 * is load-bearing for the whole SDVR command set, so the clock is picked up
 * here rather than by making the tunnel smarter. */
static char          _cclk_line[48] = "";
static volatile bool _cclk_seen     = false;
/* Tick stamp of the last periodic report. */
static uint32_t      _notify_period_last = 0U;

static void _notify_emit(uint32_t rsn, uint32_t rsd);
static void _notify_emit_ex(uint32_t rsn, uint32_t rsd, bool force);

/* IR LED runtime state. PoC: also drive LED_USER3 as a visible proxy so the
 * user can see the command landing during bring-up; the actual IR LED GPIO
 * isn't yet wired into the BSP enum and is W15 in the proposal. */
static bool _irled_state = false;

/* SoW §4.1 success-ack helper: "<cmd> [<sub>] ok". Use at the end of any
 * new command that completes successfully. */
static void _cmd_ack(const t_stream *stream, uint8_t **argv, size_t argc);
/* Forward-decls of helpers used by frame injection (defined later in this TU). */
static int32_t  _stream_read_exact(const t_stream *stream, uint8_t *buf, size_t size, uint32_t timeout_ms);
static uint32_t _crc32(const uint8_t *data, size_t len);

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
static int32_t  _camera_cmd_status(const t_stream *stream, uint8_t **argv, size_t argc);
static void     _camera_brightness_hint(const t_stream *stream, int32_t status);

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
  {.run = _rtc_cmd                , .name = "rtc"       , .help = "[set DDMMYYYYHHMMSS | sync] - get, set, or take the time from the modem" },
  {.run = _version_cmd            , .name = "version"   , .help = "Print application version" },
  {.run = _system_cmd             , .name = "system"    , .help = "[version] - main-CPU version details (fw/uid/dev/rev) (SoW §3.7)" },
  {.run = _commands_cmd           , .name = "commands"  , .help = "List supported shell commands and parameters (SoW §3.7)" },
  {.run = _stacks_cmd             , .name = "stacks"    , .help = "Per-task stack high-water usage since boot" },
  {.run = _echo_cmd               , .name = "echo"      , .help = "[on | off | query]" },
  {.run = _irled_cmd              , .name = "irled"     , .help = "[on | off | query]" },
  {.run = _motion_cmd             , .name = "motion"    , .help = "Board movement: [sense <0..100> <timeout_s>] | [query] | [read] | [selftest] | [simulate 0|1]" },
  {.run = _img_cmd                , .name = "img"       , .help = "[size H W | quality 1..100 | color YCBCR|RGB|CMYK | chroma 0|1 | query]" },
  {.run = _detect_cmd             , .name = "detect"    , .help = "[start | stop | profile <det_msk> <act_msk> (act bit0=SD bit1=report bit2=upload) | profile query | debounce <ms> | debounce query | stats | simulate [N] [people|vehicle]]" },
  {.run = _notify_cmd             , .name = "notify"    , .help = "[enable <mask>|disable|trigger <code>|period <s>|query]" },
  {.run = _photo_cmd              , .name = "photo"     , .help = "[savesd | upload] - capture JPEG and save to SD / upload via modem" },
  {.run = _sd_cmd                 , .name = "sd"        , .help = "[query | ls | format CONFIRM]" },
  {.run = _frame_cmd              , .name = "frame"     , .help = "[upload | load <file.raw> | run | report on|off | clear | query] - inject test frame into NN" },
  {.run = _tile_cmd               , .name = "tile"      , .help = "[grid c r|crop px|frame W H|overlap h v|thresh conf iou|upload|run|live [n]|query|clear|default] - tiled multi-crop detection" },
  {.run = _mdm_cmd                , .name = "mdm"       , .help = "<cmd> | relink | test wedge [baud] | test urc <line> | test echo | stats | raw on|off - MangOH modem pass-through (SoW §4.6)" },
  {.run = _recovery_cmd           , .name = "recovery"  , .help = "Reboot into FSBL recovery (halts chip; useful with provisioned DA cert only)" },
  {.run = _safeboot_cmd           , .name = "safeboot"  , .help = "[status | clear | test] - bootloop counter / safe-mode inspection + drill" },
  {.run = _update_cmd             , .name = "update"    , .help = "[app | model] - Receive new firmware/model over CDC and reflash xSPI (default: app)" },
  {.run = _camera_cmd             , .name = "camera"    , .help = "Camera control"  },
  #if defined(N6CAM_WIFI_MURATA)
  {.run = _wifi_cmd               , .name = "wifi"      , .help = "WiFi control"    },
  #endif /* N6CAM_WIFI_MURATA */
};

/* SoW §3.7: enumerate the registered command table (name + help, where the
 * help string already documents sub-commands and parameters). Defined here,
 * after _shell_cmd[], so ARRAY_SIZE sees the complete array. */
static int32_t _commands_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  for (size_t i = 0U; i < ARRAY_SIZE(_shell_cmd); i++)
  {
    CMD_PRINTF(stream, "%-10s %s%s",
               _shell_cmd[i].name, _shell_cmd[i].help, lwshell_eol());
  }
  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/* Firmware-update receive buffers: live in PSRAM (xSPI1, 32 MB). Separate
 * buffers per target so the App update path can't accidentally overflow
 * the model buffer or vice-versa. NOLOAD section, no init cost.
 * Cache-aligned for DMA-safe CDC RX. */
static __ALIGN_BEGIN uint8_t _fwupd_app_buf  [FWUPD_APP_MAX_SIZE]   __ALIGN_END IN_PSRAM;
static __ALIGN_BEGIN uint8_t _fwupd_model_buf[FWUPD_MODEL_MAX_SIZE] __ALIGN_END IN_PSRAM;

/* Boot-guard threshold — after this many crash-reboots, App enters safe
 * mode (NN auto-start suppressed). The counter is in the flash-backed
 * registry (TAMP backup and SRAM_UNCACHED both wipe on this kit's
 * NVIC_SystemReset path; flash is the only thing that actually persists). */
#define BOOT_GUARD_THRESHOLD    3U

/* CRC32 (zlib-compatible, poly 0xEDB88320, reflected). Software table-based;
 * vendor's bsp_crc is CRC-16-CCITT which won't match what `zlib.crc32` on the
 * host produces, so we roll our own here. */
static uint32_t _crc32_table[256];
static bool     _crc32_ready = false;

/* Camera -------------------------------------*/
static const t_lwshell_cmd  _shell_cmd_camera[] = {
  { .run = _camera_cmd_flip       , .name = "flip"      , .help = "[H | V | "OPT_OFF"]" },
  { .run = _camera_cmd_aec        , .name = "aec"       , .help = "[value | "OPT_OFF"]  - Range: -2.0 - 2.0" },
  { .run = _camera_cmd_awb        , .name = "awb"       , .help = "[value | "OPT_AUTO"] - Range: 0 - N, sensor-dependent (no value lists them)" },
  { .run = _camera_cmd_gain       , .name = "gain"      , .help = "[value]        - Range: 0 - 72000[mdB]" },
  { .run = _camera_cmd_exposure   , .name = "exposure"  , .help = "[value]        - Range: 0 - 33000[usec]" },
  { .run = _camera_cmd_brightness , .name = "brightness", .help = "[value]        - Range: 0 - 100 (not on every sensor)" },
  { .run = _camera_cmd_status     , .name = "status"    , .help = "               - Print all camera settings" },
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

  /* Guards the shared notification scratch. The NN task is created before
   * this one and emits without asking this file first, which would be a
   * race but for ThreadX creating every task in tx_application_define() and
   * scheduling none of them until it returns — so nothing has run by the
   * time this mutex exists. */
  status = (int32_t)tx_mutex_create(&_rcmd_mtx, "tx.mtx.rcmd", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    LERROR(TRACE_SHELL, "Remote-cmd mutex creation failed");
    Error_Handler();
  }
  (void)stream_init(&_rcmd_stream, _rcmd_stream_read, _rcmd_stream_write);

  status = (int32_t)tx_mutex_create(&_notify_mtx, "tx.mtx.notify", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }

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

  /* Register the modem URC forwarder up front, not on the first `mdm`
   * command. The MangOH emits `+SDVRRDY: <ver>` once at its startup; with
   * registration deferred, that banner — the single best proof that the
   * modem->camera path works — was discarded unless the operator happened to
   * have run an `mdm` command earlier in the same session. */
  modem_set_urc_callback(_mdm_urc_forward, NULL);

  /* ── Safe-boot guard ───────────────────────────────────────────
   *
   * The bootloop counter is bumped + saved in registry_task_init (runs
   * before NN/camera/display init, so a crash anywhere downstream gets
   * counted). Here we just READ it and decide whether to enter safe
   * mode. Threshold = 3 — three consecutive crashes → don't auto-start
   * NN, leaving CDC reachable so the operator can push a fix via
   * 'update [app|model]'. The healthy-boot timer in the main loop
   * clears the counter after ~30 s of uptime. */
  uint8_t boot_n = 0U;
  bool safe_mode = false;

  /* §4.5 motion-sensor parameters, read under the same lock as everything
   * else and applied once it is released. */
  uint8_t  motion_sens    = 50U;
  uint32_t motion_timeout = 30U;

  /* Apply persisted shell settings (SoW §4.1 + §3.1 detect) + read
   * bootloop counter under the registry lock. NO write here — the
   * bump already happened in registry_task_init. */
  {
    t_registry_data *reg = registry_acquire();
    if (reg)
    {
      lwshell_echo_set(reg->shell_echo_enable != 0U);

      boot_n = reg->boot_count;
      safe_mode = (boot_n >= BOOT_GUARD_THRESHOLD);

      /* SoW §3.1: detection must be explicitly started. We deliberately do
       * NOT auto-restore a persisted "started" state on cold boot: if a model
       * wedges the NPU on live frames, auto-starting hard-hangs the app before
       * the shell is reachable, leaving no recovery but SWD. Always boot with
       * NN off; the operator (or a script) issues `detect start`. The det/
       * action masks below are still restored so the profile persists. */
      bool persisted_detect = (reg->detect_enable != 0U);
      (void)persisted_detect;
      nn_task_detect_set(false);
      if (safe_mode)
      {
        LERROR(TRACE_SHELL,
          "*** SAFE BOOT *** %u consecutive crash-reboots detected. "
          "NN auto-start suppressed.",
          (unsigned)boot_n);
      }
      else
      {
        LINFO(TRACE_SHELL, "Boot %u/%u — NN off (issue 'detect start' to run)",
              (unsigned)boot_n, (unsigned)BOOT_GUARD_THRESHOLD);
      }
      nn_task_det_set(reg->detect_det_mask);
      nn_task_action_set(reg->detect_action_mask);
      nn_task_debounce_set(reg->detect_debounce_ms);
      motion_sens    = reg->motion_sensitivity;
      motion_timeout = reg->motion_no_motion_timeout_s;
      registry_release();
    }
  }

  /* §3.5/§4.5 — the board's own movement sensor, armed with the parameters
   * `motion sense` persisted. It gets no task of its own: the detector is a
   * few register reads at 5 Hz, and driving it from the shell loop keeps
   * every notification producer on one thread. */
  (void)motion_sensor_init(motion_sens, motion_timeout);

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

  /* Healthy-boot watchdog: the shell task is now running; if we reach
   * ~30 s of uptime without a reboot, clear the bootloop counter so the
   * next reboot starts at 1 again. SHELL_UPDATE_TIMEOUT is ~100 ms per
   * iteration → 300 iterations ≈ 30 s. */
  uint32_t healthy_ticks = 0U;
  const uint32_t HEALTHY_BOOT_TICKS = 300U;

  /* Ask for the modem's clock on our own start-up too, not only when the
   * modem announces itself. The camera reboots far more often than the modem
   * does — every firmware flash — and on those boots no +SDVRRDY ever comes,
   * so waiting for one would leave the RTC at 2000-01-01 for the session. */
  _rtc_sync_pending = true;

  /* Shell task */
  while (1)
  {
    lwshell_update(SHELL_UPDATE_TIMEOUT);
    if (_shell.update)
    {
      _shell.update = false;
      lwshell_stream_change(_shell.stream);
    }

    /* Notifications another thread raised while a command was printing. Here,
     * between two updates, is where they cannot split a response. */
    _notify_drain_deferred();

    /* A command that arrived from the server, run here rather than on the
     * modem task's thread — lwshell belongs to this loop. Between updates
     * is the only point at which the shell is provably idle. */
    (void)_shell_run_remote_cmd();

    /* §4.2 bits 1/2 — the box itself being moved. One turn of the inertial
     * detector; it emits motion start/stop from here, on this thread, like
     * every other notification. */
    motion_sensor_poll();

    /* §4.2 bit0 — network registration. The URC arrives on the modem task;
     * emitting here keeps notification composition on one thread. */
    if (_notify_netreg_pending)
    {
      _notify_netreg_pending = false;
      _notify_emit(NOTIFY_RSN_NETREG, 0U);
    }

    /* Take the modem's clock. Done here and not in the URC callback because
     * modem_send_at blocks, and the URC fires on the modem's own receive
     * thread — asking it a question from inside its own callback would
     * deadlock. This loop is allowed to block, and does so at most once per
     * modem boot. */
    if (_rtc_sync_pending && (healthy_ticks > 20U))
    {
      /* A couple of seconds of grace first: on a cold start the CN805 link is
       * not up yet, and a sync attempted into a silent modem just burns the AT
       * timeout. Retry a bounded number of times so a modem that is slow to
       * register still ends up setting our clock, while a modem that is absent
       * does not make this a permanent tax on the loop. */
      if ((_rtc_sync_from_modem(NULL) == BSP_OK) || (++_rtc_sync_tries >= 5U))
      {
        _rtc_sync_pending = false;
      }
    }

    /* §4.2 bit3 — periodic report. `notify period <s>` stored a value that
     * nothing ever read ("wire into the system_task heartbeat later"), so the
     * interval was configurable and no report was ever sent (ScopusQA #5).
     * This loop turns at SHELL_UPDATE_TIMEOUT, which is a coarse but entirely
     * adequate clock for a report measured in seconds.
     *
     * Re-read every tick rather than cached: `notify period` can change at any
     * moment, and a cached copy would hold the old interval until reboot. */
    {
      uint32_t period = 0U;
      t_registry_data *reg = registry_acquire();
      if (reg) { period = reg->notify_period_s; registry_release(); }
      uint32_t now = (uint32_t)tx_time_get();
      if (period > 0U)
      {
        /* Unsigned difference — correct across the tick counter's wrap. */
        if ((now - _notify_period_last) >= (period * TX_TIMER_TICKS_PER_SECOND))
        {
          _notify_period_last = now;
          _notify_emit(NOTIFY_RSN_PERIODIC, 0U);
        }
      }
      else
      {
        /* Disabled: keep the base moving so switching it on later does not
         * fire immediately off a stale timestamp. */
        _notify_period_last = now;
      }
    }

    /* Clear bootloop counter once we've clearly survived the danger zone. */
    if (healthy_ticks <= HEALTHY_BOOT_TICKS)
    {
      healthy_ticks++;
      if (healthy_ticks == HEALTHY_BOOT_TICKS)
      {
        t_registry_data *reg = registry_acquire();
        if (reg && reg->boot_count != 0U)
        {
          LINFO(TRACE_SHELL, "Healthy boot reached at ~60 s; clearing bootloop counter (was %u)",
                (unsigned)reg->boot_count);
          reg->boot_count = 0U;
          registry_release();
          registry_request_save();
        }
        else if (reg)
        {
          registry_release();
        }
      }
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


/**
 * @brief Set the camera RTC from the modem's clock (AT+CCLK?).
 *
 * The camera has no battery-backed clock and no network of its own, so after
 * every power cycle its RTC starts at 2000-01-01. That is not cosmetic: the
 * SoW §7 photo name is `<serial>_DDMMYYYY_HHMMSS.rdy` and the §6 event body's
 * `tim` field both come from this clock, so every uploaded file was stamped
 * with the epoch and two photos in the same second could collide.
 *
 * The modem does have real time — it gets it from the network — and it is
 * already on the other end of a UART we own. `AT+CCLK?` answers
 * `+CCLK: "YY/MM/DD,HH:MM:SS+ZZ"`, where ZZ is the offset from UTC in
 * QUARTER hours (so "+12" is +3 h). The offset is applied, i.e. the RTC ends
 * up on local time, because that is what a tester compares against a wall
 * clock and against the receiver's own log lines.
 *
 * @param stream Where to report, or NULL for the automatic path (log only).
 * @return BSP_OK on success.
 */
static int32_t _rtc_sync_from_modem(const t_stream *stream)
{
  char reply[128] = { 0 };
  _cclk_seen = false;
  _cclk_line[0] = '\0';
  /* Returns the reply length, or negative on error — not a status code. The
   * body we actually want arrives via the URC path while this blocks. */
  int32_t mc = modem_send_at("AT+CCLK?", reply, sizeof(reply),
                             MODEM_AT_TIMEOUT_MS);
  if (mc < 0)
  {
    if (stream) CMD_PRINTF(stream, "rtc sync: modem did not answer (%ld)%s",
                           (long)mc, lwshell_eol());
    LWARNING(TRACE_SHELL, "rtc sync: modem did not answer (%ld)", (long)mc);
    return BSP_ERROR_COMPONENT;
  }

  const char *q = _cclk_seen ? strchr(_cclk_line, '"') : NULL;
  if (q == NULL)
  {
    if (stream) CMD_PRINTF(stream, "rtc sync: no clock in the modem's reply%s",
                           lwshell_eol());
    return BSP_ERROR_COMPONENT;
  }
  q++;

  /* "YY/MM/DD,HH:MM:SS+ZZ" — fixed offsets, so validate the separators
   * rather than trusting the length alone. */
  uint32_t yy, mo, dd, hh, mi, ss;
  if ((q[2] != '/') || (q[5] != '/') || (q[8] != ',') ||
      (q[11] != ':') || (q[14] != ':') ||
      !_parse_n_digits(q + 0,  2, &yy) || !_parse_n_digits(q + 3,  2, &mo) ||
      !_parse_n_digits(q + 6,  2, &dd) || !_parse_n_digits(q + 9,  2, &hh) ||
      !_parse_n_digits(q + 12, 2, &mi) || !_parse_n_digits(q + 15, 2, &ss))
  {
    if (stream) CMD_PRINTF(stream, "rtc sync: could not parse '%s'%s",
                           _cclk_line, lwshell_eol());
    return BSP_ERROR_COMPONENT;
  }

  /* Timezone, in quarter hours, optional. */
  int32_t tz_q = 0;
  const char *tz = q + 17;
  if ((*tz == '+') || (*tz == '-'))
  {
    uint32_t v = 0U;
    if (_parse_n_digits(tz + 1, 2, &v))
    {
      tz_q = (*tz == '-') ? -(int32_t)v : (int32_t)v;
    }
  }

  /* Apply the offset. Minutes since midnight can go outside the day, so
   * carry into the date — a sync a few minutes either side of midnight is
   * exactly when getting this wrong would be least visible and most wrong. */
  int32_t minutes = (int32_t)(hh * 60U + mi) + (tz_q * 15);
  int32_t day_adj = 0;
  while (minutes < 0)     { minutes += 1440; day_adj--; }
  while (minutes >= 1440) { minutes -= 1440; day_adj++; }

  int32_t y = (int32_t)yy + 2000;
  int32_t m = (int32_t)mo;
  int32_t d = (int32_t)dd + day_adj;
  static const uint8_t DIM[13] =
    { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  for (;;)
  {
    int32_t dim = (int32_t)DIM[(m >= 1 && m <= 12) ? m : 1];
    if ((m == 2) && (((y % 4) == 0) && (((y % 100) != 0) || ((y % 400) == 0))))
    {
      dim = 29;
    }
    if (d < 1)
    {
      m--; if (m < 1) { m = 12; y--; }
      dim = (int32_t)DIM[m];
      if ((m == 2) && (((y % 4) == 0) && (((y % 100) != 0) || ((y % 400) == 0))))
      {
        dim = 29;
      }
      d += dim;
    }
    else if (d > dim)
    {
      d -= dim;
      m++; if (m > 12) { m = 1; y++; }
    }
    else
    {
      break;
    }
  }

  if ((y < 2000) || (y > 2099))
  {
    if (stream) CMD_PRINTF(stream, "rtc sync: modem clock not set yet (%s)%s",
                           q, lwshell_eol());
    return BSP_ERROR_COMPONENT;
  }

  t_datetime dt;
  dt.year    = (uint8_t)(y - 2000);
  dt.month   = (uint8_t)m;
  dt.day     = (uint8_t)d;
  dt.hours   = (uint8_t)(minutes / 60);
  dt.minutes = (uint8_t)(minutes % 60);
  dt.seconds = (uint8_t)ss;

  int32_t status = bsp_rtc_set_time(&dt);
  if (status != BSP_OK)
  {
    if (stream) CMD_PRINTF(stream, "rtc sync: set failed (%ld)%s",
                           (long)status, lwshell_eol());
    return status;
  }

  LINFO(TRACE_SHELL, "rtc synced from modem: %02u/%02u/20%02u %02u:%02u:%02u "
        "(tz %+ld quarter-hours)", (unsigned)dt.day, (unsigned)dt.month,
        (unsigned)dt.year, (unsigned)dt.hours, (unsigned)dt.minutes,
        (unsigned)dt.seconds, (long)tz_q);
  if (stream)
  {
    CMD_PRINTF(stream, "rtc sync: %02u/%02u/20%02u %02u:%02u:%02u%s",
               (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year,
               (unsigned)dt.hours, (unsigned)dt.minutes, (unsigned)dt.seconds,
               lwshell_eol());
  }
  return BSP_OK;
}

static int32_t _rtc_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  t_datetime dt;
  int32_t    status;

  /* rtc sync — take the time off the modem, which has a network to get it
   * from. See _rtc_sync_from_modem for why this exists. */
  if ((argc >= 2U) && (strcmp((char*)argv[1], "sync") == 0))
  {
    if (_rtc_sync_from_modem(stream) == BSP_OK)
    {
      _cmd_ack(stream, argv, argc);
    }
    return LWSHELL_OK;
  }

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

  /* FW_VERSION comes from version_bsp.h, which the vendor generates and
   * which our build never regenerates — so every image we produce reports
   * the same string, and there is no way to tell from the outside which of
   * them is on the board. That has already cost a debugging session: a
   * flash that silently did not take is indistinguishable from one that
   * did. The compiler's own timestamp is unique per build and costs
   * nothing, so it is printed alongside and the question becomes
   * answerable. */
  CMD_PRINTF(stream, "Build: %s %s%s", __DATE__, __TIME__, lwshell_eol());

  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/* Per-task stack high-water marks.
 *
 * A stack overflow on this product does not announce itself. The task that
 * overruns keeps running; what breaks is whatever ThreadX happened to place
 * after it, minutes later and somewhere else entirely — the USB device
 * falling off the bus, the shell going quiet, a watchdog reset with no fault
 * to look at. That is expensive to diagnose from the outside and trivial to
 * read from the inside, so it is readable from the inside.
 *
 * ThreadX fills every stack with 0xEF at creation (TX_DISABLE_STACK_FILLING
 * is not defined here), so the untouched run at the low end measures what
 * has never been used and the rest is the high-water mark. It costs one
 * scan per call and nothing at all when nobody asks.
 *
 * "used" is the worst case since boot, not the current depth. Anything at or
 * near 100% has already corrupted memory — the report is late, not wrong. */
/* ThreadX's created-thread list. Declared here rather than by including
 * tx_thread.h, which is the kernel's internal header and expects to be
 * compiled as part of it; these two symbols are all we want from it. */
extern TX_THREAD *_tx_thread_created_ptr;
extern ULONG      _tx_thread_created_count;

static int32_t _stacks_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  UNUSED(argc);

  TX_INTERRUPT_SAVE_AREA
  TX_THREAD *head;
  ULONG      count;

  /* Snapshot the created-thread list under interrupt lock — a task created
   * or deleted mid-walk would otherwise take us off into freed memory. No
   * thread is created after start-up on this product, but the walk is cheap
   * to make safe and expensive to debug when it is not. */
  TX_DISABLE
  head  = _tx_thread_created_ptr;
  count = _tx_thread_created_count;
  TX_RESTORE

  CMD_PRINTF(stream, "%-22s %7s %7s %5s%s",
             "task", "size", "used", "pct", lwshell_eol());

  TX_THREAD *t = head;
  for (ULONG i = 0U; (i < count) && (t != TX_NULL); i++)
  {
    const uint8_t *base = (const uint8_t*)t->tx_thread_stack_start;
    ULONG          size = (ULONG)t->tx_thread_stack_size;

    /* Count the fill pattern still standing at the low end. */
    ULONG free_bytes = 0U;
    while ((free_bytes < size) && (base[free_bytes] == 0xEFU))
    {
      free_bytes++;
    }

    ULONG used = size - free_bytes;
    ULONG pct  = (size > 0U) ? ((used * 100U) / size) : 0U;

    CMD_PRINTF(stream, "%-22s %7lu %7lu %4lu%%%s",
               (t->tx_thread_name != TX_NULL) ? t->tx_thread_name : "?",
               (unsigned long)size, (unsigned long)used,
               (unsigned long)pct, lwshell_eol());

    t = t->tx_thread_created_next;
    if (t == head) { break; }
  }

  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/* SoW §3.7: "Query system version details of main CPU." Reports the
 * application firmware string plus the STM32N6 silicon identity (96-bit
 * MCU UID, device and revision IDs) so a host can fingerprint the board.
 * Accepts bare "system" or "system version". */
static int32_t _system_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if ((argc >= 2U) && (strcmp((char*)argv[1], "version") != 0))
  {
    return LWSHELL_ERROR_SYNTAX_CMD;
  }

  CMD_PRINTF(stream, "fw: %s%s", FW_VERSION, lwshell_eol());
  CMD_PRINTF(stream, "uid: %08lX%08lX%08lX%s",
             (unsigned long)HAL_GetUIDw2(),
             (unsigned long)HAL_GetUIDw1(),
             (unsigned long)HAL_GetUIDw0(), lwshell_eol());
  CMD_PRINTF(stream, "dev: 0x%03lX rev: 0x%04lX%s",
             (unsigned long)HAL_GetDEVID(),
             (unsigned long)HAL_GetREVID(), lwshell_eol());
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

/* The AT parameter parser on the modem consumes embedded double quotes, so
 * the §6 JSON cannot cross AT+SDVRNTFA as-is: `{"ser":1}` arrives as
 * `{ser:1}` — accepted, and no longer JSON. Substituting one character for
 * the quote survives intact (measured on the modem; so does the comma, as
 * long as the parameter is quoted). The modem restores it when the command
 * carries ENC=1. See Handle_NtfA in the modem app's at_handler.c.
 *
 * Backtick is the substitute because it cannot appear in JSON structure.
 * Every value in the body below is either numeric or a fixed-format
 * timestamp, so none can contain one. If a free-text field is ever added
 * here — `mod` is the obvious candidate — it MUST have backticks stripped
 * first, or one in the input would decode back into a stray quote on the
 * modem and corrupt the JSON. */
#define NOTIFY_QUOTE_SUB   '`'

/* atServer rejects any single parameter of 129 bytes or more (measured: 128
 * accepted, 129 a clean ERROR), so the body is split across as many
 * parameters as it needs and the modem rejoins them.
 *
 * One parameter is NOT enough, which is why this is chunked rather than
 * checked: the body is 100 bytes today only because `mod` is empty and
 * `bat`/`vol` are the placeholder 0.0. With real values it is exactly 128
 * with an empty `mod`, and any `mod` string at all goes over. Chunking means
 * populating those fields cannot silently stop notifications from being
 * forwarded. */
#define NOTIFY_CHUNK_MAX   (128U)
#define NOTIFY_JSON_MAX    (384U)   /* bounded by the modem's 512-byte line */

/* Compose the SoW §6 notification body and deliver it to both consumers:
 * the CDC shell, which is what a bench host parses, and the modem, which is
 * what actually reaches the server.
 *
 * The modem leg is queued, never sent inline. modem_send_at blocks for up to
 * MODEM_AT_TIMEOUT_MS per attempt; an earlier prototype that called it from
 * here froze the shell for 10+ seconds, and this function is also called
 * from the NN detection path, where blocking would stall inference. */
static void _notify_emit_ex(uint32_t rsn, uint32_t rsd, bool force)
{
  /* SoW §4.2 says the mask decides what is reported. It never did: the value
   * was stored by `notify enable` and read back by `notify query`, and no
   * emitter ever consulted it — which is why enabling a bit changed nothing
   * and disabling one changed nothing either (ScopusQA #5).
   *
   * Only the six §4.2 bits are gated. Bits outside that table (0x40, the
   * photo event) are ours and are not part of the mask's contract, and
   * `notify trigger` sets `force` because its whole purpose is to exercise
   * the transport regardless of what the unit is configured to report. */
  if (!force && ((rsn & NOTIFY_MASK_ALL) != 0U))
  {
    uint32_t mask = 0U;
    t_registry_data *reg = registry_acquire();
    if (reg) { mask = reg->notify_enable_mask; registry_release(); }
    if ((rsn & mask) == 0U)
    {
      LINFO(TRACE_SHELL, "notify: rsn=0x%02lx not in enable mask 0x%02lx — "
            "not reported", (unsigned long)rsn, (unsigned long)mask);
      return;
    }
  }

  t_datetime dt = { 0 };
  (void)bsp_rtc_get_time(&dt);
  /* 32-bit MCU UID low word as serial */
  uint32_t ser = HAL_GetUIDw0();

  /* The JSON body on its own — the shell line and the AT command need the
   * same bytes wrapped differently, so build it once. */
  rtos_mutex_acquire(&_notify_mtx, true);

  char *const json = _notify_json;
  int jn = snprintf(json, sizeof(_notify_json),
    "{\"ser\":%lu,\"num\":%lu,\"rsn\":%lu,\"rsd\":%lu,"
    "\"tim\":\"20%02u%02u%02u%02u%02u%02u\",\"mtn\":%u,"
    "\"mod\":\"\",\"bat\":0.0,\"vol\":0.0}",
    (unsigned long)ser, (unsigned long)_notify_num,
    (unsigned long)rsn, (unsigned long)rsd,
    (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.day,
    (unsigned)dt.hours, (unsigned)dt.minutes, (unsigned)dt.seconds,
    motion_sensor_state() ? 1U : 0U);

  if ((jn > 0) && ((size_t)jn < sizeof(_notify_json)))
  {
    char *const buf = _notify_line;
    int n = snprintf(buf, sizeof(_notify_line), "+SDVRNTF: %s\r\n", json);
    /* snprintf returns what it WOULD have written, so an over-long body would
     * have handed stream_write a length past the end of the buffer. The JSON
     * is bounded well under this, but the length that reaches a write must
     * come from the buffer and not from the formatter. */
    if ((n > 0) && ((size_t)n < sizeof(_notify_line)) &&
        (_shell.stream != NULL) && !_shell_binary_rx)
    {
      if (tx_thread_identify() == &_shell.thread)
      {
        /* Our own thread: nothing of ours is half-printed. */
        stream_write(_shell.stream, (uint8_t*)buf, (size_t)n, 100U);
      }
      else if (_notify_defer_count < NOTIFY_DEFER_DEPTH)
      {
        uint8_t slot = (uint8_t)((_notify_defer_head + _notify_defer_count) %
                                 NOTIFY_DEFER_DEPTH);
        memcpy(_notify_defer[slot], buf, (size_t)n);
        _notify_defer_len[slot] = (uint16_t)n;
        _notify_defer_count++;
      }
      else
      {
        _notify_defer_inline++;
        stream_write(_shell.stream, (uint8_t*)buf, (size_t)n, 100U);
      }
    }

    if ((size_t)jn > NOTIFY_JSON_MAX)
    {
      LERROR(TRACE_SHELL, "notify: body of %d bytes exceeds the %u-byte "
             "transport limit; not forwarded", jn, (unsigned)NOTIFY_JSON_MAX);
    }
    else
    {
      /* Substitute inside the body only; the quotes that delimit each AT
       * parameter are added below and must stay real quotes. SIZE stays the
       * true body length — the substitution is 1:1, and the modem restores
       * before it measures anything. */
      char *const enc = _notify_enc;
      for (int i = 0; i <= jn; i++)
      {
        enc[i] = (json[i] == '"') ? NOTIFY_QUOTE_SUB : json[i];
      }

      /* Split into parameters no larger than the modem's per-parameter cap.
       * The modem concatenates them back in order (ENC=1). One chunk is the
       * common case today and is byte-for-byte the single-parameter form. */
      char *const at = _notify_at;
      int an = snprintf(at, sizeof(_notify_at), "AT+SDVRNTFA=%lu,%d",
                        (unsigned long)_notify_num, jn);
      bool ok = (an > 0) && ((size_t)an < sizeof(_notify_at));

      for (int off = 0; ok && (off < jn); off += (int)NOTIFY_CHUNK_MAX)
      {
        int take = jn - off;
        if (take > (int)NOTIFY_CHUNK_MAX) { take = (int)NOTIFY_CHUNK_MAX; }
        int wrote = snprintf(&at[an], sizeof(_notify_at) - (size_t)an,
                             ",\"%.*s\"", take, &enc[off]);
        if ((wrote <= 0) || ((size_t)(an + wrote) >= sizeof(_notify_at)))
        {
          ok = false;
          break;
        }
        an += wrote;
      }

      if (ok)
      {
        int wrote = snprintf(&at[an], sizeof(_notify_at) - (size_t)an, ",1");
        ok = (wrote > 0) && ((size_t)(an + wrote) < sizeof(_notify_at));
      }

      if (ok)
      {
        (void)modem_notify_async(at);
      }
      else
      {
        LERROR(TRACE_SHELL, "notify: AT line would overflow %u bytes; "
               "not forwarded", (unsigned)MODEM_NOTIFY_MAX);
      }
    }
  }

  _notify_num = (_notify_num + 1U) & 0xFFFFU;

  rtos_mutex_acquire(&_notify_mtx, false);
}

static void _notify_emit(uint32_t rsn, uint32_t rsd)
{
  _notify_emit_ex(rsn, rsd, false);
}

/* Write out any notification parked by another thread. Shell thread only:
 * this is the point the deferral exists to reach. */
static void _notify_drain_deferred(void)
{
  for (;;)
  {
    rtos_mutex_acquire(&_notify_mtx, true);
    if (_notify_defer_count == 0U)
    {
      rtos_mutex_acquire(&_notify_mtx, false);
      return;
    }
    uint8_t slot = _notify_defer_head;
    size_t  n    = (size_t)_notify_defer_len[slot];
    /* Written under _notify_mtx, exactly as the inline path is: the lock is
     * what keeps the slot from being refilled while it is on the wire, and
     * emitters already serialise on it for the whole compose. */
    if ((n > 0U) && (_shell.stream != NULL) && !_shell_binary_rx)
    {
      stream_write(_shell.stream, (uint8_t*)_notify_defer[slot], n, 100U);
    }
    _notify_defer_head = (uint8_t)((slot + 1U) % NOTIFY_DEFER_DEPTH);
    _notify_defer_count--;
    rtos_mutex_acquire(&_notify_mtx, false);
  }
}

/* Public entry point for producers outside this file — today the NN task's
 * live inference loop, which has a detection to report and no business
 * knowing how a notification is composed or where it goes. */
void shell_notify_emit(uint32_t rsn, uint32_t rsd)
{
  _notify_emit(rsn, rsd);
}

/* Network registration (§4.2 bit 0). Latched by the modem URC forwarder,
 * which runs on the modem task, and drained by the shell loop — the same
 * split `shell_remote_cmd_post` uses, so a URC never emits a notification
 * from inside the modem's own receive path. */
void shell_notify_netreg(void)
{
  _notify_netreg_pending = true;
}

/*-------------------------------------------------------------------------*//**
 * Remote command execution — the camera end of the MQTT command channel.
 *
 * The modem subscribes to the unit's command topic and forwards anything
 * that is not an AT command here, as `+SDVRCMD: "<text>"`. The text is a
 * shell line: it runs in this shell, against these commands, and its
 * output goes back through AT+SDVRCMDR to be published.
 *
 * Two constraints shape the whole design:
 *
 * 1. **lwshell is not reentrant and owns one stream at a time.** The URC
 *    arrives on the modem task's thread, and running the command there
 *    would tear the console shell in half. So the URC only *parks* the
 *    line, and the shell task picks it up between its own iterations —
 *    the command runs on the thread that owns lwshell, always.
 *
 * 2. **The output has to be captured, not printed.** lwshell writes
 *    through whatever stream is installed, so the executor swaps in a
 *    capture stream that appends to a buffer, runs the line, and swaps the
 *    console back. Nothing in the command implementations changes, and a
 *    remote `version` produces byte-identical text to a typed one.
 *//*--------------------------------------------------------------------------*/

/* Capture stream: reading hands lwshell the parked line once, writing
 * appends to the response buffer. */
static int32_t _rcmd_stream_read(uint8_t *buff, size_t size, uint32_t timeout)
{
  UNUSED(timeout);
  size_t total = strlen(_rcmd_run);
  if (_rcmd_feed_off >= total)
  {
    return 0;
  }
  size_t left = total - _rcmd_feed_off;
  size_t take = (left < size) ? left : size;
  memcpy(buff, &_rcmd_run[_rcmd_feed_off], take);
  _rcmd_feed_off += take;
  return (int32_t)take;
}

static int32_t _rcmd_stream_write(const uint8_t *buff, size_t size,
                                  uint32_t timeout)
{
  UNUSED(timeout);
  size_t room = (_rcmd_rsp_len < REMOTE_RSP_MAX)
                  ? (REMOTE_RSP_MAX - _rcmd_rsp_len) : 0U;
  size_t take = (size < room) ? size : room;
  if (take > 0U)
  {
    memcpy(&_rcmd_rsp[_rcmd_rsp_len], buff, take);
    _rcmd_rsp_len += take;
    _rcmd_rsp[_rcmd_rsp_len] = '\0';
  }
  /* Always claim the whole write. Reporting a short write would make the
   * command believe the console is congested and, for the commands that
   * check, change what they print — the capture must not alter the
   * output it exists to observe. */
  return (int32_t)size;
}

/* Park a command received from the modem. Runs on the modem task's thread;
 * does no work beyond the copy, for the reason in note 1 above. */
void shell_remote_cmd_post(const char *text)
{
  if ((text == NULL) || (text[0] == '\0'))
  {
    return;
  }

  rtos_mutex_acquire(&_rcmd_mtx, true);
  if (_rcmd_pending)
  {
    /* One at a time. Overwriting the parked line would answer the second
     * command and leave the first unanswered but acknowledged, which is
     * the one failure mode an operator cannot diagnose from the far end. */
    LWARNING(TRACE_SHELL, "remote cmd: busy, rejecting '%s'", text);
    rtos_mutex_acquire(&_rcmd_mtx, false);
    return;
  }
  (void)snprintf(_rcmd_line, sizeof(_rcmd_line), "%s", text);
  _rcmd_pending = true;
  rtos_mutex_acquire(&_rcmd_mtx, false);
}

/* Queue one segment of a response as AT+SDVRCMDR. Encoding is identical to
 * the notification path — backtick for the double quote, 128-byte chunks,
 * ENC=1 — because the AT line imposes the same rules regardless of what is
 * travelling on it. Shell output contains quotes routinely, so the
 * substitution matters more here than it does for the §6 JSON. */
static void _rcmd_send_segment(const char *seg, size_t len, uint32_t num)
{
  if ((len == 0U) || (len > REMOTE_SEG_MAX))
  {
    return;
  }

  for (size_t i = 0U; i < len; i++)
  {
    char c = seg[i];
    /* CR/LF cannot cross an AT parameter — atServer terminates the line on
     * them. They are the one thing the substitution cannot carry, so they
     * become spaces and the response reads as one flowed line. */
    if ((c == '\r') || (c == '\n')) { c = ' '; }
    else if (c == '"')              { c = NOTIFY_QUOTE_SUB; }
    _rcmd_enc[i] = c;
  }
  _rcmd_enc[len] = '\0';

  int an = snprintf(_rcmd_at, sizeof(_rcmd_at), "AT+SDVRCMDR=%lu,%u",
                    (unsigned long)num, (unsigned)len);
  bool ok = (an > 0) && ((size_t)an < sizeof(_rcmd_at));

  for (size_t off = 0U; ok && (off < len); off += NOTIFY_CHUNK_MAX)
  {
    size_t take = len - off;
    if (take > NOTIFY_CHUNK_MAX) { take = NOTIFY_CHUNK_MAX; }
    int wrote = snprintf(&_rcmd_at[an], sizeof(_rcmd_at) - (size_t)an,
                         ",\"%.*s\"", (int)take, &_rcmd_enc[off]);
    if ((wrote <= 0) || ((size_t)(an + wrote) >= sizeof(_rcmd_at)))
    {
      ok = false;
      break;
    }
    an += wrote;
  }

  if (ok)
  {
    int wrote = snprintf(&_rcmd_at[an], sizeof(_rcmd_at) - (size_t)an, ",1");
    ok = (wrote > 0) && ((size_t)(an + wrote) < sizeof(_rcmd_at));
  }

  if (ok)
  {
    (void)modem_notify_async(_rcmd_at);
  }
  else
  {
    LERROR(TRACE_SHELL, "remote cmd: response segment would overflow the "
           "AT line; dropped");
  }
}

/* Run any parked remote command. Called from the shell task's own loop, so
 * lwshell is idle and this thread owns it. Returns true if one ran. */
static bool _shell_run_remote_cmd(void)
{
  bool have = false;

  rtos_mutex_acquire(&_rcmd_mtx, true);
  if (_rcmd_pending)
  {
    /* The trailing CR is what makes lwshell treat the fed bytes as a
     * complete line and execute it. */
    (void)snprintf(_rcmd_run, sizeof(_rcmd_run), "%s\r", _rcmd_line);
    _rcmd_pending = false;
    have = true;
  }
  rtos_mutex_acquire(&_rcmd_mtx, false);

  if (!have)
  {
    return false;
  }

  LINFO(TRACE_SHELL, "remote cmd: running '%s'", _rcmd_line);

  _rcmd_feed_off = 0U;
  _rcmd_rsp_len  = 0U;
  _rcmd_rsp[0]   = '\0';

  /* Echo off while captured: the echo would put the command text and a
   * prompt into its own response, which is noise on the operator's screen
   * and costs AT-line budget that the answer needs. */
  bool echo_was = lwshell_echo_get();
  lwshell_echo_set(false);

  (void)lwshell_stream_change(&_rcmd_stream);
  /* Timeout 0: the capture stream never blocks, and the whole line is
   * already buffered, so one update consumes and runs it. */
  (void)lwshell_update(0U);
  (void)lwshell_stream_change(_shell.stream);

  lwshell_echo_set(echo_was);

  if (_rcmd_rsp_len == 0U)
  {
    (void)snprintf(_rcmd_rsp, sizeof(_rcmd_rsp), "(no output)");
    _rcmd_rsp_len = strlen(_rcmd_rsp);
  }

  /* Send in order, one AT command per segment. */
  size_t sent = 0U;
  for (uint32_t s = 0U; (s < REMOTE_SEG_LIMIT) && (sent < _rcmd_rsp_len); s++)
  {
    size_t take = _rcmd_rsp_len - sent;
    if (take > REMOTE_SEG_MAX) { take = REMOTE_SEG_MAX; }
    _rcmd_send_segment(&_rcmd_rsp[sent], take, _rcmd_num);
    sent += take;
    _rcmd_num = (_rcmd_num + 1U) & 0xFFFFU;
  }

  if (sent < _rcmd_rsp_len)
  {
    const char *more = "[truncated]";
    _rcmd_send_segment(more, strlen(more), _rcmd_num);
    _rcmd_num = (_rcmd_num + 1U) & 0xFFFFU;
  }

  return true;
}

/* Test-frame injection — for validating the NN algorithm against a known
 * scene without depending on whatever the camera lens currently sees.
 *
 *   frame upload  -> receive FRAME_HDR (FRMI+size+crc) + CAMERA_ANCILLARY_BUFFER_SIZE RGB bytes
 *   frame run     -> route NN input to the test buffer; wait for the next
 *                    inference; print detection count + top classes
 *   frame clear   -> revert NN to live camera input
 *   frame query   -> show whether a test frame is loaded
 *
 * Frame format: CAMERA_ANCILLARY_WIDTH x HEIGHT (256x256), RGB888 (R,G,B,R,G,B,...), row-major, top-left
 * origin. Same layout the camera ancillary pipeline produces, so the NN
 * sees an indistinguishable input.
 */
static int32_t _frame_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;
  const char *sub = (const char*)argv[1];

  if (strcmp(sub, "query") == 0)
  {
    /* Report the override, not _frame_loaded. They diverge once the
     * override lapses on its own, and the one that matters to the caller
     * is the one the NN is actually reading. */
    bool active = nn_task_test_frame_active();
    if (active)
    {
      CMD_PRINTF(stream, "frame: loaded (NN %s) — inference is running on the "
                         "test picture, NOT the lens; lapses in %lus%s",
                 nn_task_detect_get() ? "running" : "stopped",
                 (unsigned long)nn_task_test_frame_remaining_s(),
                 lwshell_eol());
    }
    else
    {
      CMD_PRINTF(stream, "frame: empty (NN %s)%s",
                 nn_task_detect_get() ? "running" : "stopped",
                 lwshell_eol());
    }
    _frame_loaded = active;
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "clear") == 0)
  {
    nn_task_set_test_frame(NULL);
    /* The opt-in goes with the frame. Leaving it armed would mean the next
     * tester's injected image reports as a real detection, which is the one
     * thing the mute exists to prevent. */
    nn_task_test_frame_report_set(false);
    _frame_loaded = false;
    CMD_PRINTF(stream, "frame: cleared (NN back to live camera)%s", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* SD-based load: read a pre-prepared 192x192 RGB888 .raw file from SD.
   * Workflow: drop a folder of .raw files on the SD card via PC, then
   * iterate quickly through them with 'frame load file.raw' + 'frame run'. */
  if ((strcmp(sub, "load") == 0) && (argc >= 3U))
  {
    const char *fname = (const char*)argv[2];
    size_t got = 0U;
    int32_t status = fx_app_read_file(fname, _frame_test_buf, FRAME_EXPECTED_SIZE, &got);
    if (status != FX_SUCCESS)
    {
      CMD_PRINTF(stream, "frame load: read failed (%ld)%s", (long)status, lwshell_eol());
      return LWSHELL_OK;
    }
    if (got != FRAME_EXPECTED_SIZE)
    {
      CMD_PRINTF(stream,
        "frame load: bad size %lu, expected %u (must be 192x192 RGB888)%s",
        (unsigned long)got, (unsigned)FRAME_EXPECTED_SIZE, lwshell_eol());
      _frame_loaded = false;
      return LWSHELL_OK;
    }
    SCB_CleanInvalidateDCache_by_Addr((uint32_t*)_frame_test_buf, FRAME_EXPECTED_SIZE);
    _frame_loaded = true;
    CMD_PRINTF(stream, "frame load: ok (%s, %lu bytes)%s",
               fname, (unsigned long)got, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "upload") == 0)
  {
    uint8_t hdr[FRAME_HDR_SIZE];

    /* Claim the stream for the whole transaction, banner included, BEFORE
     * printing it. _stream_read_exact sets this too, but that is one line too
     * late: the host sends `frame upload` and then reads for this banner, so a
     * notification emitted in the gap between the command and the banner is
     * what the host reads as the banner. It then streams the payload into a
     * kit that is no longer expecting it, and the upload fails with an empty
     * banner — reproducible on the bench simply by running the injector while
     * detection is live. */
    _shell_binary_rx = true;

    CMD_PRINTF(stream,
      "Ready. Send: 'FRMI' + size_le(4) + crc32_le(4) + %u bytes RGB%s",
      (unsigned)FRAME_EXPECTED_SIZE, lwshell_eol());

    int32_t n = _stream_read_exact(stream, hdr, FRAME_HDR_SIZE, FRAME_RX_TIMEOUT_MS);
    if (n != (int32_t)FRAME_HDR_SIZE)
    {
      CMD_PRINTF(stream, "ERROR: header timeout (%ld bytes)%s", (long)n, lwshell_eol());
      _shell_binary_rx = false;
      return LWSHELL_OK;
    }
    if (memcmp(hdr, FRAME_MAGIC, 4) != 0)
    {
      CMD_PRINTF(stream, "ERROR: bad magic%s", lwshell_eol());
      _shell_binary_rx = false;
      return LWSHELL_OK;
    }
    uint32_t size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    uint32_t expect_crc = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    if (size != FRAME_EXPECTED_SIZE)
    {
      CMD_PRINTF(stream, "ERROR: size=%lu, expected %u%s",
                 (unsigned long)size, (unsigned)FRAME_EXPECTED_SIZE, lwshell_eol());
      _shell_binary_rx = false;
      return LWSHELL_OK;
    }
    n = _stream_read_exact(stream, _frame_test_buf, size, FRAME_RX_TIMEOUT_MS);
    if (n != (int32_t)size)
    {
      CMD_PRINTF(stream, "ERROR: payload short (%ld/%lu)%s", (long)n, (unsigned long)size, lwshell_eol());
      _shell_binary_rx = false;
      return LWSHELL_OK;
    }
    uint32_t got_crc = _crc32(_frame_test_buf, size);
    if (got_crc != expect_crc)
    {
      CMD_PRINTF(stream, "ERROR: CRC mismatch (got 0x%08lx, expected 0x%08lx)%s",
                 (unsigned long)got_crc, (unsigned long)expect_crc, lwshell_eol());
      _shell_binary_rx = false;
      return LWSHELL_OK;
    }

    /* Cache flush so the NPU sees the bytes we just wrote (NN runs from RAM). */
    SCB_CleanInvalidateDCache_by_Addr((uint32_t*)_frame_test_buf, FRAME_EXPECTED_SIZE);
    _frame_loaded = true;
    /* Register the uploaded buffer as the NN test-frame override IMMEDIATELY.
     * Without this, a subsequent 'detect start' wakes NN_task with no override
     * set, so it processes a live camera frame on its first cycle. If that
     * live frame happens to be ATON-problematic (T11.6-family), the kit
     * hardfaults — making frame-injection tests inadvertently dangerous. */
    nn_task_set_test_frame(_frame_test_buf);
    CMD_PRINTF(stream, "frame upload: ok (%lu bytes, CRC 0x%08lx)%s",
               (unsigned long)size, (unsigned long)got_crc, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    _shell_binary_rx = false;
    return LWSHELL_OK;
  }

  /* `frame report on|off|query` — let the NEXT injected frame drive the §4.2
   * actions (snapshot / upload / notification) instead of being muted.
   *
   * Off by default and reset with every `frame clear`, because an injected
   * picture reaching the customer's server as a real event is worse than an
   * untested path. See nn_task_test_frame_report_set(). */
  if (strcmp(sub, "report") == 0)
  {
    const char *arg = (argc > 2) ? argv[2] : "query";
    if (strcmp(arg, "on") == 0)
    {
      nn_task_test_frame_report_set(true);
    }
    else if (strcmp(arg, "off") == 0)
    {
      nn_task_test_frame_report_set(false);
    }
    else if (strcmp(arg, "query") != 0)
    {
      CMD_PRINTF(stream, "frame report: expected on | off | query%s",
                 lwshell_eol());
      return LWSHELL_OK;
    }
    CMD_PRINTF(stream, "frame report: %s%s",
               nn_task_test_frame_report_get() ? "on — injected frames DO "
                                                 "notify and upload"
                                               : "off — injected frames are "
                                                 "not reported",
               lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "run") == 0)
  {
    if (!_frame_loaded)
    {
      CMD_PRINTF(stream, "frame run: no frame loaded (use 'frame upload' first)%s", lwshell_eol());
      return LWSHELL_OK;
    }
    if (!nn_task_detect_get())
    {
      CMD_PRINTF(stream, "frame run: NN is stopped — run 'detect start' first%s", lwshell_eol());
      return LWSHELL_OK;
    }

    /* Route NN input to our test buffer and wait for an inference that
     * actually consumed it.
     *
     * This used to be `HAL_Delay(100)` and then read the box buffer. The NN
     * loop is driven by the camera's frame event, not by this call, and one
     * inference is ~100 ms on its own — so the read frequently landed on the
     * boxes from the LIVE scene and reported them as the injected image's
     * result. A sweep of 36 test images produced the same two detections at
     * the same two confidences for image after image: it was reporting the
     * lab, not the pictures. A test that quietly measures the wrong thing is
     * worse than one that fails.
     *
     * Two inferences, not one: the first may already have been in flight on
     * the live buffer when the override was armed. */
    if (!_nn_run_on_buffer(_frame_test_buf, 3000U))
    {
      CMD_PRINTF(stream, "frame run: no inference on the injected frame "
                         "within 3s — is the camera pipeline running?%s",
                 lwshell_eol());
      return LWSHELL_OK;
    }

    uint32_t boxes = nn_task_get_box_count();
    t_nn_box  buf[NN_BOXES_MAX_NUM];
    uint32_t  got = nn_get_detections(buf, NN_BOXES_MAX_NUM);
    float nn_ms = stat_value(STAT_TIME_NN_TOTAL);
    CMD_PRINTF(stream, "frame run: %lu detection(s), NN %.1fms%s",
               (unsigned long)boxes, nn_ms, lwshell_eol());
    for (uint32_t i = 0; (i < got) && (i < 10U); i++)
    {
      CMD_PRINTF(stream, "  [%lu] class=%ld conf=%.2f bbox=(%.2f,%.2f,%.2f,%.2f)%s",
                 (unsigned long)i,
                 (long)buf[i].class_index,
                 buf[i].conf,
                 buf[i].x_center, buf[i].y_center,
                 buf[i].width,    buf[i].height,
                 lwshell_eol());
    }
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* frame dump — print raw head/tail of the most-recent NN output tensor.
   * Helps diagnose detection failures: if values look like garbage / zeros /
   * all the same → the model isn't producing usable output for the post-
   * processor regardless of conf threshold. */
  if (strcmp(sub, "dump") == 0)
  {
    float head[16], tail[16];
    uint32_t bytes = 0U;
    nn_task_dump_output(head, tail, &bytes);
    CMD_PRINTF(stream, "NN output: %lu bytes (%lu floats)%s",
               (unsigned long)bytes, (unsigned long)(bytes / 4U), lwshell_eol());
    CMD_PRINTF(stream, "  head[0..15]:%s    ", lwshell_eol());
    for (int i = 0; i < 16; i++) CMD_PRINTF(stream, "%.3f ", head[i]);
    CMD_PRINTF(stream, "%s", lwshell_eol());
    CMD_PRINTF(stream, "  tail[-16..-1]:%s    ", lwshell_eol());
    for (int i = 0; i < 16; i++) CMD_PRINTF(stream, "%.3f ", tail[i]);
    CMD_PRINTF(stream, "%s", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  return LWSHELL_ERROR_SYNTAX_CMD;
}

/* ========================================================================
 * Tiled multi-crop inference (`tile` command)
 * ------------------------------------------------------------------------
 * Idea (per I.T.P. Novex): run the camera slowly (~1 FPS) but, on each
 * frame, slice the full high-res image into an overlapping grid of square
 * tiles and run the *existing, unchanged* 256x256 yolov8n detector on each
 * tile. Small/distant people that occupy too few pixels in a single
 * full-frame downscale become resolvable inside their tile. Detections are
 * remapped to full-frame coordinates and merged with IoU-NMS so a person
 * straddling two adjacent tiles is reported once.
 *
 * This first cut is the UART-testable engine: the "full frame" is uploaded
 * over CDC (any WxH RGB888, up to the 16 MB model-update buffer we reuse as
 * scratch) rather than grabbed live off the DCMIPP. That keeps the live
 * camera / ATON path untouched while letting us validate tiling geometry +
 * NMS end-to-end with real photos via `n6cam-tile.py`.
 *
 * Buffers reused (no new PSRAM):
 *   _fwupd_model_buf  (16 MB) - holds the uploaded full frame. Only ever
 *                     used during `update model`, never concurrent with tiling.
 *   _frame_test_buf   (256x256x3) - per-tile resized crop = the NN override.
 * ======================================================================== */

#define TILE_MAX_AXIS       12U     /* max cols or rows                        */
#define TILE_MAX_DETS       256U    /* pre-NMS accumulation cap                */
#define TILE_NN_SIDE        CAMERA_ANCILLARY_WIDTH   /* 256 - NN input side    */

/* Factory defaults, mirroring the I.T.P. Novex 90-deg-FOV example: 5x4 grid,
 * 576 px square crops over a 2592x1944 sensor frame, auto overlap, conf 0.45,
 * IoU 0.40. `tile default` restores exactly these. */
#define TILE_DEF_COLS       5U
#define TILE_DEF_ROWS       4U
#define TILE_DEF_CROP       576U
#define TILE_DEF_OVL_H      0U       /* 0 = auto (even distribution)           */
#define TILE_DEF_OVL_V      0U
#define TILE_DEF_FW         2592U
#define TILE_DEF_FH         1944U
#define TILE_DEF_CONF       0.45f
#define TILE_DEF_IOU        0.40f

/* Tiling configuration (persisted between commands). */
static uint16_t _tile_cols     = TILE_DEF_COLS;
static uint16_t _tile_rows     = TILE_DEF_ROWS;
static uint16_t _tile_crop     = TILE_DEF_CROP;   /* square crop side, px       */
static uint16_t _tile_ovl_h    = TILE_DEF_OVL_H;
static uint16_t _tile_ovl_v    = TILE_DEF_OVL_V;
static uint16_t _tile_fw       = TILE_DEF_FW;     /* uploaded full-frame width  */
static uint16_t _tile_fh       = TILE_DEF_FH;     /* uploaded full-frame height */
static float    _tile_conf     = TILE_DEF_CONF;   /* keep detections >= conf    */
static float    _tile_iou      = TILE_DEF_IOU;    /* NMS suppression IoU        */
static bool     _tile_loaded   = false;  /* a full frame is in _fwupd_model_buf*/

/* Post-remap detection in full-frame normalized [0,1] corner coordinates. */
typedef struct { float x1, y1, x2, y2, conf; int32_t cls; bool keep; } _tile_det_t;
static _tile_det_t _tile_dets[TILE_MAX_DETS];

/* Human-readable name for the COCO classes we care about (person + vehicles);
 * everything else prints as its numeric index. */
static const char *_tile_class_name(int32_t c)
{
  switch (c)
  {
    case 0:  return "person";
    case 1:  return "bicycle";
    case 2:  return "car";
    case 3:  return "motorcycle";
    case 5:  return "bus";
    case 7:  return "truck";
    default: return "class";
  }
}

/* Compute the `n` tile origins along one axis of length `span`, given a square
 * `crop` and an optional explicit `ovl` overlap (0 = auto). Origins slide from
 * 0 to (span-crop); auto mode distributes them evenly so the grid always
 * covers the whole span with automatic overlap when crop*n > span. Returns the
 * effective stride via *stride_out. */
static void _tile_axis_origins(uint16_t n, uint16_t span, uint16_t crop,
                               uint16_t ovl, uint16_t *origins, uint16_t *stride_out)
{
  uint16_t c = (crop > span) ? span : crop;
  uint16_t last = (uint16_t)(span - c);
  if (n <= 1U)
  {
    origins[0] = 0U;
    *stride_out = 0U;
    return;
  }
  uint32_t stride;
  if (ovl > 0U)
  {
    stride = (c > ovl) ? (uint32_t)(c - ovl) : 1U;
  }
  else
  {
    stride = (uint32_t)last / (uint32_t)(n - 1U);
    if (stride == 0U) stride = 1U;
  }
  for (uint16_t i = 0U; i < n; i++)
  {
    uint32_t o = (uint32_t)i * stride;
    if (o > last) o = last;
    origins[i] = (uint16_t)o;
  }
  origins[n - 1U] = last;   /* guarantee the last tile reaches the far edge   */
  *stride_out = (uint16_t)stride;
}

/* Bilinear-resize the square region [cx,cy, crop x crop] of the uploaded RGB888
 * full frame into the 256x256x3 NN input buffer `dst`. Source coordinates are
 * clamped so we never read outside the frame. */
static void _tile_resize_crop(const uint8_t *src, uint16_t fw, uint16_t fh,
                              uint16_t cx, uint16_t cy, uint16_t crop, uint8_t *dst)
{
  const uint32_t N = TILE_NN_SIDE;
  const float step = (crop > 1U) ? (float)(crop - 1U) / (float)(N - 1U) : 0.0f;
  for (uint32_t oy = 0U; oy < N; oy++)
  {
    float    fy = (float)cy + (float)oy * step;
    uint32_t y0 = (uint32_t)fy;
    if (y0 > (uint32_t)(fh - 2U)) y0 = fh - 2U;
    float wy = fy - (float)y0;
    for (uint32_t ox = 0U; ox < N; ox++)
    {
      float    fx = (float)cx + (float)ox * step;
      uint32_t x0 = (uint32_t)fx;
      if (x0 > (uint32_t)(fw - 2U)) x0 = fw - 2U;
      float wx = fx - (float)x0;

      const uint8_t *p00 = src + ((size_t)y0 * fw + x0) * 3U;
      const uint8_t *p01 = p00 + 3U;
      const uint8_t *p10 = p00 + (size_t)fw * 3U;
      const uint8_t *p11 = p10 + 3U;
      uint8_t *d = dst + ((size_t)oy * N + ox) * 3U;
      for (uint32_t ch = 0U; ch < 3U; ch++)
      {
        float top = (float)p00[ch] * (1.0f - wx) + (float)p01[ch] * wx;
        float bot = (float)p10[ch] * (1.0f - wx) + (float)p11[ch] * wx;
        float v   = top * (1.0f - wy) + bot * wy;
        d[ch] = (uint8_t)(v + 0.5f);
      }
    }
  }
}

/* IoU of two normalized corner boxes. */
static float _tile_iou_of(const _tile_det_t *a, const _tile_det_t *b)
{
  float ix1 = (a->x1 > b->x1) ? a->x1 : b->x1;
  float iy1 = (a->y1 > b->y1) ? a->y1 : b->y1;
  float ix2 = (a->x2 < b->x2) ? a->x2 : b->x2;
  float iy2 = (a->y2 < b->y2) ? a->y2 : b->y2;
  float iw = ix2 - ix1, ih = iy2 - iy1;
  if (iw <= 0.0f || ih <= 0.0f) return 0.0f;
  float inter = iw * ih;
  float ua = (a->x2 - a->x1) * (a->y2 - a->y1)
           + (b->x2 - b->x1) * (b->y2 - b->y1) - inter;
  return (ua > 0.0f) ? (inter / ua) : 0.0f;
}

/* Greedy class-aware NMS over the first `n` entries of _tile_dets. Marks the
 * survivors keep=true. O(n^2), n <= TILE_MAX_DETS. */
static void _tile_nms(uint32_t n, float iou_th)
{
  for (uint32_t i = 0U; i < n; i++) _tile_dets[i].keep = true;
  for (uint32_t i = 0U; i < n; i++)
  {
    if (!_tile_dets[i].keep) continue;
    for (uint32_t j = i + 1U; j < n; j++)
    {
      if (!_tile_dets[j].keep) continue;
      if (_tile_dets[j].cls != _tile_dets[i].cls) continue;
      if (_tile_iou_of(&_tile_dets[i], &_tile_dets[j]) > iou_th)
      {
        /* keep the higher-confidence one */
        if (_tile_dets[j].conf > _tile_dets[i].conf)
        {
          _tile_dets[i].keep = false;
          break;                 /* i is gone; stop comparing it              */
        }
        _tile_dets[j].keep = false;
      }
    }
  }
}

/* Arm the NN at `buf` and block until an inference has actually run on it.
 *
 * Every caller here used to do `nn_task_set_test_frame(buf); HAL_Delay(100);`
 * and then read the box buffer. The NN loop runs off the camera's frame
 * event, not off the arming call, and one inference takes ~100 ms by itself
 * — so the read routinely returned the boxes from whatever the NN had last
 * finished, which on a live pipeline is the room the camera is pointed at.
 * That turned a 36-image sweep into 36 copies of the lab.
 *
 * Waits for TWO completions, because the first may have been in flight on
 * the previous buffer when this one was armed. Returns false on timeout,
 * which means the camera pipeline is not producing frames at all.
 */
static bool _nn_run_on_buffer(uint8_t *buf, uint32_t timeout_ms)
{
  uint32_t seq0 = nn_task_test_frame_seq();
  nn_task_set_test_frame(buf);
  uint32_t waited = 0U;
  while (((nn_task_test_frame_seq() - seq0) < 2U) && (waited < timeout_ms))
  {
    HAL_Delay(10);
    waited += 10U;
  }
  return (nn_task_test_frame_seq() - seq0) >= 2U;
}

/* `tile run` core: crop -> resize -> NN -> remap -> accumulate -> NMS. */
/* Core sweep, shared by `tile run` (uploaded frame) and `tile live` (live
 * camera snapshot): crop each grid tile from `frame` (RGB888, fw x fh),
 * bilinear-resize it to the 256x256 NN input, run the detector, remap boxes to
 * full-frame normalized coords, accumulate, then class-aware IoU-NMS. `label`
 * tags the report line ("run"/"live"). */
static int32_t _tile_run_source(const t_stream *stream, const uint8_t *frame,
                                uint16_t fw, uint16_t fh, const char *label)
{
  if (!nn_task_detect_get())
  {
    CMD_PRINTF(stream, "tile %s: NN stopped - run 'detect start' first%s", label, lwshell_eol());
    return LWSHELL_OK;
  }
  uint16_t crop = _tile_crop;               /* clamp so a crop can't exceed the */
  if (crop > fw) crop = fw;                  /* frame (whole-axis = 1 region)    */
  if (crop > fh) crop = fh;

  uint16_t xs[TILE_MAX_AXIS], ys[TILE_MAX_AXIS], sx = 0U, sy = 0U;
  _tile_axis_origins(_tile_cols, fw, crop, _tile_ovl_h, xs, &sx);
  _tile_axis_origins(_tile_rows, fh, crop, _tile_ovl_v, ys, &sy);

  const float inv_fw = 1.0f / (float)fw;
  const float inv_fh = 1.0f / (float)fh;
  uint32_t n_acc = 0U, n_infer = 0U, n_raw = 0U;
  uint32_t t0 = HAL_GetTick();

  for (uint16_t r = 0U; r < _tile_rows; r++)
  {
    for (uint16_t c = 0U; c < _tile_cols; c++)
    {
      _tile_resize_crop(frame, fw, fh, xs[c], ys[r], crop, _frame_test_buf);
      SCB_CleanInvalidateDCache_by_Addr((uint32_t*)_frame_test_buf, FRAME_EXPECTED_SIZE);

      /* Route the NN at this tile and wait for it to actually run on it. */
      if (!_nn_run_on_buffer(_frame_test_buf, 3000U))
      {
        CMD_PRINTF(stream, "tile %s: no inference on tile (%u,%u) within 3s "
                           "— is the camera pipeline running?%s",
                   label, (unsigned)c, (unsigned)r, lwshell_eol());
        return LWSHELL_OK;
      }
      n_infer++;

      t_nn_box buf[NN_BOXES_MAX_NUM];
      uint32_t got = nn_get_detections(buf, NN_BOXES_MAX_NUM);
      for (uint32_t i = 0U; i < got; i++)
      {
        n_raw++;
        if (buf[i].conf < _tile_conf) continue;
        if (n_acc >= TILE_MAX_DETS) continue;
        /* box is normalized [0,1] within the tile -> tile px -> full-frame px
         * -> full-frame normalized. */
        float bx = (float)xs[c] + buf[i].x_center * (float)crop;
        float by = (float)ys[r] + buf[i].y_center * (float)crop;
        float bw = buf[i].width  * (float)crop;
        float bh = buf[i].height * (float)crop;
        _tile_det_t *d = &_tile_dets[n_acc++];
        d->x1   = (bx - bw * 0.5f) * inv_fw;
        d->y1   = (by - bh * 0.5f) * inv_fh;
        d->x2   = (bx + bw * 0.5f) * inv_fw;
        d->y2   = (by + bh * 0.5f) * inv_fh;
        d->conf = buf[i].conf;
        d->cls  = buf[i].class_index;
        d->keep = true;
      }
    }
  }

  _tile_nms(n_acc, _tile_iou);
  uint32_t elapsed = HAL_GetTick() - t0;

  uint32_t kept = 0U;
  for (uint32_t i = 0U; i < n_acc; i++) if (_tile_dets[i].keep) kept++;

  CMD_PRINTF(stream,
    "tile %s: %lu tiles (%ux%u) crop %u over %ux%u, %lu raw -> %lu over-thresh -> %lu after NMS, %lu ms%s",
    label, (unsigned long)n_infer, (unsigned)_tile_cols, (unsigned)_tile_rows,
    (unsigned)crop, (unsigned)fw, (unsigned)fh,
    (unsigned long)n_raw, (unsigned long)n_acc, (unsigned long)kept,
    (unsigned long)elapsed, lwshell_eol());
  if (n_acc >= TILE_MAX_DETS)
  {
    CMD_PRINTF(stream, "  (warning: accumulation capped at %u; some tiles truncated)%s",
               (unsigned)TILE_MAX_DETS, lwshell_eol());
  }
  uint32_t shown = 0U;
  for (uint32_t i = 0U; i < n_acc; i++)
  {
    if (!_tile_dets[i].keep) continue;
    _tile_det_t *d = &_tile_dets[i];
    CMD_PRINTF(stream,
      "  [%lu] %s(%ld) conf=%.2f bbox=(%.3f,%.3f,%.3f,%.3f)%s",
      (unsigned long)shown, _tile_class_name(d->cls), (long)d->cls, d->conf,
      d->x1, d->y1, d->x2, d->y2, lwshell_eol());
    if (++shown >= 32U) { CMD_PRINTF(stream, "  ... (%lu more)%s",
        (unsigned long)(kept - shown), lwshell_eol()); break; }
  }
  return LWSHELL_OK;
}

/* `tile run` - tiled sweep of the frame uploaded via `tile upload`. */
static int32_t _tile_run(const t_stream *stream)
{
  if (!_tile_loaded)
  {
    CMD_PRINTF(stream, "tile run: no frame (use 'tile upload' first)%s", lwshell_eol());
    return LWSHELL_OK;
  }
  return _tile_run_source(stream, _fwupd_model_buf, _tile_fw, _tile_fh, "run");
}

/* `tile live [n]` - bring tiling to the LIVE camera. The DCMIPP main pipe
 * already carries the full FOV at 800x600 (higher-res than the 256x256 NN
 * ancillary), so we snapshot it, expand RGB565->RGB888, and run the same
 * tiling engine on it. This recovers small/distant targets that the single
 * ancillary downscale loses, without touching the DCMIPP crop / ATON path.
 * `n` back-to-back sweeps (default 1). */
static int32_t _tile_live(const t_stream *stream, uint32_t sweeps)
{
  if (!nn_task_detect_get())
  {
    CMD_PRINTF(stream, "tile live: NN stopped - run 'detect start' first%s", lwshell_eol());
    return LWSHELL_OK;
  }
  const uint16_t mw = (uint16_t)CAMERA_MAIN_WIDTH;
  const uint16_t mh = (uint16_t)CAMERA_MAIN_HEIGHT;
  uint8_t *rgb = _fwupd_model_buf;    /* RGB888 snapshot dest (mw*mh*3 bytes)   */

  for (uint32_t s = 0U; s < sweeps; s++)
  {
    uint8_t *src = camera_get_buffer(DCMIPP_PIPE1);   /* live main RGB565       */
    if (src == NULL)
    {
      CMD_PRINTF(stream, "tile live: no camera buffer (camera not streaming?)%s", lwshell_eol());
      return LWSHELL_OK;
    }
    /* Snapshot + expand RGB565 -> RGB888 so the unchanged resize/NN path sees
     * the same layout as an uploaded frame. Invalidate first: the pipe is
     * DMA'd into PSRAM by the camera behind the D-cache. */
    SCB_InvalidateDCache_by_Addr((uint32_t*)src, (int32_t)((uint32_t)mw * mh * 2U));
    for (uint32_t p = 0U; p < (uint32_t)mw * mh; p++)
    {
      uint16_t px = (uint16_t)src[p * 2U] | ((uint16_t)src[p * 2U + 1U] << 8);
      uint8_t r5 = (uint8_t)((px >> 11) & 0x1FU);
      uint8_t g6 = (uint8_t)((px >> 5)  & 0x3FU);
      uint8_t b5 = (uint8_t)( px        & 0x1FU);
      rgb[p * 3U + 0U] = (uint8_t)((r5 << 3) | (r5 >> 2));
      rgb[p * 3U + 1U] = (uint8_t)((g6 << 2) | (g6 >> 4));
      rgb[p * 3U + 2U] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
    _tile_run_source(stream, rgb, mw, mh, "live");
  }
  _tile_loaded = false;             /* the live snapshot clobbered upload scratch */
  nn_task_set_test_frame(NULL);     /* hand the NN back to the live camera        */
  return LWSHELL_OK;
}

/* Tiled multi-crop inference. See the block comment above. */
static int32_t _tile_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;
  const char *sub = (const char*)argv[1];

  if (strcmp(sub, "grid") == 0)
  {
    if (argc < 4U) return LWSHELL_ERROR_SYNTAX_CMD;
    long c = atol((char*)argv[2]);
    long r = atol((char*)argv[3]);
    if (c < 1 || c > (long)TILE_MAX_AXIS || r < 1 || r > (long)TILE_MAX_AXIS)
    {
      CMD_PRINTF(stream, "tile grid: cols/rows must be 1..%u%s",
                 (unsigned)TILE_MAX_AXIS, lwshell_eol());
      return LWSHELL_OK;
    }
    _tile_cols = (uint16_t)c;
    _tile_rows = (uint16_t)r;
    CMD_PRINTF(stream, "tile: grid %ux%u (%u tiles)%s",
               (unsigned)_tile_cols, (unsigned)_tile_rows,
               (unsigned)(_tile_cols * _tile_rows), lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "crop") == 0)
  {
    if (argc < 3U) return LWSHELL_ERROR_SYNTAX_CMD;
    long px = atol((char*)argv[2]);
    if (px < (long)TILE_NN_SIDE || px > 4096)
    {
      CMD_PRINTF(stream, "tile crop: px must be %u..4096%s",
                 (unsigned)TILE_NN_SIDE, lwshell_eol());
      return LWSHELL_OK;
    }
    _tile_crop = (uint16_t)px;
    CMD_PRINTF(stream, "tile: crop %u px%s", (unsigned)_tile_crop, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "frame") == 0)
  {
    if (argc < 4U) return LWSHELL_ERROR_SYNTAX_CMD;
    long w = atol((char*)argv[2]);
    long h = atol((char*)argv[3]);
    if (w < (long)TILE_NN_SIDE || h < (long)TILE_NN_SIDE || w > 8192 || h > 8192 ||
        (uint32_t)(w * h * 3) > FWUPD_MODEL_MAX_SIZE)
    {
      CMD_PRINTF(stream, "tile frame: WxHx3 must be %u..%lu bytes%s",
                 (unsigned)TILE_NN_SIDE, (unsigned long)FWUPD_MODEL_MAX_SIZE, lwshell_eol());
      return LWSHELL_OK;
    }
    _tile_fw = (uint16_t)w;
    _tile_fh = (uint16_t)h;
    _tile_loaded = false;   /* dimensions changed; require a fresh upload      */
    CMD_PRINTF(stream, "tile: frame %ux%u (upload %lu bytes)%s",
               (unsigned)_tile_fw, (unsigned)_tile_fh,
               (unsigned long)((uint32_t)w * h * 3U), lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "overlap") == 0)
  {
    if (argc < 4U) return LWSHELL_ERROR_SYNTAX_CMD;
    long oh = atol((char*)argv[2]);
    long ov = atol((char*)argv[3]);
    if (oh < 0 || ov < 0 || oh >= (long)_tile_crop || ov >= (long)_tile_crop)
    {
      CMD_PRINTF(stream, "tile overlap: 0..crop-1 (0 = auto)%s", lwshell_eol());
      return LWSHELL_OK;
    }
    _tile_ovl_h = (uint16_t)oh;
    _tile_ovl_v = (uint16_t)ov;
    CMD_PRINTF(stream, "tile: overlap h=%u v=%u%s%s",
               (unsigned)_tile_ovl_h, (unsigned)_tile_ovl_v,
               (_tile_ovl_h == 0U && _tile_ovl_v == 0U) ? " (auto)" : "",
               lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "thresh") == 0)
  {
    if (argc < 4U) return LWSHELL_ERROR_SYNTAX_CMD;
    long cp = atol((char*)argv[2]);   /* conf percent */
    long ip = atol((char*)argv[3]);   /* iou  percent */
    if (cp < 0 || cp > 100 || ip < 0 || ip > 100)
    {
      CMD_PRINTF(stream, "tile thresh: <conf%%> <iou%%>, each 0..100%s", lwshell_eol());
      return LWSHELL_OK;
    }
    _tile_conf = (float)cp / 100.0f;
    _tile_iou  = (float)ip / 100.0f;
    CMD_PRINTF(stream, "tile: conf>=%.2f iou=%.2f%s", _tile_conf, _tile_iou, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "query") == 0)
  {
    uint16_t xs[TILE_MAX_AXIS], ys[TILE_MAX_AXIS], sx = 0U, sy = 0U;
    _tile_axis_origins(_tile_cols, _tile_fw, _tile_crop, _tile_ovl_h, xs, &sx);
    _tile_axis_origins(_tile_rows, _tile_fh, _tile_crop, _tile_ovl_v, ys, &sy);
    uint16_t eff_oh = (_tile_crop > sx) ? (uint16_t)(_tile_crop - sx) : 0U;
    uint16_t eff_ov = (_tile_crop > sy) ? (uint16_t)(_tile_crop - sy) : 0U;
    CMD_PRINTF(stream, "tile: frame %ux%u grid %ux%u crop %u -> NN %u%s",
               (unsigned)_tile_fw, (unsigned)_tile_fh, (unsigned)_tile_cols,
               (unsigned)_tile_rows, (unsigned)_tile_crop, (unsigned)TILE_NN_SIDE, lwshell_eol());
    CMD_PRINTF(stream, "  stride h=%u v=%u  overlap h=%u v=%u  conf>=%.2f iou=%.2f  %s%s",
               (unsigned)sx, (unsigned)sy, (unsigned)eff_oh, (unsigned)eff_ov,
               _tile_conf, _tile_iou, _tile_loaded ? "frame LOADED" : "frame EMPTY",
               lwshell_eol());
    for (uint16_t r = 0U; r < _tile_rows; r++)
    {
      for (uint16_t c = 0U; c < _tile_cols; c++)
      {
        CMD_PRINTF(stream, "  tile[%u,%u] @ (%u,%u)%s",
                   (unsigned)c, (unsigned)r, (unsigned)xs[c], (unsigned)ys[r], lwshell_eol());
      }
    }
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "clear") == 0)
  {
    nn_task_set_test_frame(NULL);
    _tile_loaded = false;
    CMD_PRINTF(stream, "tile: cleared (NN back to live camera)%s", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "upload") == 0)
  {
    uint32_t expect = (uint32_t)_tile_fw * (uint32_t)_tile_fh * 3U;
    uint8_t  hdr[FRAME_HDR_SIZE];
    CMD_PRINTF(stream,
      "Ready. Send: 'FRMI' + size_le(4) + crc32_le(4) + %lu bytes RGB (%ux%u)%s",
      (unsigned long)expect, (unsigned)_tile_fw, (unsigned)_tile_fh, lwshell_eol());

    int32_t n = _stream_read_exact(stream, hdr, FRAME_HDR_SIZE, FRAME_RX_TIMEOUT_MS);
    if (n != (int32_t)FRAME_HDR_SIZE)
    {
      CMD_PRINTF(stream, "ERROR: header timeout (%ld bytes)%s", (long)n, lwshell_eol());
      return LWSHELL_OK;
    }
    if (memcmp(hdr, FRAME_MAGIC, 4) != 0)
    {
      CMD_PRINTF(stream, "ERROR: bad magic%s", lwshell_eol());
      return LWSHELL_OK;
    }
    uint32_t size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    uint32_t expect_crc = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    if (size != expect)
    {
      CMD_PRINTF(stream, "ERROR: size=%lu, expected %lu (set 'tile frame W H' first)%s",
                 (unsigned long)size, (unsigned long)expect, lwshell_eol());
      return LWSHELL_OK;
    }
    n = _stream_read_exact(stream, _fwupd_model_buf, size, FRAME_RX_TIMEOUT_MS);
    if (n != (int32_t)size)
    {
      CMD_PRINTF(stream, "ERROR: payload short (%ld/%lu)%s", (long)n, (unsigned long)size, lwshell_eol());
      return LWSHELL_OK;
    }
    uint32_t got_crc = _crc32(_fwupd_model_buf, size);
    if (got_crc != expect_crc)
    {
      CMD_PRINTF(stream, "ERROR: CRC mismatch (got 0x%08lx, expected 0x%08lx)%s",
                 (unsigned long)got_crc, (unsigned long)expect_crc, lwshell_eol());
      return LWSHELL_OK;
    }
    SCB_CleanInvalidateDCache_by_Addr((uint32_t*)_fwupd_model_buf, size);
    _tile_loaded = true;
    CMD_PRINTF(stream, "tile upload: ok (%lu bytes, CRC 0x%08lx)%s",
               (unsigned long)size, (unsigned long)got_crc, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "run") == 0)
  {
    int32_t rc = _tile_run(stream);
    if (rc == LWSHELL_OK) _cmd_ack(stream, argv, argc);
    return rc;
  }

  if (strcmp(sub, "live") == 0)
  {
    uint32_t n = 1U;
    if (argc >= 3U)
    {
      long v = atol((char*)argv[2]);
      if (v < 1 || v > 100)
      {
        CMD_PRINTF(stream, "tile live: sweeps must be 1..100%s", lwshell_eol());
        return LWSHELL_OK;
      }
      n = (uint32_t)v;
    }
    int32_t rc = _tile_live(stream, n);
    if (rc == LWSHELL_OK) _cmd_ack(stream, argv, argc);
    return rc;
  }

  if (strcmp(sub, "default") == 0)
  {
    _tile_cols  = TILE_DEF_COLS;  _tile_rows  = TILE_DEF_ROWS;
    _tile_crop  = TILE_DEF_CROP;
    _tile_ovl_h = TILE_DEF_OVL_H; _tile_ovl_v = TILE_DEF_OVL_V;
    _tile_fw    = TILE_DEF_FW;    _tile_fh    = TILE_DEF_FH;
    _tile_conf  = TILE_DEF_CONF;  _tile_iou   = TILE_DEF_IOU;
    _tile_loaded = false;
    nn_task_set_test_frame(NULL);   /* also drop any stale NN override           */
    CMD_PRINTF(stream,
      "tile: restored defaults (grid %ux%u crop %u frame %ux%u overlap auto conf>=%.2f iou=%.2f)%s",
      (unsigned)_tile_cols, (unsigned)_tile_rows, (unsigned)_tile_crop,
      (unsigned)_tile_fw, (unsigned)_tile_fh, _tile_conf, _tile_iou, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  return LWSHELL_ERROR_SYNTAX_CMD;
}

/* SoW §3.2 / W10 SD card management.
 *   sd query        - print mount + presence
 *   sd ls           - list files in root dir (truncates to ~1.5KB output)
 *   sd format CONFIRM - reformat as FAT32 (DESTRUCTIVE). 'CONFIRM' token
 *                     required so a typo doesn't wipe the card.
 */
static int32_t _sd_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;
  const char *sub = (const char*)argv[1];

  if (strcmp(sub, "query") == 0)
  {
    CMD_PRINTF(stream, "sd: %s%s",
               fx_app_is_open() ? "mounted" : "not mounted",
               lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strcmp(sub, "ls") == 0)
  {
    /* Buffer sized to hold ~200 entries of "<filename-up-to-40-chars> <size>\n".
     * Each CMD_PRINTF caps at SHELL_OUT_SIZE bytes (currently 1 KB), so
     * we can't dump the whole buffer in one call — the listing would be
     * truncated past entry ~38 and newly-created files (which sit at
     * the tail of the FAT directory) would be silently invisible to the
     * shell even though fx_file_open could still read them. Chunk the
     * output instead, one line at a time, so the full listing reaches
     * the stream regardless of how many files are on media.
     *
     * Static-allocated so we don't blow the shell command's stack. */
    static char ls_buf[8192];
    int32_t status = fx_app_list_root(ls_buf, sizeof(ls_buf));
    if (status != FX_SUCCESS)
    {
      CMD_PRINTF(stream, "sd ls: failed (%ld)%s", (long)status, lwshell_eol());
      return LWSHELL_OK;
    }
    /* Walk ls_buf line by line, printing each through CMD_PRINTF. The
     * lines from fx_app_list_root already end in '\n', so we restore
     * that as the LF in lwshell_eol() doesn't matter — but to keep
     * terminals happy emit CRLF via lwshell_eol(). */
    char *p = ls_buf;
    while (*p)
    {
      char *nl = strchr(p, '\n');
      if (nl) { *nl = '\0'; }
      CMD_PRINTF(stream, "%s%s", p, lwshell_eol());
      if (!nl) break;
      p = nl + 1;
    }
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if ((strcmp(sub, "format") == 0) && (argc >= 3U) &&
      (strcmp((char*)argv[2], "CONFIRM") == 0))
  {
    CMD_PRINTF(stream, "sd format: erasing...%s", lwshell_eol());
    int32_t status = fx_app_format();
    if (status != FX_SUCCESS)
    {
      CMD_PRINTF(stream, "sd format: failed (%ld)%s", (long)status, lwshell_eol());
      return LWSHELL_OK;
    }
    CMD_PRINTF(stream, "sd format: done%s", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  return LWSHELL_ERROR_SYNTAX_CMD;
}

/* SoW §3.1 / §4.2 / §7: photo savesd | upload.
 *
 * Generates the SoW §7 filename: serial_DDMMYYYY_HHMMSS.rdy
 * Emits a '+SDVRNTF: ... rsn=0x40' notification with the filename as
 * mod-string so the host (and later the modem) can correlate.
 *
 * Actual JPEG capture + SD/UART transport is proposal W7/W12/W13. The
 * call sites here will trigger jpeg_encode() and route the bytes to
 * SD (via fx_app) for savesd, or HDLC->SDVR+SENDBIN for upload, once
 * those tasks are wired. */
static int32_t _photo_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;
  const char *sub = (const char*)argv[1];

  bool savesd = (strcmp(sub, "savesd") == 0);
  bool upload = (strcmp(sub, "upload") == 0);
  if (!savesd && !upload) return LWSHELL_ERROR_SYNTAX_CMD;

  /* Build the filename per SoW §7: serial_DDMMYYYY_HHMMSS.rdy */
  t_datetime dt = { 0 };
  (void)bsp_rtc_get_time(&dt);
  uint32_t ser = HAL_GetUIDw0();
  char fname[48];
  snprintf(fname, sizeof(fname),
           "%lu_%02u%02u20%02u_%02u%02u%02u.rdy",
           (unsigned long)ser,
           (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year,
           (unsigned)dt.hours, (unsigned)dt.minutes, (unsigned)dt.seconds);

  if (savesd)
  {
    /* Claim a snapshot slot atomically — display_task will call
     * snapshot_create on the next camera frame, the snapshot task
     * encodes the JPEG and writes it to SD via fx_app_write_file_exact.
     * Using snapshot_request (atomic) instead of set_filename+trigger
     * avoids races with nn_task's auto-detect path that, when detect
     * is running, would otherwise overwrite our filename and silently
     * drop our write. */
    if (!snapshot_request(fname))
    {
      CMD_PRINTF(stream, "photo savesd: trigger failed (no SD card / busy)%s", lwshell_eol());
      return LWSHELL_OK;
    }
    CMD_PRINTF(stream, "photo savesd: capturing -> %s%s", fname, lwshell_eol());
  }
  else
  {
    /* SoW §8.2: capture a JPEG and ship it to the MangOH over USART2 in an
     * SDVR+SENDBIN binary transport, atomically (snapshot_task wraps both
     * the prefix line and the JPEG payload under the modem tx mutex so a
     * concurrent shell `mdm <at>` can't interleave). On the modem side
     * this hits the same SDVR HTTP-upload pipeline the SD-file uploads
     * use, just sourced from RAM instead of the FAT. */
    static uint32_t _upload_ref;
    _upload_ref++;
    /* The tag becomes the server-side file name (SENDBIN tag -> X-Filename),
     * so it must be the unique §7 name and not a constant: sending the
     * literal "photo" every time made the receiver overwrite one file called
     * `photo`, and only the last picture of a test run survived.
     * (ScopusQA #4.) */
    if (!snapshot_request_upload(fname, _upload_ref, fname))
    {
      CMD_PRINTF(stream, "photo upload: trigger failed (busy / no modem)%s",
                 lwshell_eol());
      return LWSHELL_OK;
    }
    CMD_PRINTF(stream, "photo upload: capturing -> SDVR+SENDBIN ref=%lu name=%s%s",
               (unsigned long)_upload_ref, fname, lwshell_eol());
  }

  /* Emit a notification carrying the action + filename. rsn=0x40 = photo-event
   * bit (extension to SoW §4.2's bit table). rsd encodes savesd vs upload. */
  _notify_emit(NOTIFY_RSN_PHOTO, savesd ? 1U : 2U);

  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/* SoW §3.1 / §4.2: notify enable <mask> | disable | trigger <code> |
 *                  period <seconds> | query.
 *
 * Bitmask values (SoW §4.2):
 *   x1   Network registration
 *   x2   Motion Start
 *   x4   Motion Stop
 *   x8   Periodic
 *   x10  People detected
 *   x20  Vehicle detected
 *
 * 'trigger <code>' immediately emits a JSON notification with rsn=code.
 * Periodic notifications wire into the system_task heartbeat later. */
static int32_t _notify_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;
  const char *sub = (const char*)argv[1];

  if ((strcmp(sub, "enable") == 0) && (argc >= 3U))
  {
    unsigned long mask = strtoul((char*)argv[2], NULL, 0);
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->notify_enable_mask = (uint32_t)mask; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "notify enable: 0x%08lx%s", mask, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strcmp(sub, "disable") == 0)
  {
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->notify_enable_mask = 0U; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "notify enable: 0x00000000%s", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if ((strcmp(sub, "trigger") == 0) && (argc >= 3U))
  {
    unsigned long code = strtoul((char*)argv[2], NULL, 0);
    /* Explicit operator request — always sent, mask or no mask. */
    _notify_emit_ex((uint32_t)code, 0U, true);
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if ((strcmp(sub, "period") == 0) && (argc >= 3U))
  {
    long s = atol((char*)argv[2]);
    if ((s < 0) || (s > (long)NOTIFY_PERIOD_MAX_S))
    {
      CMD_PRINTF(stream, "notify period: seconds must be 0..%lu%s",
                 (unsigned long)NOTIFY_PERIOD_MAX_S, lwshell_eol());
      return LWSHELL_ERROR_SYNTAX_CMD;
    }
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->notify_period_s = (uint32_t)s; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "notify period: %lds%s", s, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strcmp(sub, "query") == 0)
  {
    uint32_t m = 0U, p = 0U;
    t_registry_data *reg = registry_acquire();
    if (reg) { m = reg->notify_enable_mask; p = reg->notify_period_s; registry_release(); }
    CMD_PRINTF(stream, "notify: enable_mask=0x%08lx period=%lus num=%lu%s",
               (unsigned long)m, (unsigned long)p, (unsigned long)_notify_num, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  return LWSHELL_ERROR_SYNTAX_CMD;
}

/* SoW §3.1 / §4.2: detect start | stop | profile <det_msk> <action_msk> |
 *                 profile query.
 *
 *   det_msk    bit0 = people, bit1 = vehicles
 *   action_msk bit0 = save SD, bit1 = report cellular
 *
 * 'start'/'stop' gates the NN task's inference (camera + UVC keep running).
 * The bitmasks are stored in registry; downstream tasks (photo capture,
 * notification subsystem) read them. */
static int32_t _detect_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;
  const char *sub = (const char*)argv[1];

  if (strcmp(sub, "start") == 0)
  {
    nn_task_detect_set(true);
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->detect_enable = 1U; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "detect: started%s", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strcmp(sub, "stop") == 0)
  {
    nn_task_detect_set(false);
    t_registry_data *reg = registry_acquire();
    if (reg) { reg->detect_enable = 0U; registry_release(); registry_request_save(); }
    CMD_PRINTF(stream, "detect: stopped%s", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strcmp(sub, "simulate") == 0)
  {
    /* default to 1 box if no count given.
     *
     * The count is bounded here as well as in nn_task_simulate_detection_class:
     * the published set can never exceed NN_BOXES_MAX_NUM, so a larger number
     * is an operator mistake and saying so is more use than silently clamping
     * it and then reporting a count the unit did not accept. */
    uint32_t boxes = 1U;
    if (argc >= 3U)
    {
      long n = atol((char*)argv[2]);
      if ((n < 0) || (n > (long)NN_BOXES_MAX_NUM))
      {
        CMD_PRINTF(stream, "detect simulate: N must be 0..%u%s",
                   (unsigned)NN_BOXES_MAX_NUM, lwshell_eol());
        return LWSHELL_ERROR_SYNTAX_CMD;
      }
      boxes = (uint32_t)n;
    }
    /* Optional class: `detect simulate 2 vehicle`. The detector is
     * person+vehicle, but with no car available to point the lens at there
     * was no way to exercise the §4.2 `0x20` path on the bench. */
    bool vehicle = (argc >= 4U) &&
                   ((strcmp((char*)argv[3], "vehicle") == 0) ||
                    (strcmp((char*)argv[3], "car") == 0));
    nn_task_simulate_detection_class(boxes, vehicle ? 2 : 0);
    CMD_PRINTF(stream, "detect simulate: %lu %s object(s)%s",
               (unsigned long)boxes, vehicle ? "vehicle" : "people",
               lwshell_eol());
    /* Also fire the +SDVRNTF the inference loop would have, under the reason
     * code for the class that was asserted. */
    _notify_emit(vehicle ? NOTIFY_RSN_VEHICLE : NOTIFY_RSN_PEOPLE, boxes);
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  /* detect stats — what the automatic photo upload (action bit2) has been
   * doing. Both counters climbing is the scene changing faster than the
   * link can carry pictures of it, which is information, not a fault. */
  if (strcmp(sub, "stats") == 0)
  {
    uint32_t skipped = 0U, busy = 0U;
    nn_task_upload_stats(&skipped, &busy);
    CMD_PRINTF(stream, "detect stats: auto-upload skipped=%lu busy=%lu%s",
               (unsigned long)skipped, (unsigned long)busy, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  /* detect debounce <ms> | detect debounce query
   *
   * How long a new count must hold before it is believed and reported.
   * Every change of that debounced count raises an event, so this is the
   * single knob between "a flickering detector reports every few seconds"
   * and "a change is missed". Default 1000 ms. */
  if (strcmp(sub, "debounce") == 0)
  {
    if ((argc >= 3U) && (strcmp((char*)argv[2], "query") != 0))
    {
      long ms = strtol((char*)argv[2], NULL, 0);
      if ((ms < 0) || (ms > 600000)) return LWSHELL_ERROR_SYNTAX_CMD;
      nn_task_debounce_set((uint32_t)ms);
      t_registry_data *reg = registry_acquire();
      if (reg)
      {
        reg->detect_debounce_ms = (uint32_t)ms;
        registry_release();
        registry_request_save();
      }
    }
    CMD_PRINTF(stream, "detect debounce: %lu ms%s",
               (unsigned long)nn_task_debounce_get(), lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strcmp(sub, "profile") == 0)
  {
    /* profile query */
    if ((argc >= 3U) && (strcmp((char*)argv[2], "query") == 0))
    {
      uint8_t dm = 0U, am = 0U;
      t_registry_data *reg = registry_acquire();
      if (reg) { dm = reg->detect_det_mask; am = reg->detect_action_mask; registry_release(); }
      CMD_PRINTF(stream, "detect profile: det_msk=0x%02x action_msk=0x%02x%s",
                 (unsigned)dm, (unsigned)am, lwshell_eol());
      _cmd_ack(stream, argv, argc);
      return LWSHELL_OK;
    }
    /* profile <det_msk> <action_msk> */
    if (argc >= 4U)
    {
      long dm = strtol((char*)argv[2], NULL, 0);
      long am = strtol((char*)argv[3], NULL, 0);
      if ((dm < 0) || (dm > 0xFF) || (am < 0) || (am > 0xFF))
      {
        return LWSHELL_ERROR_SYNTAX_CMD;
      }
      t_registry_data *reg = registry_acquire();
      if (reg)
      {
        reg->detect_det_mask    = (uint8_t)dm;
        reg->detect_action_mask = (uint8_t)am;
        registry_release();
        registry_request_save();
      }
      nn_task_det_set((uint8_t)dm);
      nn_task_action_set((uint8_t)am);
      CMD_PRINTF(stream, "detect profile: det_msk=0x%02lx action_msk=0x%02lx%s",
                 (unsigned long)dm, (unsigned long)am, lwshell_eol());
      _cmd_ack(stream, argv, argc);
      return LWSHELL_OK;
    }
    return LWSHELL_ERROR_SYNTAX_CMD;
  }
  return LWSHELL_ERROR_SYNTAX_CMD;
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

/* SoW §3.5/§4.5 — the board's movement sensor.
 *
 *   motion sense <sensitivity 0..100> <no_motion_timeout_s>
 *   motion query        what is configured, and what the sensor is doing
 *   motion read         the raw acceleration vector, in mg
 *   motion selftest     make the sensor itself produce motion, with no hands
 *   motion simulate <0|1>   assert the state, bypassing the sensor
 *
 * This is the *unit* moving — the box picked up, knocked or tilted — and not
 * objects moving in the field of view; the two were conflated once and the
 * §4.2 motion bits were wired to the detector's box count, which is not what
 * §4.5 describes. Detection of people and vehicles has its own reason codes
 * (0x10 / 0x20) and is untouched by anything here.
 *
 * The parameters persist in the registry and are applied to the running
 * detector immediately: `motion sense` is expected to change how the unit
 * behaves now, not after the next reboot. */
static int32_t _motion_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U) return LWSHELL_ERROR_SYNTAX_CMD;
  const char *sub = (const char*)argv[1];

  if ((argc >= 4U) && (strcmp(sub, "sense") == 0))
  {
    int  s = atoi((char*)argv[2]);
    long t = atol((char*)argv[3]);
    if ((s < 0) || (s > 100) || (t < 0) || (t > (long)MOTION_TIMEOUT_MAX_S))
    {
      CMD_PRINTF(stream, "motion sense: sensitivity 0..100, timeout 0..%lu s%s",
                 (unsigned long)MOTION_TIMEOUT_MAX_S, lwshell_eol());
      return LWSHELL_ERROR_SYNTAX_CMD;
    }

    t_registry_data *reg = registry_acquire();
    if (reg)
    {
      reg->motion_sensitivity         = (uint8_t)s;
      reg->motion_no_motion_timeout_s = (uint32_t)t;
      registry_release();
      registry_request_save();
    }
    motion_sensor_config((uint8_t)s, (uint32_t)t);

    t_motion_status st;
    motion_sensor_status(&st);
    CMD_PRINTF(stream, "motion: sensitivity=%u timeout=%lu (threshold %lu mg)%s",
               (unsigned)st.sensitivity, (unsigned long)st.timeout_s,
               (unsigned long)st.threshold_mg, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "query") == 0)
  {
    t_motion_status st;
    motion_sensor_status(&st);

    /* The first line is the §4.5 answer and has kept its wording since the
     * parameters were storage-only, because the test suite and the tester's
     * manual both read it. Everything the sensor knows follows underneath. */
    CMD_PRINTF(stream, "motion: sensitivity=%u timeout=%lu%s",
               (unsigned)st.sensitivity, (unsigned long)st.timeout_s,
               lwshell_eol());
    if (st.present)
    {
      CMD_PRINTF(stream, "motion: sensor LSM6DSO32 at 0x%02X, threshold %lu mg%s",
                 (unsigned)st.addr, (unsigned long)st.threshold_mg, lwshell_eol());
      CMD_PRINTF(stream, "motion: state=%s%s deviation=%lu mg peak=%lu mg still=%lu s%s",
                 st.active ? "moving" : "still",
                 st.forced ? " (simulated)" : "",
                 (unsigned long)st.last_dev_mg, (unsigned long)st.peak_dev_mg,
                 (unsigned long)st.still_s, lwshell_eol());
      CMD_PRINTF(stream, "motion: events start=%lu stop=%lu%s",
                 (unsigned long)st.starts, (unsigned long)st.stops, lwshell_eol());
    }
    else
    {
      CMD_PRINTF(stream, "motion: sensor not present — start/stop not produced%s",
                 lwshell_eol());
    }
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "read") == 0)
  {
    int32_t mg[3] = { 0 };
    if (motion_sensor_read(mg) != BSP_OK)
    {
      CMD_PRINTF(stream, "motion read: sensor not available%s", lwshell_eol());
      return LWSHELL_OK;
    }
    /* At rest one axis reads about ±1000 — that is gravity, and it is the
     * cheapest proof the part is alive and mounted the way we think. */
    CMD_PRINTF(stream, "motion read: x=%ld y=%ld z=%ld mg%s",
               (long)mg[0], (long)mg[1], (long)mg[2], lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if (strcmp(sub, "selftest") == 0)
  {
    /* The sensor's electrostatic self-test deflects the proof mass, so the
     * step it produces travels the whole real path — filter, threshold, state
     * machine, notification. It is how a bench with nobody near it exercises
     * §4.2 motion start/stop for real. */
    int32_t delta = 0;
    int32_t rc = motion_sensor_selftest(&delta);
    CMD_PRINTF(stream, "motion selftest: %s (shift %ld mg)%s",
               (rc == BSP_OK) ? "sensor responded" : "no valid response",
               (long)delta, lwshell_eol());
    if (rc != BSP_OK) { return LWSHELL_OK; }
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  if ((argc >= 3U) && (strcmp(sub, "simulate") == 0))
  {
    /* Transport only — this asserts the state and skips the sensor, the way
     * `detect simulate` asserts a scene. `motion selftest` is the one to use
     * when the sensor is meant to be part of what is under test. */
    const char *arg = (const char*)argv[2];
    bool on = (strcmp(arg, "1") == 0) || (strcmp(arg, "on") == 0) ||
              (strcmp(arg, "start") == 0);
    bool off = (strcmp(arg, "0") == 0) || (strcmp(arg, "off") == 0) ||
               (strcmp(arg, "stop") == 0);
    if (!on && !off) { return LWSHELL_ERROR_SYNTAX_CMD; }

    motion_sensor_force(on);
    CMD_PRINTF(stream, "motion simulate: %s%s", on ? "moving" : "still",
               lwshell_eol());
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

/* safeboot [status | clear | test]
 *   status — print current boot_count, threshold, safe-mode flag
 *   clear  — zero the counter (use after the bad model/app is replaced
 *            so the next reboot starts at 1 again, not at threshold)
 *   test   — set counter to threshold so the next boot engages safe
 *            mode (NN auto-start suppressed). Verification + drill aid. */
static int32_t _safeboot_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  uint8_t bn = 0U;
  {
    t_registry_data *reg = registry_acquire();
    if (reg != NULL)
    {
      bn = reg->boot_count;
      registry_release();
    }
  }
  bool safe = (bn >= BOOT_GUARD_THRESHOLD);

  if (argc < 2U || strncmp((const char*)argv[1], "status", 6) == 0)
  {
    CMD_PRINTF(stream, "boot_count = %u/%u, safe_mode = %s%s",
               (unsigned)bn, (unsigned)BOOT_GUARD_THRESHOLD,
               safe ? "YES" : "no", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strncmp((const char*)argv[1], "clear", 5) == 0)
  {
    t_registry_data *reg = registry_acquire();
    if (reg != NULL)
    {
      reg->boot_count = 0U;
      registry_release();
      (void)registry_save();
    }
    CMD_PRINTF(stream, "Bootloop counter cleared (was %u)%s",
               (unsigned)bn, lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  if (strncmp((const char*)argv[1], "test", 4) == 0)
  {
    /* Set counter to threshold MINUS 1 — the next registry_task_init
     * will bump it to threshold and engage safe mode on that boot. */
    t_registry_data *reg = registry_acquire();
    if (reg != NULL)
    {
      reg->boot_count = (uint8_t)(BOOT_GUARD_THRESHOLD - 1U);
      registry_release();
      (void)registry_save();
    }
    CMD_PRINTF(stream,
               "Counter set to %u — next reboot will engage safe mode. "
               "Reboot now (e.g. unplug+replug USB) to verify.%s",
               (unsigned)(BOOT_GUARD_THRESHOLD - 1U), lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }
  CMD_PRINTF(stream, "usage: safeboot [status | clear | test]%s", lwshell_eol());
  return LWSHELL_ERROR_SYNTAX_CMD;
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
  /* Save/restore rather than set/clear: a caller that owns a whole multi-read
   * transaction (header then payload, see `frame upload`) has already claimed
   * the stream, and clearing it here would open a gap between the two reads
   * for a notification to land in. */
  bool was_binary = _shell_binary_rx;
  _shell_binary_rx = true;
  while (got < size)
  {
    int32_t n = stream_read(stream, buf + got, size - got, timeout_ms);
    if (n <= 0)
    {
      _shell_binary_rx = was_binary;
      return (int32_t)got;  /* return what we have on timeout/error */
    }
    got += (size_t)n;
  }
  _shell_binary_rx = was_binary;
  return (int32_t)got;
}

/**
 * Danger zone: disable xSPI memory-mapped mode, erase enough 64KB blocks to
 * cover `size` at `xspi_offset`, write the payload from `src`, then
 * NVIC_SystemReset. Caller must have verified the buffer (magic + CRC).
 *
 * For the model target we erase only the blocks needed to hold `size`
 * (otherwise erasing the full 28 MB SLOT1_WEIGHTS could take ~4 minutes).
 *
 * Note: We do NOT disable interrupts globally — the CDC mutex and watchdog
 * refresh both rely on RTOS. The vendor watchdog_task preempts during HAL
 * polling so the dog stays fed on its own cadence.
 */
static int32_t _fwupd_flash_and_reset(const t_stream *stream,
                                      const uint8_t *src,
                                      uint32_t xspi_offset,
                                      size_t size)
{
  int32_t status;
  /* Number of 64KB blocks needed (round up). */
  uint32_t blocks = (uint32_t)((size + 0xFFFFU) / 0x10000U);

  /* Suspend the NN task FIRST: it's the only consumer that does memory-mapped
   * reads into xSPI NOR (model weights at 0x70600000). If we disable MMP while
   * it's mid-inference, the next weight load faults and we never get to the
   * erase/write. Camera, JPEG, display tasks read from PSRAM (xSPI1), not
   * xSPI2 NOR, so they're not affected. */
  CMD_PRINTF(stream, "Suspending NN task...%s", lwshell_eol());
  (void)nn_task_suspend_thread();
  HAL_Delay(50);

  CMD_PRINTF(stream, "Disabling xSPI memory-mapped mode...%s", lwshell_eol());
  status = BSP_XSPI_NOR_DisableMemoryMappedMode(0);
  if (status != BSP_OK)
  {
    CMD_PRINTF(stream, "ERROR: DisableMMP=%ld%s", (long)status, lwshell_eol());
    goto recover;
  }

  CMD_PRINTF(stream, "Erasing %lux64KB blocks at xSPI offset 0x%08lx...%s",
             (unsigned long)blocks, (unsigned long)xspi_offset, lwshell_eol());
  for (uint32_t b = 0; b < blocks; b++)
  {
    status = BSP_XSPI_NOR_Erase_Block(0, xspi_offset + (b * 0x10000U), BSP_XSPI_NOR_ERASE_64K);
    if (status != BSP_OK)
    {
      CMD_PRINTF(stream, "ERROR: erase block %lu failed (%ld)%s", (unsigned long)b, (long)status, lwshell_eol());
      goto recover;
    }
    tx_thread_sleep(1);   /* yield: feed watchdog_task between erases */
  }

  /* Write page-by-page in 64KB chunks, YIELDING between each. The vendor
   * watchdog_task refreshes the IWDG from its own thread (it sleeps on its
   * window), so a multi-second non-yielding blocking write starves it and
   * the write never completes. tx_thread_sleep(1) lets the watchdog_task +
   * CDC run between chunks. Progress is printed so any failure pinpoints an
   * offset. (Only the NN task reads xSPI2 NOR and it is suspended, so the
   * tasks that run during the yield — camera/jpeg/display/CDC — touch only
   * PSRAM/internal RAM and are safe with NOR MMP disabled.) */
  /* Round the write up to whole 64KB blocks and pad the tail with 0xFF (the
   * erased-flash value). A SHORT final partial page after many full pages
   * fails the MX66 page-program on this kit: BSP_XSPI_NOR_Write returns -3 at
   * the same *payload* offset regardless of the chip address (confirmed
   * data-independent — a random payload of the model's exact size fails
   * identically, while an all-full-64KB-chunk payload flashes completely).
   * Writing only whole 64KB chunks is the pattern that flashes reliably. The
   * erase above already cleared `blocks` whole blocks, and the NN reads only
   * the model's own bytes, so the extra 0xFF tail is harmless. */
  uint32_t write_size = blocks * 0x10000U;        /* == ceil(size / 64KB) * 64KB */
  if (write_size > (uint32_t)size)
  {
    memset((uint8_t*)src + size, 0xFF, write_size - (uint32_t)size);
  }

  CMD_PRINTF(stream, "Writing %lu bytes (padded to %lu)...%s",
             (unsigned long)size, (unsigned long)write_size, lwshell_eol());
  {
    const uint32_t WR_CHUNK = 0x10000U;   /* 64 KB — always a whole chunk now */
    for (uint32_t off = 0U; off < write_size; off += WR_CHUNK)
    {
      status = BSP_XSPI_NOR_Write(0, (uint8_t*)src + off, xspi_offset + off, WR_CHUNK);
      if (status != BSP_OK)
      {
        CMD_PRINTF(stream, "ERROR: write failed (%ld) at +0x%08lx%s",
                   (long)status, (unsigned long)off, lwshell_eol());
        goto recover;
      }
      if ((off % 0x80000U) == 0U)   /* progress every 512KB */
      {
        CMD_PRINTF(stream, "  written %lu/%lu...%s",
                   (unsigned long)off, (unsigned long)write_size, lwshell_eol());
      }
      tx_thread_sleep(1);
    }
  }

  CMD_PRINTF(stream, "Done. Resetting...%s", lwshell_eol());
  HAL_Delay(50);  /* let CDC TX drain */
  NVIC_SystemReset();
  return BSP_OK;  /* unreachable */

recover:
  /* A failed flash previously returned with MMP still DISABLED, leaving the
   * kit wedged (every later CDC update -> DisableMMP=-24, needing SWD or a
   * power-cycle). Re-enable memory-mapped mode and resume the NN task so the
   * shell stays usable and the update can simply be retried over CDC. */
  (void)BSP_XSPI_NOR_EnableMemoryMappedMode(0);
  (void)nn_task_resume_thread();
  return status;
}

static int32_t _update_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  uint8_t  hdr[FWUPD_HDR_SIZE];
  uint32_t size;
  uint32_t expect_crc;
  uint32_t got_crc;
  int32_t  n;

  /* Target selection: 'update' (legacy) and 'update app' = SLOT1_APP;
   * 'update model' = SLOT1_WEIGHTS. */
  const char *tgt = (argc >= 2U) ? (const char*)argv[1] : "app";
  bool     is_model = (strcmp(tgt, "model") == 0);
  bool     is_app   = (strcmp(tgt, "app")   == 0) || (argc < 2U);
  if (!is_model && !is_app)
  {
    return LWSHELL_ERROR_SYNTAX_CMD;
  }

  uint32_t xspi_offset = is_model ? FWUPD_MODEL_XSPI_OFFSET : FWUPD_APP_XSPI_OFFSET;
  uint32_t max_size    = is_model ? FWUPD_MODEL_MAX_SIZE    : FWUPD_APP_MAX_SIZE;
  uint8_t *buf         = is_model ? _fwupd_model_buf        : _fwupd_app_buf;
  const char *target_str = is_model ? "model" : "app";

  CMD_PRINTF(stream,
    "Ready (target=%s). Send: 'UPDT' + size_le(4) + crc32_le(4) + payload (max %lu B)%s",
    target_str, (unsigned long)max_size, lwshell_eol());

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

  if ((size == 0U) || (size > max_size))
  {
    CMD_PRINTF(stream, "ERROR: bad size %lu (max %lu for %s)%s",
               (unsigned long)size, (unsigned long)max_size,
               target_str, lwshell_eol());
    return LWSHELL_OK;
  }
  CMD_PRINTF(stream, "Receiving %lu bytes (expected CRC32 0x%08lx)...%s",
             (unsigned long)size, (unsigned long)expect_crc, lwshell_eol());

  /* Read payload into the target's PSRAM buffer */
  n = _stream_read_exact(stream, buf, (size_t)size, FWUPD_PAYLOAD_TIMEOUT_MS);
  if (n != (int32_t)size)
  {
    CMD_PRINTF(stream, "ERROR: payload short (got %ld/%lu bytes)%s",
               (long)n, (unsigned long)size, lwshell_eol());
    return LWSHELL_OK;
  }

  /* Verify CRC32 */
  got_crc = _crc32(buf, (size_t)size);
  if (got_crc != expect_crc)
  {
    CMD_PRINTF(stream, "ERROR: CRC32 mismatch (got 0x%08lx, expected 0x%08lx)%s",
               (unsigned long)got_crc, (unsigned long)expect_crc, lwshell_eol());
    return LWSHELL_OK;
  }
  CMD_PRINTF(stream, "CRC32 OK. Flashing %s...%s", target_str, lwshell_eol());

  /* Point of no return: erase + write + reset */
  _fwupd_flash_and_reset(stream, buf, xspi_offset, (size_t)size);
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
        /* Naming the real range matters: the profile table comes from the
         * ISP tuning file for the fitted sensor, so it is not the fixed
         * 0..5 the help text used to promise. On the IMX335 tuning there
         * are three, and "Invalid profile ID (3)!" on its own reads like a
         * fault rather than the end of the list. (ScopusQA #7.) */
        CMD_PRINTF(stream, "Invalid profile ID (%u)! Valid range is 0..%u "
                           "on %s; 'camera awb' with no value lists them.%s",
                   idx, count, camera_get_name(), lwshell_eol());
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
      _camera_brightness_hint(stream, status);
      return LWSHELL_OK;
    }
  }
  status = _camera_print_status(stream, camera_get_brightness(&value), false);
  if (status == 0)
  {
    CMD_PRINTF(stream, "Brightness: %u%s", value, lwshell_eol());
  }
  else
  {
    _camera_brightness_hint(stream, status);
  }
  return LWSHELL_OK;
}

/**
 * @brief Explain a brightness rejection instead of leaving it bare.
 *
 * Brightness is a sensor-driver control, and the IMX335 driver does not
 * implement one — `camera.sensor.ctrl.set_brightness` is NULL, which surfaces
 * as -3 "Not supported". That is the correct answer for this sensor, but on
 * its own it tells a tester nothing about what to do instead, so it was
 * reported as a fault (ScopusQA #6). On an ISP part the exposure/gain/AEC
 * controls are the ones that change image brightness, and they do work.
 */
static void _camera_brightness_hint(const t_stream *stream, int32_t status)
{
  if (status == -3)
  {
    CMD_PRINTF(stream, "  %s has no sensor brightness control. Use "
                       "'camera aec <-2.0..2.0>', 'camera exposure <usec>' "
                       "or 'camera gain <mdB>' to change image brightness.%s",
               camera_get_name(), lwshell_eol());
  }
}

/**
 * @brief Camera status command: print every camera setting in one place
 *
 * Each sub-command already answers with the current value when given no
 * argument, but a tester checking "what is set now?" before and after a
 * change had to run six commands and diff them by eye (ScopusQA #3). This is
 * the same information in one reply, in the same order as the sub-commands,
 * and it names the sensor so a "not supported" line can be read against it.
 *
 * Controls the fitted sensor does not implement print their reason rather
 * than being hidden: "not supported on this sensor" is an answer, and a
 * silently missing line is not.
 *
 * @param stream  Output stream
 * @param argv    Arguments (tokens)
 * @param argc    Number of arguments
 * @return Error code
 */
static int32_t _camera_cmd_status(const t_stream *stream, uint8_t **argv, size_t argc)
{
  ISP_AECAlgoTypeDef        aec;
  ISP_AWBAlgoTypeDef        awb;
  ISP_SensorGainTypeDef     gain;
  ISP_SensorExposureTypeDef exp;
  uint8_t                   flip;
  uint8_t                   count = 0U;
  uint8_t                   current = 0U;
  uint16_t                  bright;

  (void)argv;
  (void)argc;

  CMD_PRINTF(stream, "Sensor    : %s%s", camera_get_name(), lwshell_eol());
  CMD_PRINTF(stream, "ISP       : %s%s",
             camera_use_isp() ? "yes" : "no", lwshell_eol());

  if (camera_get_flip(&flip) == 0)
  {
    if (flip == CAMERA_FLIP_NONE)
    {
      CMD_PRINTF(stream, "Flip      : %s%s", OPT_OFF, lwshell_eol());
    }
    else
    {
      CMD_PRINTF(stream, "Flip      : %s%s%s",
                 (flip & CAMERA_FLIP_H) ? "H" : "",
                 (flip & CAMERA_FLIP_V) ? "V" : "", lwshell_eol());
    }
  }
  else
  {
    CMD_PRINTF(stream, "Flip      : not supported on this sensor%s", lwshell_eol());
  }

  if (camera_get_aec(&aec) == 0)
  {
    CMD_PRINTF(stream, "AEC       : %s, compensation %.1f EV%s",
               aec.enable ? STATUS_ACTIVE : STATUS_INACTIVE,
               (float)aec.exposureCompensation / 2.0f, lwshell_eol());
  }
  else
  {
    CMD_PRINTF(stream, "AEC       : not supported on this sensor%s", lwshell_eol());
  }

  if (camera_get_awb(&awb, &count, &current) == 0)
  {
    /* The valid profile range is whatever the tuning file carries, which is
     * why it is reported rather than assumed — see ScopusQA #7. */
    if (awb.enable)
    {
      CMD_PRINTF(stream, "AWB       : auto (profiles 0..%u available)%s",
                 count, lwshell_eol());
    }
    else
    {
      CMD_PRINTF(stream, "AWB       : profile %u of 0..%u - %6luK %s%s",
                 current, count, awb.referenceColorTemp[current],
                 awb.label[current], lwshell_eol());
    }
  }
  else
  {
    CMD_PRINTF(stream, "AWB       : not supported on this sensor%s", lwshell_eol());
  }

  if (camera_get_gain(&gain) == 0)
  {
    CMD_PRINTF(stream, "Gain      : %lu mdB (0..%lu)%s",
               (unsigned long)gain.gain, (unsigned long)CAMERA_GAIN_MAX,
               lwshell_eol());
  }
  else
  {
    CMD_PRINTF(stream, "Gain      : not supported on this sensor%s", lwshell_eol());
  }

  if (camera_get_exposure(&exp) == 0)
  {
    CMD_PRINTF(stream, "Exposure  : %lu usec (0..%lu)%s",
               (unsigned long)exp.exposure, (unsigned long)CAMERA_EXPOSURE_MAX,
               lwshell_eol());
  }
  else
  {
    CMD_PRINTF(stream, "Exposure  : not supported on this sensor%s", lwshell_eol());
  }

  if (camera_get_brightness(&bright) == 0)
  {
    CMD_PRINTF(stream, "Brightness: %u (0..%u)%s", bright,
               (unsigned)CAMERA_BRIGHTNESS_MAX, lwshell_eol());
  }
  else
  {
    CMD_PRINTF(stream, "Brightness: not supported on %s - use aec/exposure/gain%s",
               camera_get_name(), lwshell_eol());
  }

  _cmd_ack(stream, argv, argc);
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
*           MangOH modem pass-through (SoW §4.6)
*//*--------------------------------------------------------------------------*/

/* Stream pointer for the URC callback to write back to. Set every time the
 * shell task wakes — the URC arrives in modem_task context, we forward it
 * to the shell's stream so the operator sees +SDVR* lines as they come. */
static const t_stream *volatile _mdm_urc_stream = NULL;

static void _mdm_urc_forward(const char *line, size_t len, void *ctx)
{
  (void)len; (void)ctx;

  /* +SDVRCMD: "<text>" is a remote command, not a message to read. Park it
   * for the shell task and fall through, so the console still shows what
   * arrived — an operator watching the port needs to see that the unit is
   * being driven from elsewhere. */
  {
    static const char PFX[] = "+SDVRCMD: \"";
    if (strncmp(line, PFX, sizeof(PFX) - 1U) == 0)
    {
      char text[REMOTE_CMD_MAX];
      const char *body = line + (sizeof(PFX) - 1U);
      size_t i = 0U;
      while ((body[i] != '\0') && (body[i] != '"') &&
             (i + 1U < sizeof(text)))
      {
        text[i] = body[i];
        i++;
      }
      text[i] = '\0';
      shell_remote_cmd_post(text);
    }
  }

  /* §4.2 bit0 — "sent each time modem is registered to the network, on power
   * up, reset or after network loss".
   *
   * Two URCs satisfy that sentence and both are needed. `+SDVRNET: UP` is the
   * registration itself, but it only fires on a transition — restart the
   * modem app while the data session is already up and it never comes, which
   * would leave the "on power up" half of the requirement unreported and the
   * event untestable on a bench that is always in coverage. `+SDVRRDY: <ver>`
   * is the modem announcing it has just started, i.e. the power-up/reset case.
   * `+SDVRNET: ERROR <n>` is a loss, not a registration, and must not latch. */
  /* Catch the clock reply on its way to the console — see _cclk_line. */
  if (strncmp(line, "+CCLK:", 6) == 0)
  {
    size_t i = 0U;
    while ((line[i] != '\0') && (i + 1U < sizeof(_cclk_line)))
    {
      _cclk_line[i] = line[i];
      i++;
    }
    _cclk_line[i] = '\0';
    _cclk_seen = true;
  }

  if ((strncmp(line, "+SDVRNET: UP", 12) == 0) ||
      (strncmp(line, "+SDVRRDY:", 9) == 0))
  {
    shell_notify_netreg();
    /* The modem is up, so it has a clock worth copying. The camera's RTC
     * starts at 2000-01-01 on every power cycle and stamps every photo name
     * and event body, so this is the moment to fix it. */
    _rtc_sync_tries   = 0U;
    _rtc_sync_pending = true;
  }

  /* Fall back to the shell's own stream: the forwarder is now registered at
   * shell init, before any `mdm` command has had a chance to set this. */
  const t_stream *s = _mdm_urc_stream;
  if (s == NULL) s = _shell.stream;
  if (s == NULL) return;
  /* The shell task's CMD_PRINTF macro needs the static `_shell.out`
   * scratch, but the URC fires in modem_task context. Use stream_printf
   * directly with a tiny stack buffer — keeps the URC path independent
   * of the command-printer plumbing. */
  static uint8_t scratch[MODEM_URC_MAX + 8U];
  stream_printf(s, scratch, sizeof(scratch),
                "%s%s", line, lwshell_eol());
}

static int32_t _mdm_cmd(const t_stream *stream, uint8_t **argv, size_t argc)
{
  if (argc < 2U)
  {
    CMD_PRINTF(stream, "Usage: mdm <at-command>%s"
                       "       mdm test echo            - HDLC loopback%s"
                       "       mdm test urc <line>      - synthesise a URC%s"
                       "       mdm stats [reset]        - link RX/TX counters%s"
                       "       mdm raw on|off           - hex-dump raw RX%s",
               lwshell_eol(), lwshell_eol(), lwshell_eol(),
               lwshell_eol(), lwshell_eol());
    return LWSHELL_OK;
  }

  /* `mdm stats` / `mdm raw` — link diagnostics. These exist because every
   * layer below discards malformed input silently, so a link that is
   * receiving corrupted bytes is indistinguishable from one receiving
   * nothing. Handled before the URC registration so they stay side-effect
   * free. */
  if (strcmp((char*)argv[1], "stats") == 0)
  {
    if (argc >= 3U && strcmp((char*)argv[2], "reset") == 0)
    {
      modem_reset_stats();
      CMD_PRINTF(stream, "mdm stats: cleared%s", lwshell_eol());
      return LWSHELL_OK;
    }
    t_modem_stats st;
    modem_get_stats(&st);
    CMD_PRINTF(stream,
      "rx: bytes=%lu frames=%lu badcrc=%lu overflow=%lu trunc=%lu "
      "stray=%lu err=%lu timeouts=%lu%s"
      "tx: frames=%lu err=%lu retries=%lu   usart2 err(ORE/FE/NE)=%lu%s",
      (unsigned long)st.rx_bytes,     (unsigned long)st.rx_frames,
      (unsigned long)st.rx_bad,       (unsigned long)st.rx_overflow,
      (unsigned long)st.rx_truncated, (unsigned long)st.rx_stray,
      (unsigned long)st.rx_errors,    (unsigned long)st.rx_timeouts, lwshell_eol(),
      (unsigned long)st.tx_frames,(unsigned long)st.tx_errors,
      (unsigned long)st.tx_retries,
      (unsigned long)st.uart_errors, lwshell_eol());
    /* The notification queue is the camera→server path, and it fails
     * differently from the link itself: a healthy tx line with dropped or
     * failed notifications means the events are being lost above the UART,
     * which no rx/tx counter would show. */
    CMD_PRINTF(stream,
      "ntf: queued=%lu sent=%lu unconfirmed=%lu dropped=%lu%s",
      (unsigned long)st.ntf_queued, (unsigned long)st.ntf_sent,
      (unsigned long)st.ntf_unconfirmed, (unsigned long)st.ntf_dropped,
      lwshell_eol());
    /* Link health. A rising relink count on a bench that is otherwise quiet
     * is the CN805 translator latching; consec>0 means it is happening now. */
    CMD_PRINTF(stream, "link: relinks=%lu consec_timeouts=%lu%s",
      (unsigned long)st.relinks, (unsigned long)st.consec_timeouts,
      lwshell_eol());
    /* Report the line rate the peripheral is REALLY running at. If this is
     * not 115200 the link cannot work, however good the wiring is -- and a
     * loopback test would still pass, because both directions would share the
     * same wrong divisor. */
    uint32_t fck    = bsp_uart_get_kernel_clock(UART_2);
    uint32_t actual = bsp_uart_get_actual_baud(UART_2);
    CMD_PRINTF(stream, "usart2: fck=%lu Hz brr=0x%04lX actual=%lu baud (want 115200)%s",
               (unsigned long)fck,
               (unsigned long)uart[UART_2].bsp.huart.Instance->BRR,
               (unsigned long)actual, lwshell_eol());
    if ((actual != 0U) &&
        ((actual > 118000U) || (actual < 112000U)))
    {
      CMD_PRINTF(stream, "  -> BAUD IS WRONG (%lu vs 115200): BRR was computed "
                         "against a different kernel clock%s",
                 (unsigned long)actual, lwshell_eol());
    }

    if (st.rx_bytes == 0U)
    {
      CMD_PRINTF(stream, "  -> nothing reaching PF6: check wiring/GND/TX-RX cross%s",
                 lwshell_eol());
    }
    else if (st.rx_frames == 0U)
    {
      CMD_PRINTF(stream, "  -> bytes arrive but never frame: far end not HDLC, "
                         "or baud/level mismatch%s", lwshell_eol());
    }
    return LWSHELL_OK;
  }
  if (strcmp((char*)argv[1], "raw") == 0 && argc >= 3U)
  {
    bool on = (strcmp((char*)argv[2], "on") == 0);
    modem_set_raw_dump(on);
    CMD_PRINTF(stream, "mdm raw: %s%s", on ? "on" : "off", lwshell_eol());
    return LWSHELL_OK;
  }

  /* Register the URC forwarder once (per shell, per stream). The callback
   * pointer is global, but the stream changes when shell is re-entered. */
  _mdm_urc_stream = stream;
  modem_set_urc_callback(_mdm_urc_forward, NULL);

  /* Big scratch buffers — keep them out of the shell task's 2 KB stack
   * (which already carries the lwshell tokeniser + per-command locals).
   * Single shell consumer is serial, so a static is fine. */
  static char cmd[MODEM_FRAME_MAX];
  static char reply[MODEM_FRAME_MAX];
  static uint8_t test_wire[64];
  static uint8_t test_out[64];

  /* `mdm test ...` exercises the framing locally so the path can be
   * validated without a MangOH on the bench. */
  if (strcmp((char*)argv[1], "test") == 0 && argc >= 3U)
  {
    /* Fault injection: wedge the link on purpose so the recovery path can be
     * demonstrated. The real CN805 latch has never been reproducible on
     * demand, which is exactly why the recovery needed a way to be tested at
     * all. Mis-configuring the line rate is a faithful model: frames really
     * do stop crossing the wire, and the thing that fixes it — a UART
     * re-init — is exactly what fixes the real one. */
    if (strcmp((char*)argv[2], "wedge") == 0)
    {
      uint32_t baud = 9600U;
      if (argc >= 4U)
      {
        long b = atol((char*)argv[3]);
        if (b > 0) { baud = (uint32_t)b; }
      }
      (void)modem_test_wedge(baud);
      CMD_PRINTF(stream, "mdm test wedge: USART2 forced to %lu baud; the next "
                 "%u commands should fail, then the link auto-relinks%s",
                 (unsigned long)baud, (unsigned)MODEM_RELINK_AFTER,
                 lwshell_eol());
      _cmd_ack(stream, argv, argc);
      return LWSHELL_OK;
    }
    if (strcmp((char*)argv[2], "echo") == 0)
    {
      /* Round-trip "AT\r\n" through the HDLC encoder/decoder and report. */
      const uint8_t  payload[] = { 'A', 'T', '\r', '\n' };
      size_t         wire_len = 0U;
      hdlc_encode(payload, sizeof(payload), test_wire, sizeof(test_wire), &wire_len);
      t_hdlc_decoder d;
      hdlc_decoder_init(&d, test_out, sizeof(test_out));
      size_t got = 0U;
      for (size_t i = 0U; i < wire_len; i++)
      {
        size_t finished = 0U;
        if (hdlc_decoder_feed(&d, test_wire[i], &finished) == HDLC_FEED_FRAME)
        {
          got = finished;
        }
      }
      CMD_PRINTF(stream,
        "mdm test echo: wire=%u bytes, decoded=%u bytes, match=%s%s",
        (unsigned)wire_len, (unsigned)got,
        (got == sizeof(payload) && memcmp(test_out, payload, got) == 0) ? "yes" : "no",
        lwshell_eol());
      _cmd_ack(stream, argv, argc);
      return LWSHELL_OK;
    }
    if (strcmp((char*)argv[2], "urc") == 0 && argc >= 4U)
    {
      /* Synthesise an asynchronous URC and run it through the dispatcher
       * exactly as if it arrived over USART2. lwshell already tokenised
       * on whitespace, so a URC like '+SDVRRDY: 1.0.5' arrives across
       * argv[3..argc-1]; rejoin with single spaces so we forward the
       * whole line. The forwarder above will write it back to this
       * shell's stream. */
      size_t up = 0U;
      for (size_t i = 3U; i < argc; i++)
      {
        if (i > 3U && up < sizeof(cmd) - 1U) cmd[up++] = ' ';
        size_t a = strlen((char*)argv[i]);
        if (up + a >= sizeof(cmd) - 1U) a = sizeof(cmd) - 1U - up;
        memcpy(&cmd[up], argv[i], a);
        up += a;
      }
      if (up + 2U < sizeof(cmd)) { cmd[up++] = '\r'; cmd[up++] = '\n'; }
      modem_inject_rx((const uint8_t*)cmd, up);
      _cmd_ack(stream, argv, argc);
      return LWSHELL_OK;
    }
    CMD_PRINTF(stream, "Bad syntax — use 'mdm test echo', 'mdm test urc <line>' "
               "or 'mdm test wedge [baud]'%s",
               lwshell_eol());
    return LWSHELL_OK;
  }

  /* Force the link-recovery path by hand. Useful when the link is wedged and
   * you would rather not wait for three commands to time out, and as the
   * manual half of the watchdog. */
  if (strcmp((char*)argv[1], "relink") == 0)
  {
    int32_t rc = modem_relink();
    CMD_PRINTF(stream, "mdm relink: %s%s",
               (rc == 0) ? "USART2 re-initialised" : "FAILED", lwshell_eol());
    _cmd_ack(stream, argv, argc);
    return LWSHELL_OK;
  }

  /* Real pass-through: forward exactly what the user typed.
   *
   * Rejoining argv with spaces cannot work here. lwshell's tokeniser rewrites
   * the line in place and replaces a quote found mid-token with a NUL, so
   *   mdm AT+SDVRNTFHOST="1.2.3.4"
   * arrived as argv[1] == "AT+SDVRNTFHOST=" — the address silently gone, and
   * the modem answering ERROR to a command it was right to reject. Quoted
   * parameters are not optional on the modem side either: its own parser
   * rejects a bare dotted IP, so this affected every AT+SDVR* setter that
   * takes a string.
   *
   * lwshell_raw_line() returns the untokenised copy; skip our own command
   * word and any spaces after it, and pass the remainder through untouched. */
  const char *raw = lwshell_raw_line();
  size_t pos = 0U;
  if (raw != NULL)
  {
    /* Step over leading spaces, then the command word ("mdm"), then the
     * spaces before its first argument. */
    while (*raw == ' ') { raw++; }
    while ((*raw != '\0') && (*raw != ' ')) { raw++; }
    while (*raw == ' ') { raw++; }

    size_t a = strlen(raw);
    if (a >= sizeof(cmd)) { a = sizeof(cmd) - 1U; }
    memcpy(cmd, raw, a);
    pos = a;
  }
  /* Trailing whitespace would become part of the AT command. */
  while ((pos > 0U) && ((cmd[pos - 1U] == ' ') || (cmd[pos - 1U] == '\t')))
  {
    pos--;
  }
  cmd[pos] = '\0';

  if (pos == 0U)
  {
    CMD_PRINTF(stream, "mdm: nothing to send%s", lwshell_eol());
    return LWSHELL_OK;
  }

  int32_t rc = modem_send_at(cmd, reply, sizeof(reply), MODEM_AT_TIMEOUT_MS);
  if (rc < 0)
  {
    CMD_PRINTF(stream, "mdm: error %ld (no modem? timeout?)%s",
               (long)rc, lwshell_eol());
    return LWSHELL_OK;
  }
  /* Print the reply. Strip the trailing newline if any — CMD_PRINTF appends
   * lwshell_eol() so we keep the line terminator consistent. */
  size_t rlen = (size_t)rc;
  while (rlen > 0U && (reply[rlen - 1U] == '\n' || reply[rlen - 1U] == '\r'))
    reply[--rlen] = '\0';
  CMD_PRINTF(stream, "%s%s", reply, lwshell_eol());
  _cmd_ack(stream, argv, argc);
  return LWSHELL_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: Shell -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_SHELL */

