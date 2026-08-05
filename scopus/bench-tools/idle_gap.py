#!/usr/bin/env python3
"""Characterise the first-command-after-idle drop: for a range of idle gaps,
send one `mdm AT` and record whether it succeeds, then a second immediately
after to confirm the link itself is fine."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n6 import open_raw, send

SHELL = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02"
GAPS = [7.0, 7.0, 7.0]


def one(fd):
    t = time.monotonic()
    out = send(fd, "mdm AT", 8.0)
    return ("error" not in out.lower()), time.monotonic() - t


fd = open_raw(SHELL)
try:
    # prime: get the link into a known-good state
    for _ in range(3):
        one(fd)
        time.sleep(0.2)

    print(f"{'idle gap':>9} | {'1st cmd':>16} | {'2nd cmd':>16}")
    print("-" * 50)
    results = []
    for g in GAPS:
        time.sleep(g)
        ok1, t1 = one(fd)
        ok2, t2 = one(fd)
        results.append((g, ok1, ok2))
        print(f"{g:8.1f}s | {'OK ' if ok1 else 'DROP'} {t1:11.2f}s | "
              f"{'OK ' if ok2 else 'DROP'} {t2:11.2f}s")

    print("\nsummary:")
    drops = [g for g, a, _ in results if not a]
    print(f"  first-command dropped after idle gaps: "
          f"{drops if drops else 'none'}")
    print(f"  second command dropped: "
          f"{[g for g, _, b in results if not b] or 'never'}")
finally:
    os.close(fd)
