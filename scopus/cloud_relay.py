#!/usr/bin/env python3
"""Scopus cloud relay — the public half of the end-to-end chain.

`test_server.py` is the receiver you run on a PC sitting on the modem's USB
subnet. That only works while the cable is attached: over cellular the modem
gets a carrier-NAT address and can reach public addresses only, while the
tester's PC has no public address at all. Neither end can open a connection to
the other.

This is the piece in the middle. It runs on a host with a public IP, receives
exactly what `test_server.py` receives — notification datagrams on UDP, photo
POSTs on HTTP — and then lets the tester's PC *pull* them out over an ordinary
outbound HTTP request (`relay_pull.py`). The device pushes up, the PC pulls
down, and neither needs to accept an inbound connection.

    python3 cloud_relay.py --key <shared-secret>            # :9999/udp :8080/tcp
    python3 cloud_relay.py --key K --udp-port 39999 --http-port 38080

The device end is deliberately identical to `test_server.py`'s: same UDP
datagram, same POST, same X-* headers, same JPEG completeness check. A photo
that arrives here and a photo that arrives there are judged by the same code,
so "it worked on the cable" and "it worked on cellular" mean the same thing.

Read endpoints (`/events`, `/photo/<n>`, `/`) require the key; the device's
POST does not, because the modem cannot add an arbitrary query string beyond
what AT+SDVRSRVRPATH holds. Set --device-token to require the device to prove
itself with the X-Token header (AT+SDVRTOK) instead.
"""
import argparse
import datetime
import http.server
import json
import os
import pathlib
import re
import socket
import sys
import threading
import urllib.parse

MAX_UDP = 65535

# Bound on what a single POST may be. A Scopus photo is ~100 KB; anything
# larger on a public port is somebody else's traffic, and reading it into
# memory on a 512 MB droplet is how this process gets OOM-killed.
MAX_UPLOAD_BYTES = 8 * 1024 * 1024


class Store:
    """Every received thing, in arrival order, with a monotonic sequence.

    The sequence is what makes the pull side resumable: a client says "give me
    everything after 41" and gets exactly that, so a viewer can be restarted,
    or two of them can watch at once, without either missing or replaying.
    """

    def __init__(self, data_dir, max_photos):
        self.dir = pathlib.Path(data_dir)
        (self.dir / "photos").mkdir(parents=True, exist_ok=True)
        self.events = []
        self.seq = 0
        self.max_photos = max_photos
        self.cond = threading.Condition()
        self.log_path = self.dir / "events.jsonl"

    def add(self, kind, payload, blob=None):
        with self.cond:
            self.seq += 1
            seq = self.seq
            ev = {"seq": seq, "kind": kind,
                  "at": datetime.datetime.now(datetime.timezone.utc)
                          .strftime("%Y-%m-%dT%H:%M:%SZ"),
                  **payload}
            if blob is not None:
                path = self.dir / "photos" / f"{seq:06d}_{ev.get('name', 'photo.jpg')}"
                path.write_bytes(blob)
                ev["stored"] = path.name
            self.events.append(ev)
            with self.log_path.open("a") as fp:
                fp.write(json.dumps(ev) + "\n")
            self._evict_locked()
            self.cond.notify_all()
        return ev

    def _evict_locked(self):
        """Keep the newest max_photos images and 5000 events on this box.

        The relay is a test fixture on someone else's production droplet; it
        must not be the reason that droplet fills its disk. Events stay in
        events.jsonl regardless — only the in-memory window and the image
        files are bounded.
        """
        photos = sorted((self.dir / "photos").glob("*"))
        for stale in photos[:-self.max_photos] if len(photos) > self.max_photos else []:
            try:
                stale.unlink()
            except OSError:
                pass
        if len(self.events) > 5000:
            del self.events[:-5000]

    def since(self, seq, wait):
        with self.cond:
            if not any(e["seq"] > seq for e in self.events) and wait > 0:
                self.cond.wait(timeout=wait)
            return [e for e in self.events if e["seq"] > seq], self.seq

    def photo(self, seq):
        for name in os.listdir(self.dir / "photos"):
            if name.startswith(f"{seq:06d}_"):
                return (self.dir / "photos" / name).read_bytes(), name
        return None, None

    def counts(self):
        with self.cond:
            n = sum(1 for e in self.events if e["kind"] == "notification")
            u = sum(1 for e in self.events if e["kind"] == "upload")
        return n, u


def jpeg_verdict(body):
    """Same judgement `test_server.py` makes, so both ends agree on 'intact'.

    A truncated transfer still POSTs happily, so 'a POST arrived' is not the
    assertion worth making — 'it starts FFD8 and ends FFD9' is. Trailing bytes
    after EOI are reported but do not make the image bad: the kit's hardware
    codec pads to a 32-bit word, and calling that truncated was a false alarm
    raised on a perfectly good photo.
    """
    is_jpeg = body[:2] == b"\xff\xd8"
    eoi = body.rfind(b"\xff\xd9")
    complete = is_jpeg and eoi >= 0
    trailing = len(body) - eoi - 2 if eoi >= 0 else 0
    if complete:
        return True, f"JPEG, complete{f', {trailing} trailing byte(s) after EOI' if trailing else ''}"
    if is_jpeg:
        return False, "JPEG, TRUNCATED (no EOI marker)"
    return False, f"not a JPEG (starts {body[:4].hex()})"


def udp_listener(store, port, log):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    log(f"listening UDP 0.0.0.0:{port} (notifications)")
    while True:
        data, addr = sock.recvfrom(MAX_UDP)
        text = data.decode(errors="replace")
        try:
            obj, valid = json.loads(text), True
        except json.JSONDecodeError:
            obj, valid = None, False
        store.add("notification", {"from": addr[0], "raw": text,
                                   "json": obj, "valid": valid})
        log(f"NOTIFICATION from {addr[0]} "
            f"{'valid JSON' if valid else 'NOT VALID JSON'}: {text[:200]}")


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "ScopusCloudRelay/1.0"
    protocol_version = "HTTP/1.1"

    # set by main()
    store = None
    key = None
    device_token = None
    logf = staticmethod(lambda m: None)

    def _authed(self, query):
        return self.key is not None and query.get("key", [None])[0] == self.key

    def _peer(self):
        """Who actually sent this.

        The relay is reachable two ways: straight on its own port, and proxied
        (the cloud firewall in front of this droplet passes only 80/443, so the
        device's POST normally arrives via the reverse proxy). Proxied, the
        socket's peer is the proxy, and recording 127.0.0.1 for every photo
        would throw away the one field that says which device sent it.
        """
        fwd = self.headers.get("X-Forwarded-For")
        if fwd and self.client_address[0] in ("127.0.0.1", "::1"):
            return fwd.split(",")[0].strip()
        return self.client_address[0]

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # ── device side ──────────────────────────────────────────────────────
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        if n > MAX_UPLOAD_BYTES:
            self._json(413, {"error": "too large"})
            return
        body = self.rfile.read(n) if n else b""

        if self.device_token is not None and \
                self.headers.get("X-Token", "") != self.device_token:
            self.logf(f"UPLOAD REJECTED from {self._peer()}: bad X-Token")
            self._json(401, {"error": "bad token"})
            return

        name = pathlib.Path(self.headers.get("X-Filename") or "upload.bin").name
        ok, kind = jpeg_verdict(body)
        ev = self.store.add("upload", {
            "from": self._peer(), "name": name, "bytes": len(body),
            "jpeg": ok, "verdict": kind, "path": self.path,
            "headers": {h: self.headers.get(h) for h in
                        ("X-Filename", "X-Filesize", "X-Timestamp", "X-Ref",
                         "Content-Type") if self.headers.get(h)},
        }, blob=body)
        self.logf(f"UPLOAD from {self._peer()} {len(body)} bytes "
                  f"seq={ev['seq']} {kind}")

        # The device treats any 2xx as success and anything else as an upload
        # error, so answer plainly and always with a Content-Length: this is
        # HTTP/1.1 with keep-alive and curl will otherwise hang for the body.
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"OK")

    # ── viewer side ──────────────────────────────────────────────────────
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)

        if parsed.path == "/health":
            n, u = self.store.counts()
            self._json(200, {"ok": True, "notifications": n, "uploads": u,
                             "seq": self.store.seq})
            return

        if not self._authed(query):
            self._json(401, {"error": "key required"})
            return

        if parsed.path == "/events":
            since = int(query.get("since", ["0"])[0])
            wait = min(float(query.get("wait", ["0"])[0]), 60.0)
            evs, seq = self.store.since(since, wait)
            self._json(200, {"seq": seq, "events": evs})
            return

        m = re.fullmatch(r"/photo/(\d+)", parsed.path)
        if m:
            blob, name = self.store.photo(int(m.group(1)))
            if blob is None:
                self._json(404, {"error": "no such photo"})
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Disposition", f'inline; filename="{name}"')
            self.send_header("Content-Length", str(len(blob)))
            self.end_headers()
            self.wfile.write(blob)
            return

        if parsed.path == "/":
            n, u = self.store.counts()
            html = ("<!doctype html><meta charset=utf-8><title>Scopus relay</title>"
                    "<style>body{font:14px/1.5 system-ui;margin:2rem;max-width:60rem}"
                    "pre{background:#f4f4f4;padding:.5rem;overflow:auto}</style>"
                    f"<h1>Scopus cloud relay</h1><p>{n} notifications, {u} photos, "
                    f"seq {self.store.seq}.</p><pre id=o>loading…</pre><script>"
                    "let s=0;const k=new URLSearchParams(location.search).get('key');"
                    "async function p(){const r=await fetch(`/events?since=${s}&wait=25&key=${k}`);"
                    "const j=await r.json();s=j.seq;for(const e of j.events){"
                    "o.textContent+=`\\n[${e.at}] ${e.kind} ${e.kind=='upload'"
                    "?`${e.bytes}B ${e.verdict} <a>/photo/${e.seq}</a>`:e.raw}`}p()}"
                    "o.textContent='';p();</script>")
            body = html.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self._json(404, {"error": "not found"})

    def log_message(self, *a):
        pass          # the relay does its own logging, in one stream


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--key", required=True,
                    help="shared secret the pull client must present")
    ap.add_argument("--http-port", type=int, default=8080)
    ap.add_argument("--udp-port", type=int, default=9999)
    ap.add_argument("--dir", default="scopus-relay-data")
    ap.add_argument("--device-token", default=None,
                    help="require this value in the device's X-Token header "
                         "(set on the device with AT+SDVRTOK)")
    ap.add_argument("--max-photos", type=int, default=500)
    args = ap.parse_args()

    def log(msg):
        print(f"[{datetime.datetime.now():%Y-%m-%d %H:%M:%S}] {msg}", flush=True)

    store = Store(args.dir, args.max_photos)
    Handler.store = store
    Handler.key = args.key
    Handler.device_token = args.device_token
    Handler.logf = staticmethod(log)

    threading.Thread(target=udp_listener, args=(store, args.udp_port, log),
                     daemon=True).start()

    httpd = http.server.ThreadingHTTPServer(("0.0.0.0", args.http_port), Handler)
    log(f"listening HTTP 0.0.0.0:{args.http_port} (photo uploads + pull API)")
    log(f"data in {pathlib.Path(args.dir).resolve()}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
