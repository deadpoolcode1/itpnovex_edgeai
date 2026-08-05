#!/usr/bin/env python3
"""Topology probe: listen on the modem-side FTDI while the N6 transmits on
USART2. If the two are on the same wire pair we will see the N6's HDLC frame
appear on /dev/ttyUSB0."""
import os, sys, time, termios, threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n6 import open_raw as n6_open, send as n6_send

FTDI = "/dev/ttyUSB0"

captured = bytearray()
stop = threading.Event()


def listener():
    fd = n6_open(FTDI)
    try:
        while not stop.is_set():
            try:
                b = os.read(fd, 4096)
                if b:
                    captured.extend(b)
            except BlockingIOError:
                time.sleep(0.01)
    finally:
        os.close(fd)


def main():
    t = threading.Thread(target=listener, daemon=True)
    t.start()
    time.sleep(0.7)
    base = len(captured)
    print(f"[listener on {FTDI} armed, {base}B of pre-existing noise]")

    fd = n6_open("/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02")
    try:
        for cmd in ("mdm raw on", "mdm AT", "mdm stats"):
            print(f"\n{'='*58}\n>>> N6: {cmd}\n{'-'*58}")
            print(n6_send(fd, cmd, 6.0).strip())
    finally:
        os.close(fd)

    time.sleep(0.5)
    stop.set()
    t.join(timeout=2)
    new = bytes(captured[base:])
    print(f"\n{'='*58}\nFTDI captured during N6 TX: {len(new)}B")
    print(f"  hex: {new.hex(' ')[:400]}")
    print(f"  txt: {new[:200]!r}")
    if b"\x7e" in new:
        print("  >>> HDLC flag seen on FTDI: N6 USART2 and FTDI share the wire")
    elif not new:
        print("  >>> nothing: N6 TX does NOT reach the modem-side FTDI (separate links)")


if __name__ == "__main__":
    main()
