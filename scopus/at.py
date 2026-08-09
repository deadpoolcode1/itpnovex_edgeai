#!/usr/bin/env python3
"""Send one AT command to the modem's SDVR channel and print the reply.

    python3 scopus/at.py "AT+SDVRVER"
    python3 scopus/at.py --point-here             # aim the modem at this PC
    python3 scopus/at.py --point-cloud 1.2.3.4    # aim it at a public relay
    python3 scopus/at.py --raw "AT+CPIN?"         # ask the modem itself

--point-here is the whole of "Step 2" in the tester manual: it works out which
of this PC's addresses is on the modem's subnet and sets the five endpoints
from it, then reads them back. It is one command instead of five because the
five have to agree with each other and with the address the server is actually
bound to, and typing them by hand is where that goes wrong.

--point-cloud is the same step for the cellular test, where the endpoints are
a relay on the public internet instead of this PC, and where there is a sixth
thing to get right: the modem needs its own way out. It sets the APN, turns
the data-session keeper on, and then waits for a *route* before calling it a
success — a modem can be registered on LTE with a PDP address and still have
nowhere to send a packet, which is the state that makes every notification
fail with "network unreachable" while every status screen looks healthy.
"""
import argparse
import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
from devices import ModemAt, ModemSsh                        # noqa: E402

MODEM_IP = "192.168.2.2"


def raw_at(cmd, timeout=8.0):
    """Send a command to the modem's OWN AT parser, not to the SDVR channel.

    The FTDI port this script normally uses reaches the SDVR application, which
    bridges anything it does not recognise through to the modem. That bridge is
    fine for +SDVR* work and unusable for the SIM and radio commands: the reply
    comes back at the wrong line settings and arrives as binary rubbish, which
    reads like a broken modem and is not one.

    /dev/ttyAT on the modem itself is the modem's real AT port, so questions
    about the SIM, the slot and the operator are asked there. It costs an SSH
    hop, which is why it is not the default.
    """
    ssh = ModemSsh()
    if not ssh.reachable():
        print(f"ERROR: no SSH to the modem at {MODEM_IP} — this path needs "
              f"the USB Ethernet link, even when testing over cellular.",
              file=sys.stderr)
        return None
    esc = cmd.replace("\\", "\\\\").replace('"', '\\"')
    rc, out, err = ssh.run(
        f'printf "{esc}\\r" | microcom -t {int(timeout * 1000)} /dev/ttyAT',
        timeout=timeout + 10)
    if rc != 0 and not out:
        print(f"ERROR: {err.strip() or 'the modem did not answer'}",
              file=sys.stderr)
        return None
    return out


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


def point_cloud(at, ip, http_port, udp_port, path, apn, auth, user, pwd):
    """Aim the modem at a public relay and switch the backhaul to cellular.

    The cable version of this (--point-here) can work out the address by
    asking the routing table. Nothing can work this one out: the relay is
    wherever you put it, and over cellular the modem has no route to this PC
    at all — the whole point is that both ends talk to a third machine.

    The address must be a dotted IP, not a hostname. The upload leg would
    accept a name (curl resolves it), but the notification leg is a raw UDP
    socket whose endpoint goes through inet_pton, so a hostname there is
    stored happily and then fails on every send.
    """
    try:
        socket.inet_aton(ip)
        if ip.count(".") != 3:
            raise OSError
    except OSError:
        print(f"ERROR: '{ip}' is not a dotted IPv4 address. The notification "
              f"channel cannot resolve names — pass the relay's IP.",
              file=sys.stderr)
        return 2

    print(f"Pointing the modem at the relay at {ip}\n")
    cmds = [f'AT+SDVRNTFHOST="{ip}"',
            f"AT+SDVRNTFPORT={udp_port}",
            f'AT+SDVRHOSTIP="{ip}"',
            f"AT+SDVRPORT={http_port}",
            f'AT+SDVRSRVRPATH="{path}"']
    if apn:
        cmds.append(f'AT+SDVRAPN="{apn}","{auth}","{user}","{pwd}"')
    # Last, so the link comes up against endpoints that are already set.
    cmds.append("AT+SDVRNET=1")

    bad = [c for c in cmds if "OK" not in show(at, c)]

    print("\nRead back what the modem now has:")
    ntf = show(at, "AT+SDVRNTFHOST?") + show(at, "AT+SDVRNTFPORT?")
    got = show(at, "AT+SDVRSRVGET")

    if bad:
        print(f"\nFAILED: {len(bad)} command(s) were not accepted: "
              f"{', '.join(bad)}", file=sys.stderr)
        return 1
    if ip not in got or str(http_port) not in got or path not in got:
        print(f"\nFAILED: the read-back does not show {ip}:{http_port}{path}.",
              file=sys.stderr)
        return 1
    if ip not in ntf or str(udp_port) not in ntf:
        print(f"\nFAILED: the read-back does not show notifications going to "
              f"{ip}:{udp_port}.", file=sys.stderr)
        return 1

    # Bringing the radio up takes tens of seconds and AT+SDVRNET=1 returns
    # immediately, so poll rather than declaring success on the OK. What is
    # being waited for is a *route*, not registration: a modem can be camped
    # on LTE with a PDP address and still have nowhere to send a packet.
    print("\nWaiting for the cellular link (up to 90 s)…")
    deadline = time.time() + 90
    state = ""
    while time.time() < deadline:
        state = at.send("AT+SDVRNET?", 6.0).replace("\r", "")
        fields = state.split("+SDVRNET:")[-1].split(",") if "+SDVRNET:" in state else []
        if len(fields) > 4 and fields[3].strip() == "1":
            break
        time.sleep(5)

    print()
    show(at, "AT+SDVRNET?")
    if "+SDVRNET:" in state:
        f = [x.strip() for x in state.split("+SDVRNET:")[-1].split(",")]
        if len(f) > 4 and f[3] == "1":
            print(f"\nOK — cellular link up. Notifications to {ip}:{udp_port}, "
                  f"photos to http://{ip}:{http_port}{path}")
            return 0
        reg, con, rte = (f[1], f[2], f[3]) if len(f) > 4 else ("?", "?", "?")
        print(f"\nFAILED: no route out after 90 s "
              f"(registered={reg} session={con} route={rte}).\n"
              f"  registered=0 → no coverage, or the SIM is in the slot the\n"
              f"                 modem is not selecting (AT!UIMS? / AT!UIMS=1)\n"
              f"  session=0    → wrong APN, or data not provisioned on the SIM\n"
              f"  route=0      → session up but no default route; check the log",
              file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Send an AT command to the modem's SDVR channel.")
    ap.add_argument("command", nargs="*", help='e.g. "AT+SDVRVER"')
    ap.add_argument("--point-here", action="store_true",
                    help="set the notification + upload endpoints to this PC")
    ap.add_argument("--point-cloud", metavar="IP", default=None,
                    help="set the endpoints to a public relay at this IP and "
                         "switch the modem to its cellular backhaul")
    ap.add_argument("--apn", default=None,
                    help="APN to set alongside --point-cloud")
    ap.add_argument("--apn-auth", default="none",
                    choices=["none", "pap", "chap"])
    ap.add_argument("--apn-user", default="")
    ap.add_argument("--apn-pass", default="")
    ap.add_argument("--raw", action="store_true",
                    help="send the command to the modem's own AT parser "
                         "(SIM, slot and radio questions) instead of the "
                         "SDVR channel")
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--udp-port", type=int, default=9999)
    ap.add_argument("--path", default="/upload")
    ap.add_argument("--timeout", type=float, default=4.0)
    args = ap.parse_args()

    if not args.command and not args.point_here and not args.point_cloud:
        ap.error("give an AT command, or --point-here / --point-cloud")

    # --raw does not touch the FTDI port at all, so it is handled before the
    # port is opened: the SIM questions must still be answerable when
    # something else is holding that port.
    if args.raw:
        if not args.command:
            ap.error("--raw needs a command")
        out = raw_at(" ".join(args.command), args.timeout)
        if out is None:
            return 2
        print(f"[modem /dev/ttyAT]")
        for line in out.replace("\r", "").splitlines():
            if line.strip():
                print(f"      {line.strip()}")
        return 0 if "ERROR" not in out else 1
    if args.point_here and args.point_cloud:
        ap.error("--point-here and --point-cloud aim at different servers; "
                 "pick one")

    try:
        at = ModemAt()
    except RuntimeError:
        print("ERROR: the modem's AT port was not found (expected the FTDI "
              "adapter at /dev/ttyUSB0).", file=sys.stderr)
        return 2
    print(f"[modem {at.tty}]")

    if args.point_here:
        return point_here(at, args.http_port, args.udp_port, args.path)

    if args.point_cloud:
        return point_cloud(at, args.point_cloud, args.http_port, args.udp_port,
                           args.path, args.apn, args.apn_auth, args.apn_user,
                           args.apn_pass)

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
