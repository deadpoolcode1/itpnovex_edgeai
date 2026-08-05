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
#include "n6cam_error.h"

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

/* The notifier worker: its own thread, its own event group. It must not
 * share modem_task's group — modem_send_at clears bits there around every
 * command, and a notifier waiting on the same group would race with it.
 *
 * Nor may the notifier's work be folded into _modem_task_run: that loop is
 * the thing that *receives* the response modem_send_at waits for, so a
 * modem_send_at call made from it could never complete. */
#define MODEM_NOTIFY_TASK_STACK (2U * 1024U)
#define MODEM_NOTIFY_PRIO       APP_PRIO_USER_INTERFACE
#define MODEM_EVT_NOTIFY_WORK   BIT(0U)         /* something is in the queue */

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

  /* Link diagnostics. Every layer below this one discards malformed input
   * silently, so without these counters "nothing on the wire" and "bytes
   * arriving but never framing" look identical from the shell. Surfaced by
   * `mdm stats`; the raw hex dump is opt-in via `mdm raw on`. */
  t_modem_stats         stats;
  bool                  raw_dump;

  /* Asynchronous notification queue (see modem_notify_async). Single-slot
   * ring, producer-agnostic: shell_task's notification emitter and the NN
   * loop both push here rather than blocking on the link themselves. */
  TX_THREAD             ntf_thread;
  TX_EVENT_FLAGS_GROUP  ntf_evt;
  TX_MUTEX              ntf_mtx;      /* protects the ring below */
  char                  ntf_q[MODEM_NOTIFY_DEPTH][MODEM_NOTIFY_MAX];
  uint8_t               ntf_head;     /* next slot to send */
  uint8_t               ntf_count;    /* slots currently occupied */
} t_modem_task;

static uint8_t          _modem_stack[MODEM_TASK_STACK_SIZE];
static uint8_t          _modem_ntf_stack[MODEM_NOTIFY_TASK_STACK];
static t_modem_task     _m;
static const char       _crlf[] = "\r\n";
static const char       _hexd[] = "0123456789abcdef";

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

/* Hex-dump one received burst to the trace log. Opt-in (`mdm raw on`): at
 * 115200 a chatty link can outrun the trace sink. */
static void _dump_raw(const uint8_t *buf, size_t len)
{
  char line[(3U * 16U) + 1U];
  for (size_t off = 0U; off < len; off += 16U)
  {
    size_t n = ((len - off) < 16U) ? (len - off) : 16U;
    size_t p = 0U;
    for (size_t i = 0U; i < n; i++)
    {
      line[p++] = _hexd[(buf[off + i] >> 4) & 0x0FU];
      line[p++] = _hexd[buf[off + i] & 0x0FU];
      line[p++] = ' ';
    }
    line[p] = '\0';
    LINFO(TRACE_MODEM, "rx raw +%u: %s", (unsigned)off, line);
  }
}

/* Drive the HDLC decoder with one received chunk. */
static void _feed_chunk(const uint8_t *buf, size_t len)
{
  _m.stats.rx_bytes += (uint32_t)len;
  if (_m.raw_dump)
  {
    _dump_raw(buf, len);
  }

  for (size_t i = 0U; i < len; i++)
  {
    size_t finished = 0U;

    /* Count bytes that arrive while we are between frames. A far end that
     * speaks plain AT, or a baud/level mismatch, shows up here as a large
     * rx_stray with rx_frames stuck at zero -- otherwise indistinguishable
     * from a dead wire, since hdlc_decoder_feed() drops these silently. */
    if (!_m.dec.in_frame && (buf[i] != HDLC_FLAG))
    {
      _m.stats.rx_stray++;
    }

    if (!hdlc_decoder_feed(&_m.dec, buf[i], &finished))
    {
      /* Bad CRC / overflow — decoder is already reset; just keep going. */
      _m.stats.rx_bad++;
      continue;
    }
    if (finished > 0U)
    {
      _m.stats.rx_frames++;
      _dispatch_frame(_m.dec_out, finished);
      /* Re-arm decoder by re-init'ing — same buffer. */
      hdlc_decoder_init(&_m.dec, _m.dec_out, sizeof(_m.dec_out));
    }
  }
}

/* Transmit an HDLC-framed payload over USART2 (caller already holds tx_mtx).
 *
 * The frame is preceded by MODEM_TX_PREAMBLE bare 0x7E flags. This is not
 * cosmetic -- it is what makes the link reliable after an idle gap.
 *
 * The camera<->modem pair sits either side of the mangOH CN805 FXMA108
 * auto-direction level translator. That part infers direction from whichever
 * side drives first and holds it; after the modem answers a command the
 * translator stays latched modem->camera. The first edge we then drive is
 * consumed flipping it back, so the opening byte(s) of our next frame are
 * eaten on the wire and the modem's UART never sees the command at all.
 *
 * Measured on the bench before this change: with >=5 s between commands the
 * link dropped *every other* command in a perfectly alternating DROP/OK
 * pattern (10/10 trials), while back-to-back commands 0.3 s apart were fine
 * (11/12) because the line never idled long enough to re-latch. On the drops
 * the modem logged nothing whatsoever, confirming the loss was on the wire
 * and not in either HDLC implementation -- badcrc and framing-error counters
 * stayed at zero throughout.
 *
 * Bare flags are the ideal sacrifice: hdlc_decoder_feed() treats a flag that
 * closes an empty or partial frame as "just (re)arm" -- no payload emitted,
 * no error counted -- so however many of them the translator swallows, the
 * receiver is left correctly armed for the real frame that follows. Both ends
 * share this same hdlc.c, so the behaviour is identical on the modem side.
 */
#define MODEM_TX_PREAMBLE   16U     /* ~1.4 ms of flags @115200 */
#define MODEM_TX_WAKE_MS    10U     /* settle time after the preamble */

static int32_t _tx_framed(const uint8_t *payload, size_t payload_len,
                          uint32_t timeout_ms)
{
  /* Worst case: every byte gets stuffed → 2*(payload+2)+2. The
   * MODEM_FRAME_MAX cap on payload + CRC keeps us under a fixed bound. */
  static uint8_t wire[2U * (MODEM_FRAME_MAX + 2U) + 2U];
  static uint8_t preamble[MODEM_TX_PREAMBLE];
  size_t wire_len = 0U;

  if (!hdlc_encode(payload, payload_len, wire, sizeof(wire), &wire_len))
  {
    return -1;
  }

  /* Send the preamble as its OWN write, then let the line settle before the
   * real frame goes out.
   *
   * The separation is the part that matters. Bench measurement: a contiguous
   * preamble immediately followed by the frame did NOT help -- every command
   * after an idle gap still needed the retry (tx frames=12, retries=6 over 6
   * commands). What actually recovers the link is elapsed time: once any
   * transmission has woken the far side, the next command succeeds. So the
   * preamble's job is to be that first, sacrificial transmission, and it
   * needs a gap after it rather than more bytes.
   *
   * Cost is MODEM_TX_WAKE_MS per command against a ~180 ms round trip, versus
   * the ~2160 ms a timeout-and-retry costs. Bare flags are inert to the
   * receiver: hdlc_decoder_feed() treats a flag closing an empty frame as
   * "just (re)arm", so the decoder is left correctly armed either way. */
  memset(preamble, HDLC_FLAG, sizeof(preamble));
  if (bsp_uart_write(MODEM_UART, preamble, sizeof(preamble), timeout_ms) < 0)
  {
    _m.stats.tx_errors++;
    return -1;
  }
  tx_thread_sleep((ULONG)((MODEM_TX_WAKE_MS * TX_TIMER_TICKS_PER_SECOND) / 1000U));

  int32_t rc = bsp_uart_write(MODEM_UART, wire, wire_len, timeout_ms);
  if (rc >= 0)
  {
    _m.stats.tx_frames++;
    return 0;
  }
  _m.stats.tx_errors++;
  return -1;
}

void modem_get_stats(t_modem_stats *out)
{
  if (out == NULL) return;
  *out = _m.stats;
  out->uart_errors = bsp_uart_get_errors(MODEM_UART);
}

void modem_reset_stats(void)
{
  memset(&_m.stats, 0, sizeof(_m.stats));
}

void modem_set_raw_dump(bool on)
{
  _m.raw_dump = on;
}

/*----------------------------------------------------------------------------*/
/* Public API                                                                 */
/*----------------------------------------------------------------------------*/

/* Shared body of modem_send_at / modem_send_at_once. `attempts` is how many
 * times the command may be put on the wire; see the note by the retry loop
 * for why that is the caller's decision and not a constant. */
static int32_t _send_at(const char *cmd, char *reply, size_t reply_cap,
                        uint32_t timeout_ms, unsigned attempts)
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

  /* Derive ticks from TX_TIMER_TICKS_PER_SECOND rather than hard-coding a
   * divisor. This used to divide by 10 for a 100 Hz tick, but the app builds
   * with tx_user.h's TX_TIMER_TICKS_PER_SECOND == 1000, so every `mdm`
   * command waited only a tenth of its intended timeout (~200 ms instead of
   * 2000 ms). The WP76 answers through Legato's le_atServer and a PTY, which
   * routinely takes longer than that -- so the reply landed AFTER we had
   * already given up and reported "no modem?", and the partial response in
   * _m.resp_buf was discarded below. A TX->RX jumper loopback hides this
   * completely, because the echo comes back in microseconds. */
  ULONG timeout_ticks = ((ULONG)timeout_ms * TX_TIMER_TICKS_PER_SECOND) / 1000UL;
  if (timeout_ticks == 0U)
  {
    timeout_ticks = 1U;
  }

  int32_t out = -2;

  /* One retry on timeout, as a backstop under the flag preamble in
   * _tx_framed(). The preamble is the actual fix for the CN805 translator's
   * direction latch; this covers the residual case where a frame is still
   * lost (a longer idle, a marginal edge, or the modem genuinely busy). A
   * retry is safe for a QUERY: `mdm` carries AT queries, and a duplicate that
   * does arrive is answered idempotently. Each attempt re-arms the collector
   * so a late reply to attempt 1 cannot be mistaken for attempt 2's response.
   *
   * It is NOT safe for a command with a side effect, which is why `attempts`
   * is a parameter. A notification is the case in point: the ack path is
   * lossy (see ntf_unconfirmed), so the retry fired routinely, the modem had
   * received the first copy perfectly well, and the server got every event
   * TWICE — visible on the bench as two identical datagrams with the same
   * `num` two seconds apart. */
  for (unsigned attempt = 0U; attempt < attempts; attempt++)
  {
    /* Arm the response collector. */
    rtos_mutex_acquire(&_m.resp_mtx, true);
    _m.resp_cap    = (reply_cap < sizeof(_m.resp_buf)) ? reply_cap : sizeof(_m.resp_buf);
    _m.resp_len    = 0U;
    _m.resp_buf[0] = '\0';
    _m.resp_active = true;
    rtos_clear_event(&_m.evt, MODEM_EVT_RESP_DONE | MODEM_EVT_RESP_OVERFLOW);
    rtos_mutex_acquire(&_m.resp_mtx, false);

    if (_tx_framed(payload, cmd_len + 2U, timeout_ms) != 0)
    {
      rtos_mutex_acquire(&_m.resp_mtx, true);
      _m.resp_active = false;
      rtos_mutex_acquire(&_m.resp_mtx, false);
      rtos_mutex_acquire(&_m.tx_mtx, false);
      return -1;
    }

    ULONG got = 0U;
    UINT  tx_status = tx_event_flags_get(
        &_m.evt, MODEM_EVT_RESP_DONE | MODEM_EVT_RESP_OVERFLOW,
        TX_OR_CLEAR, &got, timeout_ticks);

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

    if (out != -2)
    {
      break;              /* answered (or overflowed) — done */
    }
    _m.stats.tx_retries++;
  }

  rtos_mutex_acquire(&_m.tx_mtx, false);
  return out;
}

int32_t modem_send_at(const char *cmd, char *reply, size_t reply_cap,
                      uint32_t timeout_ms)
{
  return _send_at(cmd, reply, reply_cap, timeout_ms, MODEM_AT_ATTEMPTS);
}

int32_t modem_send_at_once(const char *cmd, char *reply, size_t reply_cap,
                           uint32_t timeout_ms)
{
  return _send_at(cmd, reply, reply_cap, timeout_ms, 1U);
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

int32_t modem_notify_async(const char *at_line)
{
  if ((at_line == NULL) || (at_line[0] == '\0'))
  {
    _m.stats.ntf_dropped++;
    return -1;
  }

  size_t len = strlen(at_line);
  if (len >= MODEM_NOTIFY_MAX)
  {
    /* Truncating an AT command would send a different command, so refuse.
     * Loudly: a notification silently shortened to fit is worse than one
     * that never left. */
    LERROR(TRACE_MODEM, "notify: line of %u bytes exceeds %u, dropped",
           (unsigned)len, (unsigned)(MODEM_NOTIFY_MAX - 1U));
    _m.stats.ntf_dropped++;
    return -1;
  }

  rtos_mutex_acquire(&_m.ntf_mtx, true);
  if (_m.ntf_count >= MODEM_NOTIFY_DEPTH)
  {
    rtos_mutex_acquire(&_m.ntf_mtx, false);
    /* Drop the newest rather than evicting an older one: the queue backs up
     * only when the link is slow or down, and in that state the earlier
     * events are the ones with a chance of still being sent. */
    LWARNING(TRACE_MODEM, "notify: queue full (%u), dropping",
             (unsigned)MODEM_NOTIFY_DEPTH);
    _m.stats.ntf_dropped++;
    return -2;
  }

  uint8_t slot = (uint8_t)((_m.ntf_head + _m.ntf_count) % MODEM_NOTIFY_DEPTH);
  memcpy(_m.ntf_q[slot], at_line, len + 1U);
  _m.ntf_count++;
  _m.stats.ntf_queued++;
  rtos_mutex_acquire(&_m.ntf_mtx, false);

  rtos_raise_event(&_m.ntf_evt, MODEM_EVT_NOTIFY_WORK);
  return 0;
}

/* Notifier worker: drains the queue one line at a time, blocking on the
 * modem link so its producers never have to. */
static void _modem_notify_run(uint32_t args)
{
  UNUSED(args);

  /* Same clock-tree dependency as the RX loop — modem_send_at touches
   * USART2, so don't start before the BSP has switched to PLL1/IC14. */
  task_wait_event(TX_EVT_MODEM_REQUIRE);

  LINFO(TRACE_MODEM, "Notifier started");

  /* Static: this thread has a 2 KB stack and MODEM_FRAME_MAX is 1 KB. */
  static char reply[MODEM_FRAME_MAX];

  while (1)
  {
    (void)rtos_wait_any_event(&_m.ntf_evt, MODEM_EVT_NOTIFY_WORK, true);

    for (;;)
    {
      char line[MODEM_NOTIFY_MAX];

      rtos_mutex_acquire(&_m.ntf_mtx, true);
      if (_m.ntf_count == 0U)
      {
        rtos_mutex_acquire(&_m.ntf_mtx, false);
        break;
      }
      memcpy(line, _m.ntf_q[_m.ntf_head], sizeof(line));
      _m.ntf_head = (uint8_t)((_m.ntf_head + 1U) % MODEM_NOTIFY_DEPTH);
      _m.ntf_count--;
      rtos_mutex_acquire(&_m.ntf_mtx, false);

      reply[0] = '\0';
      int32_t n = modem_send_at_once(line, reply, sizeof(reply), MODEM_AT_TIMEOUT_MS);
      if ((n >= 0) && (strstr(reply, "OK") != NULL))
      {
        _m.stats.ntf_sent++;
        LINFO(TRACE_MODEM, "notify: sent (%ld B reply)", (long)n);
      }
      else
      {
        /* Deliberately no retry — see the ntf_unconfirmed note in
         * modem_task.h. The modem has almost certainly already sent this
         * one; resending would duplicate the event at the server. */
        _m.stats.ntf_unconfirmed++;
        LWARNING(TRACE_MODEM, "notify: no ack rc=%ld reply='%.60s' "
                 "(likely delivered — ack path is lossy)", (long)n, reply);
      }
    }
  }
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

  /* Wait for the BSP — specifically the clock tree — before touching USART2.
   * Without this the UART's BRR is computed against the BootROM-default
   * kernel clock and then invalidated when _system_config_clocks() switches
   * to PLL1/IC14, leaving USART2 transmitting and receiving at the wrong
   * line rate. See TX_EVT_MODEM_REQUIRE in common.h for the full rationale. */
  task_wait_event(TX_EVT_MODEM_REQUIRE);

  /* Bring up USART2. */
  int32_t status = bsp_uart_init(MODEM_UART, MODEM_UART_BAUD, false);
  if (status != 0)
  {
    LERROR(TRACE_MODEM, "USART2 init failed: %ld", (long)status);
  }
  else
  {
    bsp_uart_set_mode(MODEM_UART, UART_MODE_IT, UART_MODE_IT);
    LINFO(TRACE_MODEM, "USART2 up @ %u baud (fck=%lu Hz, actual=%lu baud)",
          (unsigned)MODEM_UART_BAUD,
          (unsigned long)bsp_uart_get_kernel_clock(MODEM_UART),
          (unsigned long)bsp_uart_get_actual_baud(MODEM_UART));
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
    else if (n == BSP_ERROR_TIMEOUT)
    {
      /* Idle link — expected once a second while nothing is arriving. */
      _m.stats.rx_timeouts++;
    }
    else if (n < 0)
    {
      /* Framing/noise/overrun on the line. bsp_uart_read() now aborts the
       * stalled transfer so the next call can re-arm, but yield anyway: with
       * no delay a persistent error spins this task at 100% CPU and starves
       * everything below it. A climbing rx_errors with rx_bytes at zero means
       * bytes ARE hitting PF6 but are malformed. */
      _m.stats.rx_errors++;
      tx_thread_sleep(2U);
    }
    /* The shell can still inject test traffic via modem_inject_rx. */
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
  /* Async notification queue: own event group + mutex (see the note by
   * MODEM_EVT_NOTIFY_WORK for why these are not modem_task's). */
  if (tx_event_flags_create(&_m.ntf_evt, "modem.ntf_evt") != TX_SUCCESS)
  {
    return -5;
  }
  if (tx_mutex_create(&_m.ntf_mtx, "modem.ntf_mtx", TX_INHERIT) != TX_SUCCESS)
  {
    return -6;
  }

  /* Worker thread */
  UINT status = tx_thread_create(
      &_m.thread, "modem.task", _modem_task_run, 0,
      _modem_stack, sizeof(_modem_stack),
      MODEM_TASK_PRIO, MODEM_TASK_PRIO,
      MODEM_TASK_TIME_SLICE, TX_AUTO_START
  );
  if (status != TX_SUCCESS)
  {
    return -4;
  }

  /* Notifier thread. Same priority as the RX loop, so it can only run while
   * that loop is blocked in bsp_uart_read — which is where it spends its
   * life — and can never starve it. */
  status = tx_thread_create(
      &_m.ntf_thread, "modem.notify", _modem_notify_run, 0,
      _modem_ntf_stack, sizeof(_modem_ntf_stack),
      MODEM_NOTIFY_PRIO, MODEM_NOTIFY_PRIO,
      MODEM_TASK_TIME_SLICE, TX_AUTO_START
  );
  return (status == TX_SUCCESS) ? 0 : -7;
}
