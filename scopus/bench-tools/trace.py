#!/usr/bin/env python3
"""Listen on the N6 trace UART (ST-Link VCP) while driving mdm on the CDC
shell, to capture the `mdm raw on` hexdump of what USART2 actually receives."""
import os, sys, time, threading
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n6 import open_raw, send

TRACE = "/dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_001900403235511037333439-if01"
SHELL = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02"

cap = bytearray()
stop = threading.Event()


def listener():
    try:
        fd = open_raw(TRACE)
    except OSError as e:
        print(f"trace open failed: {e}")
        return
    try:
        while not stop.is_set():
            try:
                b = os.read(fd, 4096)
                if b:
                    cap.extend(b)
            except BlockingIOError:
                time.sleep(0.01)
    finally:
        os.close(fd)


t = threading.Thread(target=listener, daemon=True)
t.start()
time.sleep(0.5)

fd = open_raw(SHELL)
try:
    for c in ("mdm raw on", "mdm AT", "mdm stats"):
        print(f"\n=== {c} ===\n{send(fd, c, 6.0).strip()}")
finally:
    os.close(fd)

time.sleep(0.5)
stop.set(); t.join(timeout=2)
print(f"\n===== TRACE UART captured {len(cap)}B =====")
print(cap.decode(errors="replace")[-3000:])
