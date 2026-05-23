/**
 ******************************************************************************
 * @file    hdlc.h
 * @brief   HDLC-style framing for the N6 ↔ MangOH USART2 modem link.
 *
 *          Wraps a payload with 0x7E start/end flags, byte-stuffs any
 *          0x7E / 0x7D inside the payload (XOR 0x20), and appends a 16-bit
 *          CCITT-CRC. Matches Scopus SoW §5.1 ("HDLC protocol to wrap the
 *          messages, such that there won't be difference if the command is
 *          ASCII or binary"). Single sender / single receiver, no
 *          address/control fields — the "tiny HDLC" subset everybody uses
 *          for AT-over-binary tunnels.
 *
 *          Pure C, no RTOS dependency. Lives in the Application layer so
 *          the modem_task can use it on USART2 and the host-side python
 *          test harness can replicate the bit pattern.
 ******************************************************************************
 */
#ifndef _HDLC_H_
#define _HDLC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Frame delimiter — appears once at the start, once at the end. */
#define HDLC_FLAG          (0x7EU)
/** Escape byte — preceded byte is XOR'd with HDLC_ESC_MASK. */
#define HDLC_ESC           (0x7DU)
#define HDLC_ESC_MASK      (0x20U)

/** Worst-case wire size for a payload of N bytes: 1 flag + 2*(N+2) escaped
 *  bytes for payload+CRC + 1 flag. CRC is 2 bytes. */
#define HDLC_WIRE_MAX(n)   (1U + 2U * ((n) + 2U) + 1U)

/** Decoder state — caller-allocated, zero-initialise on first use. */
typedef struct
{
  uint8_t *out;        /* caller's output buffer */
  size_t   out_cap;    /* capacity of `out` */
  size_t   out_len;    /* bytes written so far in current frame (incl. CRC) */
  bool     in_frame;   /* between two flags */
  bool     escape;     /* previous byte was 0x7D, next is escaped */
} t_hdlc_decoder;

/**
 * @brief Encode payload into an HDLC frame.
 *
 * @param  payload      Input payload (may contain any bytes incl. 0x7E/0x7D).
 * @param  payload_len  Length of `payload`.
 * @param  out          Output buffer, must be at least HDLC_WIRE_MAX(payload_len).
 * @param  out_cap      Capacity of `out`.
 * @param  out_len      [out] number of bytes written to `out`.
 * @return true on success, false if `out` was too small.
 */
bool hdlc_encode(const uint8_t *payload, size_t payload_len,
                 uint8_t *out, size_t out_cap, size_t *out_len);

/**
 * @brief Initialise / reset a streaming decoder.
 *
 * @param  d            Decoder state.
 * @param  out          Buffer where decoded payloads will be written.
 * @param  out_cap      Capacity of `out`.
 */
void hdlc_decoder_init(t_hdlc_decoder *d, uint8_t *out, size_t out_cap);

/**
 * @brief Feed one wire byte into the decoder.
 *
 * @param  d            Decoder state.
 * @param  b            Next byte off the wire.
 * @param  frame_out    [out] When a complete, CRC-valid frame finishes,
 *                      *frame_out is set to the payload length (excluding
 *                      the trailing CRC). Caller reads `d->out` and resets
 *                      via hdlc_decoder_init() or another feed cycle.
 *                      Otherwise *frame_out stays 0.
 * @return true on success, false on overflow / bad CRC (decoder is auto-
 *         reset for the next frame either way).
 */
bool hdlc_decoder_feed(t_hdlc_decoder *d, uint8_t b, size_t *frame_out);

/**
 * @brief Compute CCITT-CRC16 over a buffer. Useful for the host-side
 *        encoder + for unit-testing the framing in isolation.
 *
 *        Poly 0x1021, init 0xFFFF, no input/output reflection, no XOR-out.
 *        Standard "CRC-16/XMODEM" / "CRC-CCITT (XModem)".
 */
uint16_t hdlc_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* _HDLC_H_ */
