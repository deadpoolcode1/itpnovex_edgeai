"""Host-side companion to vendor/.../hdlc.c.

Identical wire format: 0x7E flags, 0x7D escape with XOR 0x20, CRC-16/XMODEM
appended big-endian before the trailing flag.

Used by:
  * tests/test_hdlc.py — round-trip the encoder/decoder against the kit
    binary via the modem UART loopback path (see modem_task.c)
  * the `fake_modem.py` host stub for `mdm` end-to-end testing
  * the `tools/photo_upload_listener.py` host listener for SoW §8.2

Pure-stdlib so we can run it from any Python ≥ 3.7 on the dev host.
"""
from __future__ import annotations

HDLC_FLAG = 0x7E
HDLC_ESC = 0x7D
HDLC_ESC_MASK = 0x20


def crc16(data: bytes) -> int:
    """CRC-16/XMODEM. Matches hdlc_crc16() in hdlc.c byte-for-byte."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def encode(payload: bytes) -> bytes:
    """Wrap a payload in an HDLC frame ready for the wire."""
    crc = crc16(payload)
    body = payload + bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    out = bytearray([HDLC_FLAG])
    for b in body:
        if b in (HDLC_FLAG, HDLC_ESC):
            out.append(HDLC_ESC)
            out.append(b ^ HDLC_ESC_MASK)
        else:
            out.append(b)
    out.append(HDLC_FLAG)
    return bytes(out)


class Decoder:
    """Streaming HDLC decoder. Feed bytes via .feed(b); completed frames
    are appended to .frames (each one is the decoded payload, CRC already
    verified and stripped). On CRC mismatch the frame is silently dropped."""

    def __init__(self, max_payload: int = 4096) -> None:
        self._max = max_payload
        self._buf = bytearray()
        self._in_frame = False
        self._escape = False
        self.frames: list[bytes] = []
        self.bad_crc = 0

    def feed(self, b: int) -> None:
        if b == HDLC_FLAG:
            if self._in_frame and len(self._buf) >= 2 and not self._escape:
                payload = bytes(self._buf[:-2])
                got = (self._buf[-2] << 8) | self._buf[-1]
                if got == crc16(payload):
                    self.frames.append(payload)
                else:
                    self.bad_crc += 1
            self._buf.clear()
            self._in_frame = True
            self._escape = False
            return
        if not self._in_frame:
            return
        if self._escape:
            self._escape = False
            b ^= HDLC_ESC_MASK
        elif b == HDLC_ESC:
            self._escape = True
            return
        if len(self._buf) >= self._max:
            # Overflow — abort the frame, wait for next flag.
            self._buf.clear()
            self._in_frame = False
            self._escape = False
            return
        self._buf.append(b)

    def feed_bytes(self, data: bytes) -> None:
        for b in data:
            self.feed(b)


# ---- Self-test --------------------------------------------------------
def _selftest() -> None:
    cases = [
        b"AT\r\n",
        b"\x7E\x7D\x7E\x7Dembedded flags and escapes",
        b"",
        bytes(range(256)),
        b"+SDVRUPL: PROGR,\"4325412_23052026_111111.rdy\",1\r\n",
    ]
    for p in cases:
        wire = encode(p)
        d = Decoder()
        d.feed_bytes(wire)
        assert d.frames == [p], (
            f"round-trip mismatch:\n  in : {p!r}\n  wire: {wire!r}\n  out: {d.frames!r}"
        )
        assert d.bad_crc == 0

    # Corrupted CRC → frame dropped silently
    wire = bytearray(encode(b"hello"))
    wire[-3] ^= 0x01  # flip a bit inside the CRC (right before the trailing flag)
    d = Decoder()
    d.feed_bytes(bytes(wire))
    assert d.frames == [], "corrupted-CRC frame should be dropped"
    assert d.bad_crc == 1

    print("hdlc.py self-test: OK")


if __name__ == "__main__":
    _selftest()
