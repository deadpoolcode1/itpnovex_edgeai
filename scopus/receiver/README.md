# The receiver on the server side

This is the copy, under version control, of what runs on ITP's machine at
`213.8.185.180` in `/opt/sdvr-server/` and `/etc/systemd/system/`. It is not
started by the test suites — `../test_server.py` is the throwaway receiver they
stand up on the bench. This one is the real endpoint the unit uploads to.

## What it does that the previous version did not

**One port, both schemes.** `:8991` answers `http://` and `https://` on the same
listener and decides per connection, by peeking at the first byte: a TLS
ClientHello opens with the record type `0x16`, an HTTP request with an ASCII
method letter.

That is the answer to ScopusQA #19. The device chooses its own scheme —
`AT+SDVRCERTIMPORT` moves the upload *and* notification legs to https and
persists it, `AT+SDVRCERTDEL` moves them back — and a receiver that speaks only
one of the two turns that into a broken link in whichever direction is wrong:

| server | device | what happens |
|---|---|---|
| plain HTTP | https | `curl error 35 (SSL connect error)`, nothing arrives |
| TLS only | http | `ssl.SSLEOFError` in the journal, nothing is stored |

Both were seen on this bench. Nothing has to be reconfigured on the server when
QA imports or deletes a certificate now.

## Files

| File | Installed as |
|---|---|
| `server.py` | `/opt/sdvr-server/server.py` |
| `sdvr-receiver.service` | `/etc/systemd/system/sdvr-receiver.service` |
| `sdvr-http.service`, `sdvr-https.service` | compat shims, same directory |

The two old names were two *servers*, and they both wanted `:8991` — whichever
lost the race exited with `EADDRINUSE` and systemd restarted it forever (900
times by 2026-08-30, which is what "plain HTTP does not work" looked like from
the outside). They are now one-line shims that pull in `sdvr-receiver.service`,
so `systemctl enable --now sdvr-http` from the older QA notes still does the
right thing.

## Install

```bash
sudo install -m 755 server.py /opt/sdvr-server/server.py
sudo install -m 644 sdvr-*.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now sdvr-receiver
```

## Check it

```bash
curl -s http://127.0.0.1:8991/health
curl -s --cacert /opt/sdvr-server/certs/ca.crt \
        --cert   /opt/sdvr-server/certs/client.crt \
        --key    /opt/sdvr-server/certs/client.key \
        https://127.0.0.1:8991/health
```

Both answer JSON; `accepts` lists what the listener will take and `scheme` is
how *that* request arrived. Uploads and notifications are logged to
`/home/user/sdvr-uploads/_uploads.log`, one line each, and the line now names
the scheme:

```
POST /upload name=4194336_….rdy bytes=122049 … scheme=http
POST /upload name=4194336_….rdy bytes=121097 … scheme=https tls=TLSv1.2 client_cn="…sdvr-device-client"
```

A failed TLS handshake — a plain probe, or a client with no certificate where
one is required — is one line in the same file rather than a traceback in the
journal.

## Knobs

Set in the unit file, read from the environment:

| Variable | Effect |
|---|---|
| `SDVR_PORT` | listen port (8991) |
| `SDVR_UPLOAD_DIR` | where files land |
| `SDVR_TLS_CERT` / `_KEY` | server keypair — enables the https half |
| `SDVR_TLS_CA` | CA bundle for client certs — enables mTLS |
| `SDVR_TLS_CLIENT_REQUIRED` | `1` = a TLS client must present a valid cert |
| `SDVR_ALLOW_PLAIN` | `0` = refuse plain HTTP (a TLS-only deployment) |

Dropping the four TLS lines leaves a plain-HTTP receiver; setting
`SDVR_ALLOW_PLAIN=0` leaves a TLS-only one. The default is both.
