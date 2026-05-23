/**
 ******************************************************************************
 * @file    modem_task.c
 * @brief   USART2 ↔ MangOH WP76 modem link with HDLC framing and request/
 *          response + URC dispatcher.
 *
 *          See modem_task.h for the public contract. Internals:
 *
 *          - RX path: bsp_uart_read in receive-to-idle mode chops the wire
 *            into bursts. Each burst is fed byte-by-byte through
 *            hdlc_decoder_feed; each completed frame goes to
 *            _dispatch_frame, which decides "URC vs response" and either
 *            posts to a pending command waiter or fires the URC callback.
 *
 *          - TX path: caller holds _tx_mtx for the duration of one logical
 *            transmission (prefix-line + optional binary tail), so concurrent
 *            producers can't tear each other's frames apart.
 *
 *          - Synchronisation: each modem_send_at call writes the command,
 *            then waits on a TX event flag for a "response complete" bit
 *            that _dispatch_frame raises when it has accumulated a
 *            terminator (OK / ERROR / +CME ERROR / +CMS ERROR).
 ******************************************************************************
 */
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "tx_app.h"
#include "n6cam_uart.h"
#include "n6cam_rtos.h"

#include "hdlc.h"
#include "modem_task.h"

/*----------------------------------------------------------------------------*/
/* Tunables                                                                   */
/*----------------------------------------------------------------------------*/
#define MODEM_TASK_STACK_SIZE   (4U * 1024U)
#define MODEM_TASK_PRIO         APP_PRIO_USER_INTERFACE
#define MODEM_TASK_TIME_SLICE   TX_NO_TIME_SLICE

#define MODEM_UART              UART_2          /* USART2 → J503 modem link */
#define MODEM_UART_BAUD         115200U
#define MODEM_RX_CHUNK          256U

/* Bits in the modem task's event-flags group. */
#define MODEM_EVT_RESP_DONE     BIT(0U)         /* set when an OK/ERROR is seen */
#define MODEM_EVT_RESP_OVERFLOW BIT(1U)         /* set when reply buffer fills */

/*----------------------------------------------------------------------------*/
/* State                                                                      */
/*----------------------------------------------------------------------------*/
typedef struct
{
  TX_THREAD             thread;
  TX_EVENT_FLAGS_GROUP  evt;
  TX_MUTEX              tx_mtx;
  TX_MUTEX              resp_mtx;     /* protects _resp_buf / _resp_len */

  uint8_t              *stack_ptr;

  /* HDLC decoder state and its output buffer */
  t_hdlc_decoder        dec;
  uint8_t               dec_out[MODEM_FRAME_MAX];

  /* Per-command response collector. _resp_active means a caller is waiting
   * inside modem_send_at; lines that arrive go into _resp_buf until a
   * terminator is observed. */
  char                  resp_buf[MODEM_FRAME_MAX];
  size_t                resp_cap;     /* caller's cap (≤ MODEM_FRAME_MAX) */
  size_t                resp_len;
  bool                  resp_active;

  /* URC callback */
  t_modem_urc_cb        urc_cb;
  void                 *urc_ctx;
} t_modem_task;

static uint8_t          _modem_stack[MODEM_TASK_STACK_SIZE];
static t_modem_task     _m;
static const char       _crlf[] = "\r\n";

/*----------------------------------------------------------------------------*/
/* Helpers                                                                    */
/*----------------------------------------------------------------------------*/

/* Is this line a terminator that ends a command's response? */
static bool _is_terminator(const char *line, size_t len)
{
  return (len == 2U && line[0] == 'O' && line[1] == 'K') ||
         (len == 5U && memcmp(line, "ERROR", 5U) == 0) ||
         (len >= 11U && memcmp(line, "+CME ERROR:", 11U) == 0) ||
         (len >= 11U && memcmp(line, "+CMS ERROR:", 11U) == 0);
}

/* Is this line a URC the shell should forward? Heuristic: starts with '+'
 * and isn't a +CME / +CMS ERROR (handled by terminator path). */
static bool _looks_like_urc(const char *line, size_t len)
{
  if (len < 2U || line[0] != '+') return false;
  if (_is_terminator(line, len))  return false;
  return true;
}

/* One decoded HDLC frame just landed in _m.dec_out. Decide where to send it. */
static void _dispatch_frame(const uint8_t *frame, size_t len)
{
  if (len == 0U) return;

  /* The modem emits lines ending with \r\n. We may receive one line per
   * frame or many concatenated. Walk line-by-line. */
  size_t i = 0U;
  while (i < len)
  {
    /* Find next newline */
    size_t end = i;
    while (end < len && frame[end] != '\n') end++;

    /* Slice [i, end) is one line. Strip a trailing \r. */
    size_t line_end = end;
    if (line_end > i && frame[line_end - 1U] == '\r') line_end--;
    size_t line_len = line_end - i;

    /* Stash in a small NUL-terminated scratch */
    char scratch[MODEM_URC_MAX];
    size_t copy = (line_len < sizeof(scratch) - 1U) ? line_len : (sizeof(scratch) - 1U);
    memcpy(scratch, &frame[i], copy);
    scratch[copy] = '\0';

    if (_m.resp_active)
    {
      rtos_mutex_acquire(&_m.resp_mtx, true);
      bool is_term = _is_terminator(scratch, copy);
      bool is_urc  = _looks_like_urc(scratch, copy);
      if (is_urc && _m.urc_cb)
      {
        /* URCs interleave with commands; forward and don't add to response. */
        _m.urc_cb(scratch, copy, _m.urc_ctx);
      }
      else if (copy > 0U)
      {
        /* Append line to response buffer with a separating \n. */
        size_t need = copy + 1U;   /* + '\n' */
        if (_m.resp_len + need + 1U <= _m.resp_cap)
        {
          memcpy(&_m.resp_buf[_m.resp_len], scratch, copy);
          _m.resp_len += copy;
          _m.resp_buf[_m.resp_len++] = '\n';
          _m.resp_buf[_m.resp_len]   = '\0';
        }
        else
        {
          rtos_raise_event(&_m.evt, MODEM_EVT_RESP_OVERFLOW);
        }
      }
      rtos_mutex_acquire(&_m.resp_mtx, false);
      if (is_term)
      {
        rtos_raise_event(&_m.evt, MODEM_EVT_RESP_DONE);
      }
    }
    else if (_m.urc_cb && copy > 0U)
    {
      /* No command in flight — every line is effectively a URC. */
      _m.urc_cb(scratch, copy, _m.urc_ctx);
    }

    if (end >= len) break;
    i = end + 1U;
  }
}

/* Drive the HDLC decoder with one received chunk. */
static void _feed_chunk(const uint8_t *buf, size_t len)
{
  for (size_t i = 0U; i < len; i++)
  {
    size_t finished = 0U;
    if (!hdlc_decoder_feed(&_m.dec, buf[i], &finished))
    {
      /* Bad CRC / overflow — decoder is already reset; just keep going. */
      continue;
    }
    if (finished > 0U)
    {
      _dispatch_frame(_m.dec_out, finished);
      /* Re-arm decoder by re-init'ing — same buffer. */
      hdlc_decoder_init(&_m.dec, _m.dec_out, sizeof(_m.dec_out));
    }
  }
}

/* Transmit an HDLC-framed payload over USART2 (caller already holds tx_mtx). */
static int32_t _tx_framed(const uint8_t *payload, size_t payload_len,
                          uint32_t timeout_ms)
{
  /* Worst case: every byte gets stuffed → 2*(payload+2)+2. The
   * MODEM_FRAME_MAX cap on payload + CRC keeps us under a fixed bound. */
  static uint8_t wire[2U * (MODEM_FRAME_MAX + 2U) + 2U];
  size_t wire_len = 0U;
  if (!hdlc_encode(payload, payload_len, wire, sizeof(wire), &wire_len))
  {
    return -1;
  }
  int32_t rc = bsp_uart_write(MODEM_UART, wire, wire_len, timeout_ms);
  return (rc >= 0) ? 0 : -1;
}

/*----------------------------------------------------------------------------*/
/* Public API                                                                 */
/*----------------------------------------------------------------------------*/

int32_t modem_send_at(const char *cmd, char *reply, size_t reply_cap,
                      uint32_t timeout_ms)
{
  if (cmd == NULL || reply == NULL || reply_cap < 2U) return -3;
  if (timeout_ms == 0U) timeout_ms = MODEM_AT_TIMEOUT_MS;

  size_t cmd_len = strlen(cmd);
  if (cmd_len + 2U > MODEM_FRAME_MAX) return -3;

  /* Build payload "<cmd>\r\n" */
  uint8_t payload[MODEM_FRAME_MAX];
  memcpy(payload, cmd, cmd_len);
  payload[cmd_len]      = _crlf[0];
  payload[cmd_len + 1U] = _crlf[1];

  rtos_mutex_acquire(&_m.tx_mtx, true);

  /* Arm the response collector. */
  rtos_mutex_acquire(&_m.resp_mtx, true);
  _m.resp_cap    = (reply_cap < sizeof(_m.resp_buf)) ? reply_cap : sizeof(_m.resp_buf);
  _m.resp_len    = 0U;
  _m.resp_buf[0] = '\0';
  _m.resp_active = true;
  rtos_clear_event(&_m.evt, MODEM_EVT_RESP_DONE | MODEM_EVT_RESP_OVERFLOW);
  rtos_mutex_acquire(&_m.resp_mtx, false);

  int32_t rc = _tx_framed(payload, cmd_len + 2U, timeout_ms);
  if (rc != 0)
  {
    rtos_mutex_acquire(&_m.resp_mtx, true);
    _m.resp_active = false;
    rtos_mutex_acquire(&_m.resp_mtx, false);
    rtos_mutex_acquire(&_m.tx_mtx, false);
    return -1;
  }

  /* Wait for terminator or timeout. ThreadX ticks at 100 Hz here. */
  ULONG  got = 0U;
  UINT   tx_status = tx_event_flags_get(
      &_m.evt, MODEM_EVT_RESP_DONE | MODEM_EVT_RESP_OVERFLOW,
      TX_OR_CLEAR, &got, (ULONG)(timeout_ms / 10U));

  int32_t out = 0;
  rtos_mutex_acquire(&_m.resp_mtx, true);
  if (tx_status == TX_SUCCESS && _m.resp_len > 0U)
  {
    size_t copy = (_m.resp_len < reply_cap - 1U) ? _m.resp_len : (reply_cap - 1U);
    memcpy(reply, _m.resp_buf, copy);
    reply[copy] = '\0';
    out = (int32_t)copy;
  }
  else if (tx_status != TX_SUCCESS)
  {
    reply[0] = '\0';
    out = -2;
  }
  _m.resp_active = false;
  rtos_mutex_acquire(&_m.resp_mtx, false);

  rtos_mutex_acquire(&_m.tx_mtx, false);
  return out;
}

int32_t modem_send_binary(const char *prefix_line,
                          const uint8_t *payload, size_t payload_len,
                          uint32_t timeout_ms)
{
  if (prefix_line == NULL || payload == NULL) return -1;
  if (timeout_ms == 0U) timeout_ms = MODEM_AT_TIMEOUT_MS;
  size_t pre_len = strlen(prefix_line);
  if (pre_len + 2U > MODEM_FRAME_MAX) return -1;

  rtos_mutex_acquire(&_m.tx_mtx, true);

  /* First frame: prefix + CRLF (mirrors the AT-line shape so the modem's
   * SDVR+SENDBIN handler can parse parameters before binary starts). */
  uint8_t pre[MODEM_FRAME_MAX];
  memcpy(pre, prefix_line, pre_len);
  pre[pre_len]      = _crlf[0];
  pre[pre_len + 1U] = _crlf[1];
  int32_t rc = _tx_framed(pre, pre_len + 2U, timeout_ms);
  if (rc != 0)
  {
    rtos_mutex_acquire(&_m.tx_mtx, false);
    return -1;
  }

  /* Binary payload may exceed MODEM_FRAME_MAX → fragment into frames. */
  size_t off = 0U;
  while (off < payload_len)
  {
    size_t chunk = payload_len - off;
    if (chunk > MODEM_FRAME_MAX) chunk = MODEM_FRAME_MAX;
    rc = _tx_framed(&payload[off], chunk, timeout_ms);
    if (rc != 0)
    {
      rtos_mutex_acquire(&_m.tx_mtx, false);
      return -1;
    }
    off += chunk;
  }

  rtos_mutex_acquire(&_m.tx_mtx, false);
  return 0;
}

void modem_set_urc_callback(t_modem_urc_cb cb, void *user_ctx)
{
  _m.urc_cb  = cb;
  _m.urc_ctx = user_ctx;
}

void modem_inject_rx(const uint8_t *line, size_t len)
{
  /* Format the injected bytes as a synthetic decoded frame and dispatch
   * directly. Caller's responsibility to include trailing \r\n inside
   * `line` if they want the dispatcher to terminate a single logical line. */
  _dispatch_frame(line, len);
}

/*----------------------------------------------------------------------------*/
/* Task                                                                       */
/*----------------------------------------------------------------------------*/
static void _modem_task_run(uint32_t args)
{
  UNUSED(args);

  LINFO(TRACE_MODEM, "Task started");

  /* Bring up USART2. */
  int32_t status = bsp_uart_init(MODEM_UART, MODEM_UART_BAUD, false);
  if (status != 0)
  {
    LERROR(TRACE_MODEM, "USART2 init failed: %ld", (long)status);
  }
  else
  {
    bsp_uart_set_mode(MODEM_UART, UART_MODE_IT, UART_MODE_IT);
    LINFO(TRACE_MODEM, "USART2 up @ %u baud, awaiting MangOH", (unsigned)MODEM_UART_BAUD);
  }

  hdlc_decoder_init(&_m.dec, _m.dec_out, sizeof(_m.dec_out));

  uint8_t rx[MODEM_RX_CHUNK];
  while (1)
  {
    int32_t n = bsp_uart_read(MODEM_UART, rx, sizeof(rx), 1000U /* ms */);
    if (n > 0)
    {
      _feed_chunk(rx, (size_t)n);
    }
    /* On timeout / error / 0-byte, just retry. The shell can still inject
     * test traffic via modem_inject_rx in the meantime. */
  }
}

int32_t modem_task_start(void)
{
  LINFO(TRACE_MODEM, "modem_task_start: entering");
  /* Event flags */
  if (tx_event_flags_create(&_m.evt, "modem.evt") != TX_SUCCESS)
  {
    LERROR(TRACE_MODEM, "tx_event_flags_create failed");
    return -1;
  }
  /* TX serialisation (between shell mdm + nn_task auto-notify + binary upload) */
  if (tx_mutex_create(&_m.tx_mtx, "modem.tx_mtx", TX_INHERIT) != TX_SUCCESS)
  {
    return -2;
  }
  /* Response collector serialisation */
  if (tx_mutex_create(&_m.resp_mtx, "modem.resp_mtx", TX_INHERIT) != TX_SUCCESS)
  {
    return -3;
  }
  /* Worker thread */
  UINT status = tx_thread_create(
      &_m.thread, "modem.task", _modem_task_run, 0,
      _modem_stack, sizeof(_modem_stack),
      MODEM_TASK_PRIO, MODEM_TASK_PRIO,
      MODEM_TASK_TIME_SLICE, TX_AUTO_START
  );
  return (status == TX_SUCCESS) ? 0 : -4;
}
