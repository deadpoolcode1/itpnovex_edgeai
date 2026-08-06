#!/usr/bin/env python3
"""Send one command to the N6Cam shell and print what it answered.

The manual test is walked by hand, one line at a time, by someone who should
not have to keep a serial terminal open and remember how to leave it. So each
step is a self-contained command:

    python3 scopus/cam.py "detect start"
    python3 scopus/cam.py "photo upload"

The port is resolved from the by-id symlink every time, because the CDC port
moves between /dev/ttyACM1 and /dev/ttyACM2 across a reflash and hard-coding
it is the single most common way this test fails for a reason that has nothing
to do with the product.

Exit status is 0 if the command was answered at all, 1 if the shell said
nothing — enough for the tester to see "no answer" without reading tea leaves.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
from devices import N6Shell                                  # noqa: E402

# Commands that are slow because bytes have to cross the internal UART at
# 115200: a ~95 KB JPEG is ~10 s of wire time, plus the upload itself. Waiting
# 3 s and reporting "no answer" on those would be a false failure, so the wait
# follows the command rather than the tester having to know.
SLOW = (("photo",), ("sd", "snap"), ("frame", "run"))
SLOW_SECS = 25.0

# Commands that can simply be sent again when the reply goes missing, because
# sending them twice has the same effect as sending them once. The camera drops
# a console reply whenever it is emitting a notification at that moment, which
# is common enough that the alternative — telling the tester to retype it — is
# most of the manual's failure reports. Everything with a side effect (photo
# upload, frame run, detect simulate, relink, reboot) is deliberately absent.
REPEATABLE = (("uptime",), ("mdm", "stats"), ("mdm", "at"),
              ("detect", "profile"), ("detect", "start"), ("detect", "stop"),
              ("notify",), ("version",), ("help",))
RETRIES = 3


def default_wait(cmd: str) -> float:
    words = cmd.lower().split()
    for pat in SLOW:
        if words[:len(pat)] == list(pat):
            return SLOW_SECS
    return 4.0


def wait_quiet(sh, quiet=0.4, cap=10.0) -> bool:
    """Wait until the shell has been silent for `quiet` seconds.

    The camera writes notifications to this same shell as they happen, and a
    command sent into the middle of one comes back with its reply missing —
    which reads to a tester as "the camera ignored me" when the camera is
    working perfectly. Waiting for a gap first costs nothing when the shell is
    already idle, which is the normal case.
    """
    end = time.time() + cap
    last = time.time()
    while time.time() < end:
        try:
            chunk = os.read(sh.fd, 4096)
        except (BlockingIOError, OSError):
            chunk = b""
        if chunk:
            last = time.time()
        elif time.time() - last >= quiet:
            return True
        time.sleep(0.05)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Send a command to the N6Cam shell and print the reply.")
    ap.add_argument("command", nargs="+",
                    help='the shell command, e.g. "detect start"')
    ap.add_argument("--wait", type=float, default=None,
                    help="seconds to listen for the reply (default: 4, "
                         "25 for photo/frame run)")
    ap.add_argument("--tty", default=None, help="override the CDC port")
    args = ap.parse_args()

    cmd = " ".join(args.command)
    wait = args.wait if args.wait is not None else default_wait(cmd)

    try:
        sh = N6Shell(args.tty)
    except RuntimeError:
        print("ERROR: the N6Cam shell port was not found.\n"
              "       The camera is not plugged in, or it is still "
              "re-enumerating after a firmware update (wait ~30 s).\n"
              "       Expected: /dev/serial/by-id/"
              "usb-STMicroelectronics_N6Cam_*-if02", file=sys.stderr)
        return 2

    print(f"[camera {sh.tty}] > {cmd}")
    words = cmd.lower().split()
    tries = RETRIES if any(words[:len(p)] == list(p)
                           for p in REPEATABLE) else 1

    body = ""
    for n in range(tries):
        wait_quiet(sh)
        body = clean(sh.send(cmd, sentinel=None, max_secs=wait), cmd)
        if body:
            break
        if n + 1 < tries:
            print(f"   (no reply — the camera is busy sending a "
                  f"notification; retrying {n + 2}/{tries})")
            time.sleep(2.0)

    if not body:
        # Nothing came back at all. Ask the shell whether it is still there,
        # so the tester is told which of the two very different situations
        # this is rather than being left to guess.
        alive = sh.alive(3.0)
        sh.close()
        print("(no answer to this command)")
        if alive:
            print("The camera shell itself is alive — the reply was most "
                  "likely lost behind a notification.\nRun the same command "
                  "again.")
        else:
            print("The camera is not answering at all. It may have just "
                  "restarted;\nwait 30 seconds, then run: python3 "
                  "scopus/preflight.py")
        return 1
    sh.close()
    print(body)
    return 0


def clean(reply: str, cmd: str) -> str:
    """The reply with the command's own echo and the redrawn prompt removed."""
    body = reply
    i = body.find(cmd)
    if i >= 0:
        body = body[i + len(cmd):]
    body = body.replace("\r", "").strip("\n")
    while body.endswith(">") or body.endswith(" "):
        body = body[:-1].rstrip("\n ")
    return body.strip()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
