#!/usr/bin/env python3
"""SDVR upload/notification receiver — POST endpoint matching upload_file.c.

ONE port answers BOTH plain HTTP and HTTPS (ScopusQA #19).
---------------------------------------------------------
The device picks its own scheme: importing a certificate from SD flips the
upload *and* notification legs to https and persists that, and AT+SDVRCERTDEL
flips them back.  A receiver that speaks only one of the two turns that device
side setting into a broken link, in whichever direction it is wrong:

  server plain, device https  ->  curl error 35 (SSL connect error)
  server TLS,   device http   ->  ssl.SSLEOFError, nothing is stored

Both were seen on this bench.  Running two services instead does not help —
they both want :8991 and the second one dies with EADDRINUSE, restarting
forever (900 times, on 2026-08-30).

So the listener decides per connection.  A TLS ClientHello starts with the
record type byte 0x16; every HTTP request starts with an ASCII method letter.
One byte of MSG_PEEK tells the two apart before a single byte is consumed, and
the connection is wrapped in TLS only when the client actually asked for TLS.

Nothing has to be reconfigured when QA imports or deletes a certificate.

Env:
  SDVR_PORT                 listen port (default 8991)
  SDVR_UPLOAD_DIR           where files land (default /home/user/sdvr-uploads)
  SDVR_TLS_CERT             server cert (PEM) -> enables the HTTPS half
  SDVR_TLS_KEY              server private key (PEM)
  SDVR_TLS_CA               CA bundle to verify client certs (enables mTLS)
  SDVR_TLS_CLIENT_REQUIRED  "1" = TLS clients must present a valid cert
  SDVR_ALLOW_PLAIN          "0" = refuse plain HTTP (TLS-only deployment)
"""
import http.server, os, socket, ssl, datetime, json, pathlib

UPLOAD_DIR = pathlib.Path(os.environ.get("SDVR_UPLOAD_DIR", "/home/user/sdvr-uploads"))
PORT       = int(os.environ.get("SDVR_PORT", "8991"))
TLS_CERT   = os.environ.get("SDVR_TLS_CERT")
TLS_KEY    = os.environ.get("SDVR_TLS_KEY")
TLS_CA     = os.environ.get("SDVR_TLS_CA")
CLIENT_REQ = os.environ.get("SDVR_TLS_CLIENT_REQUIRED", "0") == "1"
ALLOW_PLAIN = os.environ.get("SDVR_ALLOW_PLAIN", "1") != "0"
UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
META_LOG = UPLOAD_DIR / "_uploads.log"


def stamp():
    return datetime.datetime.now().isoformat(timespec="seconds")


def log_line(msg):
    """Single writer for both the journal and _uploads.log, so a rejected
    handshake is as visible to QA as a stored file."""
    line = f"[{stamp()}] {msg}"
    print(line, flush=True)
    with META_LOG.open("a") as f:
        f.write(line + "\n")


class DualSchemeHTTPServer(http.server.ThreadingHTTPServer):
    """ThreadingHTTPServer that sniffs http-vs-TLS per connection."""

    daemon_threads = True
    ssl_ctx = None            # set in __main__ when a keypair is configured
    handshake_timeout = 15.0  # cap on how long a silent client holds a thread

    def process_request_thread(self, request, client_address):
        # Deliberately here and not in get_request(): the peek below blocks
        # until the client sends its first byte, and the accept loop is shared,
        # so one silent connection there would stall every other upload.
        try:
            request = self._negotiate(request, client_address)
        except Exception as exc:
            # A failed handshake is a normal event on a public port (a plain
            # probe, a client with no cert). Log one line, not a traceback.
            log_line(f"{client_address[0]}:{client_address[1]} handshake rejected: "
                     f"{type(exc).__name__}: {exc}")
            self.shutdown_request(request)
            return
        if request is None:
            return
        super().process_request_thread(request, client_address)

    def _negotiate(self, sock, client_address):
        """Return the socket to serve — TLS-wrapped or not — or None if closed."""
        sock.settimeout(self.handshake_timeout)
        first = sock.recv(1, socket.MSG_PEEK)
        if not first:                       # connected and hung up (port scan)
            self.shutdown_request(sock)
            return None

        if first[0] == 0x16:                # TLS record type 22 = handshake
            if self.ssl_ctx is None:
                log_line(f"{client_address[0]}:{client_address[1]} TLS refused: "
                         f"no server keypair configured")
                self.shutdown_request(sock)
                return None
            sock = self.ssl_ctx.wrap_socket(sock, server_side=True)
        elif not ALLOW_PLAIN:
            log_line(f"{client_address[0]}:{client_address[1]} plain HTTP refused "
                     f"(SDVR_ALLOW_PLAIN=0)")
            self.shutdown_request(sock)
            return None

        sock.settimeout(None)               # back to blocking for the request
        return sock


class H(http.server.BaseHTTPRequestHandler):
    server_version = "SDVRReceiver/2.1"

    def _scheme(self):
        return "https" if isinstance(self.connection, ssl.SSLSocket) else "http"

    def _peer_tls(self):
        """Short description of the TLS session, or '' for plain HTTP."""
        sock = self.connection
        if not isinstance(sock, ssl.SSLSocket):
            return ""
        try:
            ver = sock.version()
            cert = sock.getpeercert() or {}
            subj = "/".join("=".join(x) for rdn in cert.get("subject", ()) for x in rdn)
            return f" tls={ver} client_cn=\"{subj or '-'}\""
        except Exception:
            return ""

    def _log(self, msg):
        log_line(f"{self.client_address[0]}:{self.client_address[1]} {msg} "
                 f"scheme={self._scheme()}{self._peer_tls()}")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        name = self.headers.get("X-Filename") or "upload.bin"
        size_hdr = self.headers.get("X-Filesize") or str(n)
        token = self.headers.get("X-Token", "")
        req_type = (self.headers.get("req_type")
                    or self.headers.get("Req-Type")
                    or self.headers.get("X-Req-Type")
                    or "file")
        safe = pathlib.Path(name).name  # no path traversal
        dest = UPLOAD_DIR / safe
        data = self.rfile.read(n) if n else b""
        with dest.open("wb") as f:
            f.write(data)
        self._log(f"POST {self.path} name={safe} bytes={len(data)} hdr_size={size_hdr} "
                  f"token=\"{token}\" type={req_type}")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"OK")

    def do_GET(self):
        # Health probe. "scheme" is this connection; "accepts" is what the
        # listener as a whole will answer, which is the #19 question.
        accepts = [s for s, on in (("http", ALLOW_PLAIN), ("https", bool(TLS_CERT))) if on]
        body = json.dumps({"ok": True, "now": stamp(), "dir": str(UPLOAD_DIR),
                           "scheme": self._scheme(), "accepts": accepts,
                           "tls": bool(TLS_CERT)}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass  # we log in _log


if __name__ == "__main__":
    srv = DualSchemeHTTPServer(("0.0.0.0", PORT), H)
    if TLS_CERT:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certfile=TLS_CERT, keyfile=TLS_KEY)
        if TLS_CA:
            ctx.load_verify_locations(cafile=TLS_CA)
            ctx.verify_mode = ssl.CERT_REQUIRED if CLIENT_REQ else ssl.CERT_OPTIONAL
        # The context is kept on the server, not applied to the listening
        # socket: wrapping happens per connection, after the sniff.
        srv.ssl_ctx = ctx
        tls_desc = ("mutual-TLS (client cert required)" if (TLS_CA and CLIENT_REQ)
                    else "server-TLS")
    else:
        tls_desc = "no TLS keypair — HTTPS clients will be refused"
    modes = "+".join([s for s, on in (("http", ALLOW_PLAIN), ("https", bool(TLS_CERT))) if on]) or "nothing"
    print(f"[{stamp()}] SDVR receiver on 0.0.0.0:{PORT} accepts {modes} "
          f"[{tls_desc}] -> {UPLOAD_DIR}", flush=True)
    srv.serve_forever()
