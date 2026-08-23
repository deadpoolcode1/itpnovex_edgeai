#!/usr/bin/env python3
"""For each trial: idle, send ONE command, then read the modem's log by line
count (fresh ssh each time, no streaming = no buffering) to see whether the
modem's app ever saw that frame."""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from n6 import open_raw, send

# ScopusQA #11: the modem password is site data, not source. It comes from
# scopus/bench.ini (untracked) or $MODEM_PASSWORD — see scopus/lib/settings.py.
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.dirname(
    _os.path.abspath(__file__))), "lib"))
from settings import S as _S

SSH = ["sshpass", "-p", _S.require("modem", "password"),
       "ssh", "-o", "StrictHostKeyChecking=no",
       "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR",
       "root@192.168.2.2"]
LOG = "/data/sdvr/sdvr.log"


def modem(cmd):
    try:
        return subprocess.run(SSH + [cmd], capture_output=True, text=True,
                              timeout=25).stdout
    except Exception as e:
        return f"(ssh failed: {e})"


def loglines():
    out = modem(f"wc -l < {LOG}")
    try:
        return int(out.strip())
    except Exception:
        return -1


def logsince(n):
    return modem(f"tail -n +{n+1} {LOG} | grep -E 'HdlcChannel|UartFilter'")


IDLE = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
TRIALS = int(sys.argv[2]) if len(sys.argv) > 2 else 4

fd = open_raw("/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02")
try:
    for i in range(TRIALS):
        print(f"\n{'='*58}\nTRIAL {i+1}: idling {IDLE}s")
        time.sleep(IDLE)
        before = loglines()
        t = time.monotonic()
        out = send(fd, "mdm AT", 8.0)
        dt = time.monotonic() - t
        ok = "error" not in out.lower()
        time.sleep(1.5)
        seen = logsince(before).strip()
        print(f"  N6 result : {'OK' if ok else 'DROP'} ({dt:.2f}s)")
        print(f"  modem saw : {seen if seen else '(NOTHING logged)'}")
        verdict = ("link fine" if ok else
                   ("modem RECEIVED it, reply lost" if "RX" in seen
                    else "frame never reached the modem"))
        print(f"  verdict   : {verdict}")
finally:
    os.close(fd)
