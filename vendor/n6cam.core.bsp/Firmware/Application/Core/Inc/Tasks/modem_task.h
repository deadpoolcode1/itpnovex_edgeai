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

/** Maximum line length we'll forward as a URC. */
#define MODEM_URC_MAX       (512U)

/** Callback signature for asynchronous modem URCs.
 *  @param  line     Raw line bytes (NUL-terminated, no trailing \r\n).
 *  @param  len      Length of `line` (excluding NUL).
 *  @param  user_ctx Pointer passed to modem_set_urc_callback. */
typedef void (*t_modem_urc_cb)(const char *line, size_t len, void *user_ctx);

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

#ifdef __cplusplus
}
#endif
#endif /* _MODEM_TASK_H_ */
