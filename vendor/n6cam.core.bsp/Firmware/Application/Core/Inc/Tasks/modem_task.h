/**
 ******************************************************************************
 * @file    modem_task.h
 * @brief   Public API for the N6 ↔ MangOH modem link over USART2.
 *
 *          Owns USART2 RX/TX and the HDLC framing layer. Exposes a request/
 *          response API for synchronous AT-style commands (`mdm <cmd>` shell
 *          dispatches into modem_send_at), a binary-transport variant for
 *          SoW §8.2 photo upload (modem_send_binary), and a URC callback
 *          slot so the shell task can forward asynchronous +SDVR* lines to
 *          the user terminal as they arrive.
 *
 *          The modem_task runs at MEDIUM priority. RX is event-driven via
 *          HAL_UARTEx_ReceiveToIdle_IT in a loop; the worker decodes HDLC
 *          frames on the fly. TX uses bsp_uart_write under a tx mutex to
 *          serialise concurrent producers (shell, nn_task auto-notify).
 ******************************************************************************
 */
#ifndef _MODEM_TASK_H_
#define _MODEM_TASK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Maximum payload size per HDLC frame (matches the modem's typical line
 *  buffer; SoW §8.2 SENDBIN tail can carry up to size_payload bytes which
 *  are themselves wrapped, so this is the *line* cap, not the binary cap). */
#define MODEM_FRAME_MAX     (1024U)

/** Per-call timeout default for AT command request/response. */
#define MODEM_AT_TIMEOUT_MS (2000U)

/** Attempts per modem_send_at() call: one send plus one retry on timeout.
 *  Backstop under the flag preamble in _tx_framed(); see the comment there
 *  for the CN805 direction-latch behaviour both are defending against. */
#define MODEM_AT_ATTEMPTS   (2U)

/** Maximum line length we'll forward as a URC. */
#define MODEM_URC_MAX       (512U)

/** Longest AT line modem_notify_async() will accept, and how many may be
 *  in flight at once. Four is enough to absorb a detection burst without
 *  making the camera's RAM footprint interesting; a fifth is dropped and
 *  counted rather than silently overwriting an older one.
 *
 *  512 matches the modem's own AT line cap (LE_ATDEFS_COMMAND_MAX_BYTES).
 *  A §6 notification needs more than a single 128-byte AT parameter once
 *  `mod`/`bat`/`vol` carry real values, so the body is split across several
 *  quoted parameters and the quoting overhead lands in this budget. */
#define MODEM_NOTIFY_MAX    (512U)
#define MODEM_NOTIFY_DEPTH  (4U)

/** Callback signature for asynchronous modem URCs.
 *  @param  line     Raw line bytes (NUL-terminated, no trailing \r\n).
 *  @param  len      Length of `line` (excluding NUL).
 *  @param  user_ctx Pointer passed to modem_set_urc_callback. */
typedef void (*t_modem_urc_cb)(const char *line, size_t len, void *user_ctx);

/** Link diagnostics, reported by `mdm stats`.
 *
 *  Every layer of this link discards malformed input silently — bytes seen
 *  before an opening 0x7E, frames whose CRC fails, responses that arrive
 *  after a timeout. That makes a half-working link look exactly like a dead
 *  one from the shell. These counters separate the cases:
 *
 *    rx_bytes == 0                  → nothing reaching PF6 at all
 *    rx_bytes > 0, rx_stray high    → bytes arrive but never frame: far end
 *                                     isn't speaking HDLC, or baud/levels are
 *                                     wrong enough to corrupt every flag byte
 *    rx_overflow > 0                → frames arrive but are longer than the
 *                                     decoder buffer: a wrong peer, not noise
 *    rx_bad > 0                     → flags found but CRC fails: framing is
 *                                     right, the line is corrupting payload
 *    uart_errors climbing           → ORE/FE/NE at the peripheral
 *    rx_frames > 0                  → the RX path is healthy end to end
 */
typedef struct
{
  uint32_t rx_bytes;      /*!< raw bytes read off USART2 */
  uint32_t rx_frames;     /*!< CRC-valid HDLC frames decoded */
  uint32_t rx_bad;        /*!< frames dropped: bad CRC (see rx_overflow)   */
  uint32_t rx_overflow;   /*!< frames dropped: payload exceeded the decoder */
  uint32_t rx_stray;      /*!< bytes seen outside any frame */
  uint32_t rx_truncated;  /*!< lines cut to MODEM_URC_MAX, tail discarded   */
  uint32_t rx_errors;     /*!< bsp_uart_read() error returns */
  uint32_t rx_timeouts;   /*!< bsp_uart_read() timeouts (idle link) */
  uint32_t tx_frames;     /*!< HDLC frames written to USART2 */
  uint32_t tx_errors;     /*!< bsp_uart_write() failures */
  uint32_t tx_retries;    /*!< commands re-sent after a response timeout */
  uint32_t uart_errors;   /*!< USART2 ORE/FE/NE count (from the BSP) */
  uint32_t ntf_queued;      /*!< notifications accepted by modem_notify_async */
  uint32_t ntf_sent;        /*!< notifications the modem answered OK */
  /*! Notifications written to the link that no OK came back for.
   *
   *  NOT the same as "not delivered", and the difference matters. Measured
   *  on the bench: of 64 AT+SDVRNTFA commands that reached the modem, 64
   *  were decoded and 63 produced a UDP datagram (the 64th was a
   *  deliberately malformed one) — while the camera counted several as
   *  unconfirmed. The loss is on the *return* path, the same CN805
   *  direction-latch that eats the occasional reply.
   *
   *  The notifier DOES retry, but only because the modem now suppresses a
   *  repeat of a numerator it handled in the last 30 s: AT+SDVRNTFA is
   *  idempotent in N, so a retry costs one extra frame rather than a
   *  duplicate event at the server. Treat a non-zero count as "the ack path
   *  is lossy", not "events were lost". */
  uint32_t ntf_unconfirmed;
  uint32_t ntf_dropped;     /*!< notifications refused: queue full or too long */
  uint32_t relinks;         /*!< USART2 re-inits performed to recover the link */
  uint32_t consec_timeouts; /*!< command timeouts since the last good response */
} t_modem_stats;

/**
 * @brief Start the modem task.
 * @return Error code (0 on success).
 */
int32_t modem_task_start(void);

/**
 * @brief Send an AT-style command and wait for the modem's response.
 *
 *        Wraps `cmd` + CRLF in an HDLC frame, transmits on USART2, then
 *        collects HDLC frames until a terminator is observed (CRLF "OK",
 *        CRLF "ERROR", "+CME ERROR: ..." or timeout). Concatenates the
 *        body bytes into `reply` (NUL-terminated) and returns the number
 *        of bytes written.
 *
 *        URC frames received while waiting are passed to the registered
 *        callback (see modem_set_urc_callback) and not included in `reply`.
 *
 * @param  cmd        Command string (e.g. "AT", "AT+CSQ", "SDVR+PING=42").
 * @param  reply      Output buffer for the modem's reply (NUL-terminated).
 * @param  reply_cap  Capacity of `reply` (bytes incl. NUL).
 * @param  timeout_ms Per-command timeout. Pass MODEM_AT_TIMEOUT_MS for default.
 * @return Number of bytes written to `reply` (excluding NUL), or negative on
 *         error: -1 = TX failed, -2 = timeout waiting for terminator,
 *         -3 = reply buffer too small.
 */
int32_t modem_send_at(const char *cmd, char *reply, size_t reply_cap,
                      uint32_t timeout_ms);

/**
 * @brief As modem_send_at, but puts the command on the wire exactly once.
 *
 *        modem_send_at retries on a response timeout, which is right for a
 *        query and wrong for anything with a side effect. The ack path over
 *        the CN805 link is lossy, so "no reply" does NOT mean "not received":
 *        on the bench the retry fired routinely while the modem had processed
 *        the first copy perfectly well, and every notification reached the
 *        server twice.
 *
 *        Use this for any command that must happen at most once. The caller
 *        gets -2 on timeout and has to decide what that means; for
 *        notifications the answer is "assume delivered, count it as
 *        unconfirmed, do not resend".
 */
int32_t modem_send_at_once(const char *cmd, char *reply, size_t reply_cap,
                           uint32_t timeout_ms);

/**
 * @brief Send a SoW §8.2-style command followed by a binary payload.
 *
 *        Used to ship a photo from the N6 buffer up to the modem over UART
 *        instead of via SD: the prefix line carries TIME/REF/TAG/SIZE
 *        metadata, and the modem switches into binary-receive mode for the
 *        next `size` bytes.
 *
 *        Both the prefix (with CRLF) and the binary payload are HDLC-framed
 *        and transmitted under the tx mutex so a concurrent shell `mdm`
 *        command can't interleave with them.
 *
 * @param  prefix_line  ASCII command line, e.g.
 *                      `SDVR+SENDBIN=0,"photo","23052026111111",42,57732`.
 *                      Caller does NOT append \r\n; we append before HDLC.
 * @param  payload      Binary payload bytes.
 * @param  payload_len  Length of `payload`.
 * @param  timeout_ms   Per-call timeout for both prefix and payload.
 * @return 0 on success, negative on error: -1 = TX failed.
 */
int32_t modem_send_binary(const char *prefix_line,
                          const uint8_t *payload, size_t payload_len,
                          uint32_t timeout_ms);

/**
 * @brief Queue an AT line to be sent to the modem, without waiting for it.
 *
 *        modem_send_at() blocks for up to MODEM_AT_TIMEOUT_MS per attempt.
 *        That is fine for `mdm <cmd>`, where a human is waiting, and wrong
 *        for anything on a hot path: calling it inline from the shell's
 *        notification emitter froze the shell for 10+ seconds, and calling
 *        it from the NN loop would stall inference behind the modem link.
 *
 *        This copies `at_line` into a small queue and returns immediately.
 *        A dedicated worker thread sends the queued lines one at a time via
 *        modem_send_at, so they are serialised with each other and with any
 *        concurrent `mdm` command (all of them go through the tx mutex).
 *
 *        The reply is discarded — only the outcome is counted, in the
 *        ntf_* fields of t_modem_stats. A caller that needs the reply text
 *        wants modem_send_at, on a thread it is allowed to block.
 *
 *        Safe to call from any task context. Not safe from an ISR.
 *
 * @param  at_line  NUL-terminated command, no CRLF (e.g. "AT+SDVRNTFA=...").
 * @return 0 if queued, -1 if `at_line` is NULL/empty/longer than
 *         MODEM_NOTIFY_MAX-1, -2 if the queue is full. Both failures are
 *         counted in ntf_dropped.
 */
int32_t modem_notify_async(const char *at_line);

/** Consecutive command timeouts before the link is assumed wedged and the
 *  USART is re-initialised. Two is the normal worst case (an idle-gap loss
 *  plus its retry), so three means the link is not merely sleepy. */
#define MODEM_RELINK_AFTER  (3U)

/**
 * @brief Re-initialise USART2 to recover a wedged link.
 *
 *        The CN805 FXMA108 is an auto-direction level translator, and it can
 *        latch one way: the camera keeps transmitting with tx_errors at zero
 *        while nothing reaches the modem, and no counter notices. The only
 *        recovery ever observed on the bench was a camera reboot, which works
 *        because bringing the pin back up briefly tri-states TX and lets the
 *        translator re-sense direction. This does that part without the
 *        reboot.
 *
 *        Called automatically after MODEM_RELINK_AFTER consecutive command
 *        timeouts, and by hand via `mdm relink`.
 *
 * @return 0 on success, negative if the UART could not be brought back up.
 */
int32_t modem_relink(void);

/**
 * @brief Fault injection — deliberately wedge the link (test hook).
 *
 *        The real wedge is a hardware behaviour that has never been
 *        reproducible on demand, which left the recovery path above
 *        untestable: it could be written but not shown to work. This models
 *        the failure faithfully by re-initialising USART2 at the wrong baud
 *        rate, so frames really do stop crossing the wire in both directions
 *        — and, crucially, the thing that fixes it is exactly the thing that
 *        fixes the real one, a UART re-init.
 *
 *        Drive it with `mdm test wedge`; the watchdog should then recover the
 *        link within MODEM_RELINK_AFTER commands.
 *
 * @param  wrong_baud  Line rate to mis-configure to (e.g. 9600).
 * @return 0 on success.
 */
int32_t modem_test_wedge(uint32_t wrong_baud);

/**
 * @brief Register a callback for unsolicited result codes.
 *
 *        URCs are HDLC-framed lines that don't match a pending command:
 *        typically `+SDVR…` or `+CMTI` etc. The callback runs in modem_task
 *        context — keep it short, just forward the line to the shell or
 *        a queue.
 *
 * @param  cb        Callback function, or NULL to disable.
 * @param  user_ctx  Opaque pointer passed back to the callback.
 */
void modem_set_urc_callback(t_modem_urc_cb cb, void *user_ctx);

/**
 * @brief Test hook — inject a synthetic decoded frame into the modem
 *        task's normal RX path, as if it had arrived over USART2 from
 *        the modem.
 *
 *        Used by `mdm test urc <line>` to validate the URC-forwarding
 *        path without an actual MangOH connected. Also used by the bench
 *        soak procedure to keep the shell-side URC pipeline exercised.
 *
 * @param  line       Bytes to inject (no HDLC wrapping needed; this
 *                    bypasses the decoder and goes straight into the
 *                    URC/response dispatcher).
 * @param  len        Length of `line`.
 */
void modem_inject_rx(const uint8_t *line, size_t len);

/**
 * @brief Snapshot the link diagnostics counters.
 * @param out  Destination struct (ignored if NULL).
 */
void modem_get_stats(t_modem_stats *out);

/**
 * @brief Zero the link diagnostics counters (the USART2 error count kept by
 *        the BSP is cumulative since boot and is not affected).
 */
void modem_reset_stats(void);

/**
 * @brief Enable/disable the raw RX hex dump to the trace log.
 *
 *        Off by default — at 115200 a chatty link can outrun the trace sink.
 *        Turn it on only while diagnosing, via `mdm raw on`.
 *
 * @param  on  true to dump every received burst as hex.
 */
void modem_set_raw_dump(bool on);

#ifdef __cplusplus
}
#endif
#endif /* _MODEM_TASK_H_ */
