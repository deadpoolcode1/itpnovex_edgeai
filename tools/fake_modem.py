#!/usr/bin/env python3
"""Host-side fake of the MangOH WP76 modem, sitting at the far end of a
USB-UART adapter that's wired to the N6Cam's USART2 J503 pins
(7=TX, 9=RX, 11=GND, 3.3 V).

Used to bring up the M3 cellular plane without the actual MangOH on the
bench. The kit talks HDLC-wrapped AT commands to us; we decode, answer
sensibly, and occasionally emit a synthetic URC so the kit's URC
forwarding path gets exercised.

Usage:
    sudo python3 tools/fake_modem.py /dev/ttyUSB0
    # or for a loopback test against itself:
    python3 tools/fake_modem.py /tmp/loop_a   # + socat to /tmp/loop_b

The script logs every frame to stdout in human form. Ctrl-C to stop.

Handled commands:
    AT, ATI, AT+CSQ              — generic AT probes
    AT+CPIN?                     — SIM check
    SDVR+PING=<n>                — answers +SDVRPING: <n>
    SDVR+HOST="…"                — config; remembered
    SDVR+HOSTIP=<ip>             — config; remembered
    SDVR+SENDBIN=N,"TAG","TIME",REF,SIZE  — eats SIZE bytes binary,
                                            emits +SDVRSRVR: OK after

Auto-fires every 30 s:
    +SDVRRDY: 1.0.5              — pretend the modem booted
    +SDVRUPL: PROGR,"file",N     — fake upload progress

This is intentionally a *thin* mock — it gets the framing right (HDLC,
CRLF line discipline) and the SoW §5/§6/§8.2 surface area right, but
doesn't actually do cellular. When the real MangOH arrives, throw this
away.
"""
from __future__ import annotations
import os
import sys
import time
import threading
from pathlib import Path

HERE = Path(__file__).parent
sys.path.insert(0, str(HERE))
import hdlc  # noqa: E402


def _open_tty(path: str):
    """Open in raw mode at 115200, return an fd."""
    os.system(f"stty -F {path} 115200 cs8 -cstopb -parenb raw -echo")
    return os.open(path, os.O_RDWR | os.O_NOCTTY)


class FakeModem:
    def __init__(self, tty: str) -> None:
        self.tty = tty
        self.fd = _open_tty(tty)
        self.dec = hdlc.Decoder(max_payload=4096)
        # Binary-receive state for SDVR+SENDBIN
        self.bin_expect = 0
        self.bin_seen = 0
        self.bin_ref = 0
        self.bin_tag = ""
        self.config: dict[str, str] = {}
        self.urc_seq = 0
        self.lock = threading.Lock()
        self.stop = False

    # ── TX ────────────────────────────────────────────────────────
    def send_frame(self, payload: bytes) -> None:
        wire = hdlc.encode(payload)
        with self.lock:
            os.write(self.fd, wire)
        print(f"  TX [{len(payload):4d}B]: {payload[:64]!r}{'…' if len(payload) > 64 else ''}",
              flush=True)

    def send_line(self, line: str) -> None:
        if not line.endswith("\r\n"):
            line = line + "\r\n"
        self.send_frame(line.encode())

    # ── Command dispatch ─────────────────────────────────────────
    def handle_payload(self, payload: bytes) -> None:
        print(f"  RX [{len(payload):4d}B]: {payload[:64]!r}{'…' if len(payload) > 64 else ''}",
              flush=True)

        # Mid binary-receive? consume the bytes until we hit the announced size.
        if self.bin_expect > 0:
            take = min(self.bin_expect - self.bin_seen, len(payload))
            self.bin_seen += take
            print(f"  BIN: {self.bin_seen}/{self.bin_expect}", flush=True)
            if self.bin_seen >= self.bin_expect:
                # Done — pretend we POSTed to the HTTP server and ack.
                self.send_line(f'+SDVRSRVR: OK')
                self.send_line(f'+SDVRUPL: END,"upload_ref{self.bin_ref}"')
                self.send_line("OK")
                self.bin_expect = 0
                self.bin_seen = 0
            # If payload had trailing AT command after binary tail, we'd
            # have to handle it here. For now assume one frame = one
            # payload (which is what modem_task.c does).
            return

        # Parse one or more CRLF-separated lines.
        for raw in payload.split(b"\n"):
            line = raw.rstrip(b"\r").decode("ascii", errors="replace").strip()
            if not line:
                continue
            self.handle_line(line)

    def handle_line(self, line: str) -> None:
        u = line.upper()

        # SDVR+SENDBIN=<N>,"<TAG>","<TIME>",<REF>,<SIZE>
        if line.startswith("SDVR+SENDBIN="):
            try:
                args = line[len("SDVR+SENDBIN="):].split(",")
                n   = int(args[0])
                tag = args[1].strip('"')
                _t  = args[2].strip('"')
                ref = int(args[3])
                size = int(args[4])
                self.bin_expect = size
                self.bin_seen = 0
                self.bin_ref = ref
                self.bin_tag = tag
                print(f"  Expecting {size} binary bytes (tag={tag!r}, ref={ref})", flush=True)
                self.send_line(f'+SDVRUPL: START,"{tag}_{ref}"')
            except Exception as e:
                self.send_line(f'+CME ERROR: bad SENDBIN args ({e})')
            return

        if u == "AT" or u == "ATI":
            self.send_line("OK"); return
        if u == "AT+CSQ":
            self.send_line("+CSQ: 17,99")
            self.send_line("OK"); return
        if u == "AT+CPIN?":
            self.send_line("+CPIN: READY")
            self.send_line("OK"); return
        if line.startswith("SDVR+PING="):
            n = line.split("=", 1)[1]
            self.send_line(f"+SDVRPING: {n}")
            self.send_line("OK"); return
        if line.startswith("SDVR+HOST=") or line.startswith("SDVR+HOSTIP="):
            k, v = line.split("=", 1)
            self.config[k] = v
            self.send_line("OK"); return

        # Unknown — generic ERROR
        self.send_line("+CME ERROR: unknown command")

    # ── RX loop ──────────────────────────────────────────────────
    def rx_loop(self) -> None:
        buf = b""
        while not self.stop:
            try:
                chunk = os.read(self.fd, 4096)
                if not chunk:
                    time.sleep(0.01)
                    continue
            except BlockingIOError:
                time.sleep(0.01)
                continue
            self.dec.feed_bytes(chunk)
            while self.dec.frames:
                self.handle_payload(self.dec.frames.pop(0))

    # ── URC heartbeat ────────────────────────────────────────────
    def urc_loop(self) -> None:
        # First URC: "modem ready" 2 s after start
        time.sleep(2.0)
        self.send_line("+SDVRRDY: 1.0.5")
        while not self.stop:
            time.sleep(30.0)
            self.urc_seq += 1
            self.send_line(f"+SDVRUPL: PROGR,\"fake-{self.urc_seq}.jpg\",1")

    def run(self) -> None:
        threads = [
            threading.Thread(target=self.rx_loop, daemon=True),
            threading.Thread(target=self.urc_loop, daemon=True),
        ]
        for t in threads: t.start()
        try:
            while True:
                time.sleep(1.0)
        except KeyboardInterrupt:
            self.stop = True
            print("\nfake_modem stopping.", flush=True)


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: fake_modem.py <tty>   e.g. /dev/ttyUSB0", file=sys.stderr)
        return 1
    print(f"fake_modem.py listening on {sys.argv[1]} @ 115200 8N1", flush=True)
    FakeModem(sys.argv[1]).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
