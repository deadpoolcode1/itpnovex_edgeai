#!/usr/bin/env python3
"""Send one AT command to the modem's SDVR channel and print the reply.

    python3 scopus/at.py "AT+SDVRVER"
    python3 scopus/at.py --point-here          # aim the modem at this PC

--point-here is the whole of "Step 2" in the tester manual: it works out which
of this PC's addresses is on the modem's subnet and sets the five endpoints
from it, then reads them back. It is one command instead of five because the
five have to agree with each other and with the address the server is actually
bound to, and typing them by hand is where that goes wrong.
"""
import argparse
import os
import socket
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
from devices import ModemAt                                  # noqa: E402

MODEM_IP = "192.168.2.2"


def this_pc_ip(modem_ip=MODEM_IP):
    """The address of this PC on the modem's subnet.

    Asking the routing table which source address would be used to reach the
    modem is the only answer that is right by construction — the bench PC also
    has a LAN address, a Tailscale address and a docker bridge, and the modem
    can only reply to the one that shares its subnet.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((modem_ip, 9))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def show(at, cmd, timeout=4.0):
    reply = at.send(cmd, timeout).replace("\r", "").strip("\n")
    print(f"  {cmd}")
    for line in reply.splitlines():
        if line.strip():
            print(f"      {line.strip()}")
    return reply


def point_here(at, http_port, udp_port, path):
    ip = this_pc_ip()
    if not ip:
        print(f"ERROR: this PC has no route to the modem at {MODEM_IP}.\n"
              "       Check the Ethernet cable to the modem.", file=sys.stderr)
        return 2
    print(f"Pointing the modem at this PC ({ip})\n")
    # Quote every string value. The modem's parser rejects a bare dotted
    # address, and answers ERROR rather than guessing.
    cmds = [f'AT+SDVRNTFHOST="{ip}"',
            f"AT+SDVRNTFPORT={udp_port}",
            f'AT+SDVRHOSTIP="{ip}"',
            f"AT+SDVRPORT={http_port}",
            f'AT+SDVRSRVRPATH="{path}"']
    bad = [c for c in cmds if "OK" not in show(at, c)]
    print("\nRead back what the modem now has:")
    # Both endpoints, because they are set by different commands and only the
    # upload one appears in SRVGET — a notification host that silently did not
    # take would otherwise not show up until Step 5 mysteriously received
    # nothing.
    ntf = show(at, "AT+SDVRNTFHOST?") + show(at, "AT+SDVRNTFPORT?")
    got = show(at, "AT+SDVRSRVGET")

    if bad:
        print(f"\nFAILED: {len(bad)} command(s) were not accepted: "
              f"{', '.join(bad)}", file=sys.stderr)
        return 1
    # Judge on the read-back, not on the OKs: a setter that answers OK and
    # stores nothing is exactly the failure this step exists to catch.
    if ip not in got or str(http_port) not in got or path not in got:
        print(f"\nFAILED: the read-back does not show {ip}:{http_port}{path}.",
              file=sys.stderr)
        return 1
    if ip not in ntf or str(udp_port) not in ntf:
        print(f"\nFAILED: the read-back does not show notifications going to "
              f"{ip}:{udp_port}.", file=sys.stderr)
        return 1
    print(f"\nOK — notifications to {ip}:{udp_port}, "
          f"photos to http://{ip}:{http_port}{path}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Send an AT command to the modem's SDVR channel.")
    ap.add_argument("command", nargs="*", help='e.g. "AT+SDVRVER"')
    ap.add_argument("--point-here", action="store_true",
                    help="set the notification + upload endpoints to this PC")
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--udp-port", type=int, default=9999)
    ap.add_argument("--path", default="/upload")
    ap.add_argument("--timeout", type=float, default=4.0)
    args = ap.parse_args()

    if not args.command and not args.point_here:
        ap.error("give an AT command, or --point-here")

    try:
        at = ModemAt()
    except RuntimeError:
        print("ERROR: the modem's AT port was not found (expected the FTDI "
              "adapter at /dev/ttyUSB0).", file=sys.stderr)
        return 2
    print(f"[modem {at.tty}]")

    if args.point_here:
        return point_here(at, args.http_port, args.udp_port, args.path)

    reply = show(at, " ".join(args.command), args.timeout)
    if not reply.strip():
        print("\n(no answer — see 'nothing answers on the modem' in the "
              "manual: something else probably has the port open)",
              file=sys.stderr)
        return 1
    return 0 if "ERROR" not in reply else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
