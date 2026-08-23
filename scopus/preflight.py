#!/usr/bin/env python3
"""Check the bench is ready before the manual end-to-end test.

Every check here corresponds to something that has actually gone wrong on this
bench and cost an hour of chasing the wrong thing: a serial terminal left open
in another window swallowing the modem's replies, the camera's CDC port having
moved after a reflash, the modem answering nothing because the app is down.
None of those are product faults, and all of them look like one from the middle
of the test.

    python3 scopus/preflight.py

Prints one line per check and exits 0 only if the bench is fit to test on. Each
failure says what to do about it, because the person running this is testing
the product, not administering Linux.
"""
import glob
import os
import re
import socket
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "lib"))

from settings import S  # noqa: E402

# ScopusQA #11: addresses, ports and passwords come from scopus/bench.ini
# (untracked; scopus/bench.ini.template is the committed placeholder copy)
# or from the matching environment variable. See scopus/lib/settings.py.
MODEM_IP = S.get("modem", "ip")
CAM_BY_ID = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02"
MODEM_BY_ID = "/dev/serial/by-id/*FTDI*"
TEST_IMAGE = os.path.join(REPO, "images", "3_people.jpg")

G, R, Y, Z = "\033[92m", "\033[91m", "\033[93m", "\033[0m"
FAILED = []


def check(name, ok, detail="", fix=""):
    mark = f"{G}PASS{Z}" if ok else f"{R}FAIL{Z}"
    print(f"  [{mark}] {name}" + (f"  —  {detail}" if detail else ""))
    if not ok:
        FAILED.append(name)
        if fix:
            for line in fix.splitlines():
                print(f"         {Y}{line}{Z}")
    return ok


def resolve(pattern):
    for link in sorted(glob.glob(pattern)):
        real = os.path.realpath(link)
        if os.path.exists(real):
            return real
    return None


def holders(dev):
    """Which processes have `dev` open, by walking /proc.

    Not fuser/lsof: this has to work without sudo and without either tool
    installed, and the process that matters (a serial monitor in the tester's
    own editor) runs as the same user anyway.
    """
    out = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            for fd in os.listdir(f"/proc/{pid}/fd"):
                if os.readlink(f"/proc/{pid}/fd/{fd}") == dev:
                    with open(f"/proc/{pid}/comm") as f:
                        out.append(f"{f.read().strip()} (pid {pid})")
                    break
        except OSError:
            continue          # process exited, or not ours — either is fine
    return out


def port_free(port, kind="tcp"):
    fam = socket.SOCK_STREAM if kind == "tcp" else socket.SOCK_DGRAM
    s = socket.socket(socket.AF_INET, fam)
    try:
        s.bind(("0.0.0.0", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def main() -> int:
    print("Scopus bench pre-flight\n")

    # ── the two devices are plugged in ─────────────────────────────────
    cam = resolve(CAM_BY_ID)
    check("N6Cam shell port", bool(cam), cam or "not found",
          "The camera is not plugged in, or is still re-enumerating after a\n"
          "firmware update. Wait 30 seconds and run this again.")

    modem_tty = resolve(MODEM_BY_ID) or ("/dev/ttyUSB0"
                                         if os.path.exists("/dev/ttyUSB0")
                                         else None)
    check("Modem AT port", bool(modem_tty), modem_tty or "not found",
          "The modem's FTDI serial adapter is not plugged in.")

    # ── nothing else is on those ports ─────────────────────────────────
    for label, dev in (("N6Cam", cam), ("Modem", modem_tty)):
        if not dev:
            continue
        who = [h for h in holders(dev) if "python" not in h.lower()]
        check(f"{label} port is free", not who,
              "nobody else has it open" if not who else ", ".join(who),
              "Another program is reading this port and will steal the\n"
              "replies this test depends on — the test then fails with\n"
              "'no answer' while the devices are perfectly healthy.\n"
              "Close the serial monitor / terminal window using it\n"
              "(in VS Code: the Serial Monitor panel) and run this again.")

    # ── this PC is on the modem's network ──────────────────────────────
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((MODEM_IP, 9))
        my_ip = s.getsockname()[0]
    except OSError:
        my_ip = None
    finally:
        s.close()
    check("This PC is on the modem's network", bool(my_ip),
          f"this PC is {my_ip}" if my_ip else "no route to 192.168.2.x",
          "Plug the Ethernet cable between the modem and this PC.")

    ping = subprocess.run(["ping", "-c", "2", "-W", "2", MODEM_IP],
                          capture_output=True)
    check("Modem answers on the network", ping.returncode == 0,
          MODEM_IP,
          "The modem is not reachable over Ethernet. Photos are uploaded\n"
          "over this link, so Step 6 cannot pass until it is up.")

    # ── the receiving ports are free ───────────────────────────────────
    check("Server ports are free", port_free(8080) and port_free(9999, "udp"),
          "TCP 8080, UDP 9999",
          "A test server from an earlier run is still going. Close that\n"
          "window (Ctrl-C in it) before starting a new one.")

    # ── the devices actually answer ────────────────────────────────────
    ver = ""
    if modem_tty:
        from devices import ModemAt
        at = ModemAt(modem_tty)
        ver = at.send("AT+SDVRVER", 4.0)

        # Match the reply, not the echo of what we just typed. `"+SDVRVER" in
        # ver` was true either way, so when the app was gone and getty had
        # ttyHSL1, our own command came back off the login prompt, this check
        # passed, and the version was read out of the login banner: the bench
        # reported `version [Etc/GMT-3].` and then failed the check below
        # against it (ScopusQA #9). A missing app has to say so.
        m = re.search(r"\+SDVRVER:\s*([0-9]+(?:\.[0-9]+)+)", ver)
        num = m.group(1) if m else "-"
        login = any(w in ver.lower() for w in ("login:", "password:",
                                               "login incorrect"))
        check("Modem SDVR app answers", bool(m),
              f"version {num}" if m else
              ("a login prompt, not the app" if login else "silent"),
              ("The modem is answering with a Linux login prompt, which means\n"
               "the SDVR application is not installed: Legato reinstalled its\n"
               "factory system after the unit was power-cycled several times\n"
               "in quick succession. Put it back with\n"
               "  python3 scopus/modem_restore.py\n"
               "which also stops it happening again."
               if login else
               "The modem answered nothing. Either something else has the\n"
               "port open (see the check above), or the SDVR application is\n"
               "not running on the modem. Check with\n"
               "  python3 scopus/modem_restore.py --check"))
        if m:
            parts = [int(x) for x in num.split(".")]
            check("Modem firmware is new enough", parts >= [1, 7, 0], f"{num} >= 1.7.0",
                  "Older builds cannot carry the notification JSON intact.\n"
                  "Ask for a modem update before testing.")

    if cam:
        from devices import N6Shell
        sh = N6Shell(cam)
        alive = sh.alive(3.0)
        check("Camera shell answers", alive, "uptime replied" if alive else "silent",
              "The camera is powered but its shell is not responding.\n"
              "Unplug and replug the camera's USB cable, wait 30 s, retry.")
        if alive:
            r = sh.send("mdm AT", "ok", 6.0)
            link = "mdm AT ok" in r or "OK" in r.split("mdm AT", 1)[-1]
            check("Camera can reach the modem", link,
                  "internal cable carries commands" if link else "no reply",
                  "This is the link the whole product depends on. If it is\n"
                  "down, nothing later in the test can pass. Try\n"
                  '  python3 scopus/cam.py "mdm relink"\n'
                  "and if that does not fix it, report it — do not carry on.")
        sh.close()

    # ── the test image is there ────────────────────────────────────────
    check("Test image present", os.path.exists(TEST_IMAGE), TEST_IMAGE,
          "images/3_people.jpg is missing from the repository.")

    print()
    if FAILED:
        print(f"{R}NOT READY{Z} — {len(FAILED)} check(s) failed: "
              f"{', '.join(FAILED)}")
        print("Fix those first; the manual test will fail for reasons that "
              "have nothing to do with the product.")
        return 1
    print(f"{G}READY{Z} — the bench is fit to test on. Go to Step 3 of "
          f"the manual.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
