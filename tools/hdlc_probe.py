"""Interactive HDLC link prober — works on Windows (pyserial), unlike fake_modem.py.

Bench tool for bringing up the N6Cam <-> mangOH Yellow UART link. It speaks the
exact wire format in vendor/.../hdlc.c (see tools/hdlc.py) and, crucially, it
prints EVERY raw byte it receives -- framed or not, CRC-valid or not. Both
firmware ends silently discard anything that isn't a CRC-valid frame, which is
what makes a half-working link look completely dead from either console.

Typical uses
------------
Listen only (identify the far end's TX pin, watch boot URCs)::

    python tools/hdlc_probe.py COM19 --listen

Prove the mangOH RECEIVES (expect "+SDVRPING: 42" then "OK" back)::

    python tools/hdlc_probe.py COM19 --send "SDVR+PING=42"

Stand in for the modem while testing the camera (answers AT with OK)::

    python tools/hdlc_probe.py COM7 --fake-modem

Send unframed bytes, to A/B whether the far end wants raw AT or HDLC::

    python tools/hdlc_probe.py COM19 --raw "AT"

Requires: pyserial. Python >= 3.7.
"""
from __future__ import annotations

import argparse
import os
import sys
import threading
import time

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required:  python -m pip install pyserial")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hdlc  # noqa: E402  (local sibling module)


def _printable(data: bytes) -> str:
    """Render bytes with non-printables as '.', so bursts stay readable."""
    return "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in data)


class Probe:
    def __init__(self, port: str, baud: int, quiet: bool) -> None:
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.dec = hdlc.Decoder()
        self.quiet = quiet
        self.rx_bytes = 0
        self.frames: list[bytes] = []
        self._t0 = time.monotonic()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)

    # -- lifecycle ------------------------------------------------------
    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)
        self.ser.close()

    # -- rx -------------------------------------------------------------
    def _reader(self) -> None:
        while not self._stop.is_set():
            data = self.ser.read(256)
            if not data:
                continue
            self.rx_bytes += len(data)
            if not self.quiet:
                print(f"[{self._stamp()}] RX {len(data):3d}  {data.hex(' ')}")
                print(f"{'':>13}     |{_printable(data)}|")
            before = len(self.dec.frames)
            self.dec.feed_bytes(data)
            for frame in self.dec.frames[before:]:
                self.frames.append(frame)
                print(f"[{self._stamp()}] FRAME OK  {frame!r}")

    def _stamp(self) -> str:
        return f"{time.monotonic() - self._t0:7.3f}"

    # -- tx -------------------------------------------------------------
    def send_framed(self, line: str) -> None:
        payload = line.encode() + b"\r\n"
        wire = hdlc.encode(payload)
        print(f"[{self._stamp()}] TX framed {payload!r}")
        print(f"{'':>13}     {wire.hex(' ')}")
        self.ser.write(wire)
        self.ser.flush()

    def send_raw(self, line: str) -> None:
        data = line.encode() + b"\r\n"
        print(f"[{self._stamp()}] TX raw    {data!r}")
        self.ser.write(data)
        self.ser.flush()

    def report(self) -> None:
        print("-" * 68)
        print(f"raw bytes received : {self.rx_bytes}")
        print(f"CRC-valid frames   : {len(self.frames)}")
        print(f"bad-CRC frames     : {self.dec.bad_crc}")
        if self.rx_bytes and not self.frames and not self.dec.bad_crc:
            print("\n  Bytes arrived but never formed a frame -- no 0x7E flag seen.")
            print("  The far end is probably NOT speaking HDLC (plain AT text?).")
        elif self.dec.bad_crc and not self.frames:
            print("\n  Frames arrived but every CRC failed -- flags are being seen, so")
            print("  baud is right. Suspect a corrupted line or a codec mismatch.")
        elif not self.rx_bytes:
            print("\n  Nothing at all received. Check TX/RX crossing, ground, and that")
            print("  you are on the far end's TX pin.")


def _fake_modem(probe: Probe, seconds: float, delay_ms: int) -> None:
    """Answer incoming frames like the WP76 would, with a realistic delay.

    The delay matters: the camera's `mdm` command only waits ~200 ms (the
    timeout_ms/10 tick-rate bug in modem_task.c), so an instant reply hides
    the defect exactly the way a TX->RX jumper loopback does.
    """
    print(f"fake-modem: answering for {seconds:.0f}s with {delay_ms} ms latency")
    probe.send_framed("+SDVRRDY: 0.0.0-probe")
    seen = 0
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if len(probe.frames) > seen:
            req = probe.frames[seen].decode(errors="replace").strip()
            seen += 1
            time.sleep(delay_ms / 1000.0)
            if req.upper().startswith("SDVR+PING"):
                arg = req.partition("=")[2] or "0"
                probe.send_framed(f"+SDVRPING: {arg}")
            probe.send_framed("OK")
        time.sleep(0.02)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. COM19 or /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--send", action="append", default=[],
                    metavar="LINE", help="send LINE as an HDLC frame (repeatable)")
    ap.add_argument("--raw", action="append", default=[],
                    metavar="LINE", help="send LINE unframed (repeatable)")
    ap.add_argument("--listen", action="store_true", help="receive only")
    ap.add_argument("--fake-modem", action="store_true",
                    help="stand in for the WP76 and answer the camera")
    ap.add_argument("--delay-ms", type=int, default=300,
                    help="fake-modem reply latency (default 300)")
    ap.add_argument("--wait", type=float, default=5.0,
                    help="seconds to keep listening (default 5)")
    ap.add_argument("--quiet", action="store_true",
                    help="suppress the raw hex dump, show frames only")
    args = ap.parse_args()

    probe = Probe(args.port, args.baud, args.quiet)
    print(f"{args.port} @ {args.baud} 8N1 -- Ctrl-C to stop")
    print("-" * 68)
    probe.start()
    try:
        if args.fake_modem:
            _fake_modem(probe, args.wait if args.wait > 5 else 60.0, args.delay_ms)
        else:
            for line in args.send:
                probe.send_framed(line)
                time.sleep(0.2)
            for line in args.raw:
                probe.send_raw(line)
                time.sleep(0.2)
            deadline = time.monotonic() + args.wait
            while time.monotonic() < deadline:
                time.sleep(0.05)
    except KeyboardInterrupt:
        print()
    finally:
        probe.stop()
        probe.report()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
