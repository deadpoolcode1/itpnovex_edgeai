#!/usr/bin/env python3
"""Scopus test server — the "server" end of the end-to-end chain, on your PC.

The product sends two different things to two different places:

  * notifications  — a UDP datagram carrying the SoW §6 JSON, to the endpoint
                     set by AT+SDVRNTFHOST / AT+SDVRNTFPORT
  * photos         — an HTTP POST carrying the JPEG, to the endpoint set by
                     AT+SDVRHOSTIP / AT+SDVRPORT / AT+SDVRSRVRPATH

Running two separate listeners for a manual test is a good way to have one of
them not running when the event arrives, so this is both, in one process, with
one log. Everything received is printed as it arrives and saved under
--dir, so after a test run you can look at what actually landed rather than
trusting a "sent OK" from the device.

    python3 scopus/test_server.py                  # 0.0.0.0:8080 HTTP, :9999 UDP
    python3 scopus/test_server.py --http-port 8991 --udp-port 5005

Nothing here is Scopus-specific beyond the headers it logs — it is deliberately
a dumb receiver, so that a test passing means the DEVICE did the right thing.
"""
import argparse
import datetime
import http.server
import json
import pathlib
import socket
import sys
import threading

C = {"ntf": "\033[95m", "up": "\033[92m", "err": "\033[91m",
     "dim": "\033[96m", "b": "\033[1m", "0": "\033[0m"}

STATE = {"notifications": [], "uploads": []}
LOCK = threading.Lock()
OUT_DIR = pathlib.Path(".")


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S")


def log(colour, tag, msg):
    print(f"{C['dim']}[{stamp()}]{C['0']} {colour}{tag:<14}{C['0']} {msg}",
          flush=True)


# ───────────────────────────── notifications ──────────────────────────────
def udp_listener(port, expect_from=None):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    log(C["b"], "listening", f"UDP  0.0.0.0:{port}  (notifications)")

    while True:
        data, addr = sock.recvfrom(65535)
        # Filtering on source matters: unrelated LAN traffic on this port has
        # been mistaken for a passing test on this bench before.
        if expect_from and addr[0] != expect_from:
            log(C["err"], "notify?", f"ignored datagram from {addr[0]} "
                                     f"(expected {expect_from})")
            continue
        text = data.decode(errors="replace")
        try:
            obj = json.loads(text)
            pretty = " ".join(f"{k}={obj[k]!r}" for k in
                              ("ser", "num", "rsn", "rsd", "tim", "mtn")
                              if k in obj)
            log(C["ntf"], "NOTIFICATION", f"from {addr[0]}  {pretty}")
            log(C["dim"], "", f"  valid JSON, {len(obj)} fields: {text}")
            ok = True
        except json.JSONDecodeError as e:
            # This is the failure the ENC=1 encoding exists to prevent, and it
            # is worth shouting about: the datagram arrives either way.
            log(C["err"], "NOTIFICATION", f"from {addr[0]} — NOT VALID JSON ({e})")
            log(C["err"], "", f"  raw: {text}")
            obj, ok = None, False
        with LOCK:
            STATE["notifications"].append({"from": addr[0], "raw": text,
                                           "json": obj, "valid": ok,
                                           "at": stamp()})
        (OUT_DIR / "notifications.log").open("a").write(
            f"[{stamp()}] {addr[0]} {text}\n")


# ───────────────────────────── photo uploads ──────────────────────────────
class UploadHandler(http.server.BaseHTTPRequestHandler):
    server_version = "ScopusTestServer/1.0"

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(n) if n else b""
        name = pathlib.Path(self.headers.get("X-Filename") or "upload.bin").name
        dest = OUT_DIR / f"{datetime.datetime.now():%H%M%S}_{name}"
        dest.write_bytes(body)

        # A JPEG starts FFD8 and ends FFD9. Checking both is what turns "a POST
        # arrived" into "the photo arrived intact" — a truncated transfer still
        # POSTs happily.
        #
        # Trailing bytes after EOI are reported but do not make the image bad:
        # a decoder ignores them, and the kit's hardware codec used to emit up
        # to three (its DMA pads to a 32-bit word). Calling that "truncated"
        # was a false alarm this checker raised on a perfectly good photo.
        is_jpeg = body[:2] == b"\xff\xd8"
        eoi = body.rfind(b"\xff\xd9")
        complete = is_jpeg and eoi >= 0
        trailing = len(body) - eoi - 2 if eoi >= 0 else 0
        kind = (f"JPEG, complete{f', {trailing} trailing byte(s) after EOI' if trailing else ''}"
                if complete else
                "JPEG, TRUNCATED (no EOI marker)" if is_jpeg else
                f"not a JPEG (starts {body[:4].hex()})")

        log(C["up"] if complete else C["err"], "UPLOAD",
            f"from {self.client_address[0]}  {len(body)} bytes  -> {dest.name}")
        log(C["dim"], "", f"  {kind}   path={self.path}")
        for h in ("X-Filename", "X-Filesize", "X-Timestamp", "X-Ref", "X-Token",
                  "Content-Type"):
            if self.headers.get(h):
                log(C["dim"], "", f"  {h}: {self.headers.get(h)}")

        with LOCK:
            STATE["uploads"].append({"from": self.client_address[0],
                                     "bytes": len(body), "file": str(dest),
                                     "jpeg": complete,
                                     "at": stamp()})
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"OK")

    def do_GET(self):
        with LOCK:
            body = json.dumps({"ok": True, "now": stamp(),
                               "notifications": len(STATE["notifications"]),
                               "uploads": len(STATE["uploads"]),
                               "dir": str(OUT_DIR)}, indent=2).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass          # we do our own logging, in one stream with the UDP side


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--http-port", type=int, default=8080,
                    help="port for photo uploads (default 8080)")
    ap.add_argument("--udp-port", type=int, default=9999,
                    help="port for notifications (default 9999)")
    ap.add_argument("--dir", default="scopus-received",
                    help="where received files and logs are written")
    ap.add_argument("--from-modem", default=None, metavar="IP",
                    help="only accept datagrams from this address")
    ap.add_argument("--fresh", action="store_true",
                    help="start with an empty directory, keeping what an "
                         "earlier run left in <dir>-old-<timestamp>")
    args = ap.parse_args()

    global OUT_DIR
    OUT_DIR = pathlib.Path(args.dir).expanduser().resolve()

    # A manual test is judged partly by "what is in the directory afterwards",
    # and last week's photos sitting next to today's is how a test gets called
    # a pass on evidence it did not produce. Move them aside rather than
    # deleting: the previous run's files are the previous run's evidence.
    moved = None
    if args.fresh and OUT_DIR.is_dir() and any(OUT_DIR.iterdir()):
        moved = OUT_DIR.with_name(
            f"{OUT_DIR.name}-old-{datetime.datetime.now():%Y%m%d-%H%M%S}")
        OUT_DIR.rename(moved)
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"{C['b']}Scopus test server{C['0']}")
    print(f"  receiving into {OUT_DIR}")
    if moved:
        print(f"  (an earlier run's files were moved to {moved})")

    threading.Thread(target=udp_listener,
                     args=(args.udp_port, args.from_modem),
                     daemon=True).start()

    httpd = http.server.ThreadingHTTPServer(("0.0.0.0", args.http_port),
                                            UploadHandler)
    log(C["b"], "listening", f"HTTP 0.0.0.0:{args.http_port} (photo uploads)")
    print(f"{C['dim']}  Ctrl-C to stop. Point the modem here with:{C['0']}")
    print(f'    AT+SDVRNTFHOST="<this PC ip>"   AT+SDVRNTFPORT={args.udp_port}')
    print(f'    AT+SDVRHOSTIP="<this PC ip>"    AT+SDVRPORT={args.http_port}'
          f'   AT+SDVRSRVRPATH="/upload"\n')
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        with LOCK:
            n, u = len(STATE["notifications"]), len(STATE["uploads"])
            good_n = sum(1 for x in STATE["notifications"] if x["valid"])
            good_u = sum(1 for x in STATE["uploads"] if x["jpeg"])
        print(f"\n{C['b']}Summary{C['0']}")
        print(f"  notifications: {n} received, {good_n} valid JSON")
        print(f"  uploads:       {u} received, {good_u} complete JPEGs")
        print(f"  files in {OUT_DIR}")
        return 0 if (good_n == n and good_u == u) else 1


if __name__ == "__main__":
    sys.exit(main())
