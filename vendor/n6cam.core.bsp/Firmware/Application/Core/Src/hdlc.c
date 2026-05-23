/**
 ******************************************************************************
 * @file    hdlc.c
 * @brief   HDLC-style byte-stuffed framing — implementation.
 *          See `hdlc.h` for protocol details.
 ******************************************************************************
 */
#include "hdlc.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* CRC-16/XMODEM (poly 0x1021, init 0xFFFF, no reflection, no out-XOR).      */
/* Table-driven to keep per-byte cost at one XOR + one shift + one lookup.   */
/* Table precomputed below — 512 B of flash for a meaningful TX/RX speedup.  */
/* ------------------------------------------------------------------------- */
static const uint16_t _crc16_tbl[256] = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
  0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
  0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
  0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
  0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
  0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
  0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
  0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
  0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
  0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
  0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
  0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
  0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
  0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
  0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
  0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
  0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
  0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
  0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
  0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
  0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
  0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
  0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
  0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
  0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
  0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
  0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
  0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
};

uint16_t hdlc_crc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0U; i < len; i++)
  {
    uint8_t idx = (uint8_t)((crc >> 8) ^ data[i]);
    crc = (uint16_t)((crc << 8) ^ _crc16_tbl[idx]);
  }
  return crc;
}

/* ------------------------------------------------------------------------- */
/* Encoder                                                                   */
/* ------------------------------------------------------------------------- */
static bool _put(uint8_t *out, size_t out_cap, size_t *pos, uint8_t b)
{
  if (*pos >= out_cap) return false;
  out[*pos] = b;
  *pos += 1U;
  return true;
}

/** Push a byte after escaping if it collides with a flag/esc reserved
 *  value. Returns false on output overflow. */
static bool _put_stuffed(uint8_t *out, size_t out_cap, size_t *pos, uint8_t b)
{
  if (b == HDLC_FLAG || b == HDLC_ESC)
  {
    if (!_put(out, out_cap, pos, HDLC_ESC)) return false;
    return _put(out, out_cap, pos, (uint8_t)(b ^ HDLC_ESC_MASK));
  }
  return _put(out, out_cap, pos, b);
}

bool hdlc_encode(const uint8_t *payload, size_t payload_len,
                 uint8_t *out, size_t out_cap, size_t *out_len)
{
  if (out == NULL || out_len == NULL) return false;
  size_t pos = 0U;

  if (!_put(out, out_cap, &pos, HDLC_FLAG)) return false;

  for (size_t i = 0U; i < payload_len; i++)
  {
    if (!_put_stuffed(out, out_cap, &pos, payload[i])) return false;
  }

  uint16_t crc = hdlc_crc16(payload, payload_len);
  /* Big-endian on the wire: high byte first. */
  if (!_put_stuffed(out, out_cap, &pos, (uint8_t)((crc >> 8) & 0xFFU))) return false;
  if (!_put_stuffed(out, out_cap, &pos, (uint8_t)(crc & 0xFFU)))        return false;

  if (!_put(out, out_cap, &pos, HDLC_FLAG)) return false;

  *out_len = pos;
  return true;
}

/* ------------------------------------------------------------------------- */
/* Streaming decoder                                                         */
/* ------------------------------------------------------------------------- */
void hdlc_decoder_init(t_hdlc_decoder *d, uint8_t *out, size_t out_cap)
{
  d->out      = out;
  d->out_cap  = out_cap;
  d->out_len  = 0U;
  d->in_frame = false;
  d->escape   = false;
}

static void _reset_frame(t_hdlc_decoder *d)
{
  d->out_len  = 0U;
  d->in_frame = false;
  d->escape   = false;
}

bool hdlc_decoder_feed(t_hdlc_decoder *d, uint8_t b, size_t *frame_out)
{
  if (frame_out != NULL) *frame_out = 0U;

  if (b == HDLC_FLAG)
  {
    /* Flag terminates the current frame (if any) and arms the next one.
     * A flag mid-frame with an escape pending is a protocol error → drop. */
    if (d->in_frame && d->out_len >= 2U && !d->escape)
    {
      /* Validate CRC over the payload (everything except the last 2 bytes,
       * which are the transmitted CRC). */
      size_t payload_len = d->out_len - 2U;
      uint16_t got_crc =
          (uint16_t)((((uint16_t)d->out[payload_len]) << 8) |
                                  d->out[payload_len + 1U]);
      uint16_t want_crc = hdlc_crc16(d->out, payload_len);
      bool ok = (got_crc == want_crc);
      if (ok && frame_out != NULL) *frame_out = payload_len;
      _reset_frame(d);
      d->in_frame = true;   /* the same flag also opens the next frame */
      return ok;
    }
    /* Empty / partial → just (re)arm. */
    _reset_frame(d);
    d->in_frame = true;
    return true;
  }

  if (!d->in_frame) return true;  /* discard inter-frame noise */

  if (d->escape)
  {
    d->escape = false;
    b ^= HDLC_ESC_MASK;
  }
  else if (b == HDLC_ESC)
  {
    d->escape = true;
    return true;
  }

  if (d->out_len >= d->out_cap)
  {
    /* Overflow — abort this frame, wait for the next flag. */
    _reset_frame(d);
    return false;
  }
  d->out[d->out_len++] = b;
  return true;
}
