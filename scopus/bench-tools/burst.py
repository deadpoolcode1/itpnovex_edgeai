#!/usr/bin/env python3
"""Quantify the N6<->modem link: N x `mdm AT`, count successes, and read the
firmware's own RX counters before/after so drops are attributed precisely."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n6 import open_raw, send

SHELL = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02"
N = int(sys.argv[1]) if len(sys.argv) > 1 else 10


def parse_stats(txt):
    d = {}
    m = re.search(r"rx:\s*bytes=(\d+)\s+frames=(\d+)\s+badcrc=(\d+)\s+stray=(\d+)\s+err=(\d+)", txt)
    if m:
        d.update(rx_bytes=int(m.group(1)), rx_frames=int(m.group(2)),
                 badcrc=int(m.group(3)), stray=int(m.group(4)), rx_err=int(m.group(5)))
    m = re.search(r"tx:\s*frames=(\d+)\s+err=(\d+)\s+usart2 err\(ORE/FE/NE\)=(\d+)", txt)
    if m:
        d.update(tx_frames=int(m.group(1)), tx_err=int(m.group(2)), usart2_err=int(m.group(3)))
    return d


fd = open_raw(SHELL)
try:
    before = parse_stats(send(fd, "mdm stats", 4.0))
    ok = 0
    lat = []
    for i in range(N):
        t = time.monotonic()
        out = send(fd, "mdm AT", 8.0)
        dt = time.monotonic() - t
        good = "error" not in out.lower()
        ok += good
        lat.append(dt)
        print(f"  #{i+1:2d} {'OK ' if good else 'ERR'} {dt:5.2f}s  {out.strip().splitlines()[1] if len(out.strip().splitlines())>1 else ''}")
        time.sleep(0.3)
    after = parse_stats(send(fd, "mdm stats", 4.0))
finally:
    os.close(fd)

print(f"\n===== {ok}/{N} succeeded ({100.0*ok/N:.0f}%) =====")
print(f"  latency min/avg/max: {min(lat):.2f} / {sum(lat)/len(lat):.2f} / {max(lat):.2f} s")
print("\n  counter deltas over the burst:")
for k in sorted(set(before) | set(after)):
    b, a = before.get(k, 0), after.get(k, 0)
    print(f"    {k:12s} {b:8d} -> {a:8d}   (+{a-b})")
