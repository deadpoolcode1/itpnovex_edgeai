#!/usr/bin/env python3
"""Robustness soak for the N6<->modem tunnel: real AT commands across a wide
spread of idle gaps, including the >=5 s range that used to alternate."""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n6 import open_raw, send

SHELL = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02"
GAPS = [0.0, 0.5, 2.0, 5.0, 8.0, 12.0, 20.0, 6.0, 0.2, 15.0]
CMDS = ["AT", "AT+SDVRPING", "AT+SDVRVER", "AT"]


def stats(fd):
    txt = send(fd, "mdm stats", 4.0)
    m = re.search(r"tx:\s*frames=(\d+)\s+err=(\d+)\s+retries=(\d+)\s+usart2 err\(ORE/FE/NE\)=(\d+)", txt)
    r = re.search(r"rx:\s*bytes=(\d+)\s+frames=(\d+)\s+badcrc=(\d+)\s+stray=(\d+)", txt)
    return (tuple(int(x) for x in m.groups()) if m else None,
            tuple(int(x) for x in r.groups()) if r else None)


fd = open_raw(SHELL)
try:
    t0, r0 = stats(fd)
    ok = fail = 0
    lat = []
    print(f"{'gap':>6} {'command':<14} {'result':<6} {'lat':>7}   reply")
    print("-" * 66)
    for i, g in enumerate(GAPS):
        cmd = CMDS[i % len(CMDS)]
        time.sleep(g)
        t = time.monotonic()
        out = send(fd, f"mdm {cmd}", 8.0)
        dt = time.monotonic() - t
        # A framed reply -- including an application-level +SDVRERR -- means
        # the transport worked. Only the shell's own timeout is a link drop.
        good = "mdm: error" not in out.lower()
        ok, fail = ok + good, fail + (not good)
        lat.append(dt)
        body = [l for l in out.strip().splitlines()[1:] if l.strip() and l.strip() != ">"]
        print(f"{g:5.1f}s {cmd:<14} {'OK' if good else 'DROP':<6} {dt:6.2f}s   "
              f"{(body[0][:32] if body else '')}")
    t1, r1 = stats(fd)

    print(f"\n===== {ok}/{ok+fail} succeeded =====")
    print(f"  latency min/avg/max: {min(lat):.2f} / {sum(lat)/len(lat):.2f} / {max(lat):.2f} s")
    if t0 and t1:
        print(f"  tx frames +{t1[0]-t0[0]}  errors +{t1[1]-t0[1]}  "
              f"RETRIES +{t1[2]-t0[2]}  usart2 err +{t1[3]-t0[3]}")
    if r0 and r1:
        print(f"  rx frames +{r1[1]-r0[1]}  badcrc +{r1[2]-r0[2]}  stray +{r1[3]-r0[3]}")
finally:
    os.close(fd)
