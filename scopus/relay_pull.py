#!/usr/bin/env python3
"""Watch a Scopus cloud relay from a PC that nothing can connect to.

When the modem is on cellular it can reach a public address but nothing can
reach *it*, and the tester's PC — behind office NAT, on a hotspot, wherever —
has no public address either. `cloud_relay.py` sits in the middle and holds
what the device sent; this pulls it down over an ordinary outbound HTTP
request, so the PC needs no port forwarding, no VPN, and no fixed address.

    python3 scopus/relay_pull.py --relay http://165.22.181.245:38080 --key K

The output and the receiving directory are deliberately the same as
`test_server.py`'s. Anything the tester manual says about reading the log or
opening the received JPEG is true whether the test ran over the cable or over
cellular, and a photo is judged by the same FFD8/FFD9 check either way.

It long-polls (`wait=25`), so an event appears within a second of arriving
rather than on the next poll tick, and a dropped connection costs one retry
rather than a lost event: the relay replays anything after the last sequence
number we acknowledged, which is remembered in <dir>/.relay-seq across runs.
"""
import argparse
import datetime
import json
import pathlib
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

C = {"ntf": "\033[95m", "up": "\033[92m", "err": "\033[91m",
     "dim": "\033[96m", "b": "\033[1m", "0": "\033[0m"}


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S")


def log(colour, tag, msg):
    print(f"{C['dim']}[{stamp()}]{C['0']} {colour}{tag:<14}{C['0']} {msg}",
          flush=True)


def get(url, timeout):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read()


def show_notification(ev, out_dir):
    text = ev.get("raw", "")
    if ev.get("valid"):
        obj = ev.get("json") or {}
        pretty = " ".join(f"{k}={obj[k]!r}" for k in
                          ("ser", "num", "rsn", "rsd", "tim", "mtn") if k in obj)
        log(C["ntf"], "NOTIFICATION", f"from {ev['from']}  {pretty}")
        log(C["dim"], "", f"  valid JSON, {len(obj)} fields: {text}")
    else:
        # The failure the ENC=1 encoding exists to prevent. The datagram
        # arrives either way, so this is worth shouting about rather than
        # counting as a receive.
        log(C["err"], "NOTIFICATION", f"from {ev['from']} — NOT VALID JSON")
        log(C["err"], "", f"  raw: {text}")
    (out_dir / "notifications.log").open("a").write(
        f"[{stamp()}] {ev['from']} {text}\n")


def show_upload(ev, out_dir, relay, key, timeout):
    ok = ev.get("jpeg")
    log(C["up"] if ok else C["err"], "UPLOAD",
        f"from {ev['from']}  {ev['bytes']} bytes  seq={ev['seq']}")
    log(C["dim"], "", f"  {ev.get('verdict', '')}   path={ev.get('path', '')}")
    for h, v in (ev.get("headers") or {}).items():
        log(C["dim"], "", f"  {h}: {v}")

    # Pull the bytes down too, not just the verdict: the point of the manual's
    # last step is that a person opens the file and sees the room.
    url = f"{relay}/photo/{ev['seq']}?key={urllib.parse.quote(key)}"
    try:
        blob = get(url, timeout)
    except urllib.error.URLError as e:
        log(C["err"], "", f"  could not fetch the image: {e}")
        return
    dest = out_dir / f"{ev['at'].replace(':', '').replace('-', '')[-7:]}_{ev['name']}"
    dest.write_bytes(blob)
    log(C["dim"], "", f"  saved {len(blob)} bytes -> {dest.name}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--relay", required=True,
                    help="base URL of the relay, e.g. http://165.22.181.245:38080")
    ap.add_argument("--key", required=True, help="the relay's shared secret")
    ap.add_argument("--dir", default="scopus-received",
                    help="where received files and logs are written")
    ap.add_argument("--from-start", action="store_true",
                    help="replay everything the relay still holds, ignoring "
                         "the remembered position")
    ap.add_argument("--fresh", action="store_true",
                    help="start with an empty directory, keeping what an "
                         "earlier run left in <dir>-old-<timestamp>")
    ap.add_argument("--timeout", type=float, default=40.0)
    args = ap.parse_args()

    relay = args.relay.rstrip("/")
    out_dir = pathlib.Path(args.dir).expanduser().resolve()

    # Same reasoning as test_server.py: last week's photos sitting next to
    # today's is how a test gets called a pass on evidence it did not produce.
    moved = None
    if args.fresh and out_dir.is_dir() and any(out_dir.iterdir()):
        moved = out_dir.with_name(
            f"{out_dir.name}-old-{datetime.datetime.now():%Y%m%d-%H%M%S}")
        out_dir.rename(moved)
    out_dir.mkdir(parents=True, exist_ok=True)

    seq_file = out_dir / ".relay-seq"
    since = 0
    if not args.from_start and seq_file.exists():
        try:
            since = int(seq_file.read_text().strip())
        except ValueError:
            since = 0

    print(f"{C['b']}Scopus relay viewer{C['0']}")
    print(f"  relay    {relay}")
    print(f"  receiving into {out_dir}")
    if moved:
        print(f"  (an earlier run's files were moved to {moved})")
    try:
        health = json.loads(get(f"{relay}/health", 15))
    except urllib.error.URLError as e:
        print(f"{C['err']}Cannot reach the relay: {e}{C['0']}\n"
              f"  Check the address, and that the relay is running "
              f"(ssh to it and `systemctl status scopus-relay`).", file=sys.stderr)
        return 2
    print(f"  relay holds {health['notifications']} notifications, "
          f"{health['uploads']} photos (seq {health['seq']})")
    print(f"  watching from seq {since}. Ctrl-C to stop.\n")

    backoff = 1.0
    while True:
        url = (f"{relay}/events?since={since}&wait=25"
               f"&key={urllib.parse.quote(args.key)}")
        try:
            payload = json.loads(get(url, args.timeout))
            backoff = 1.0
        except urllib.error.HTTPError as e:
            if e.code == 401:
                print(f"{C['err']}The relay rejected the key.{C['0']}",
                      file=sys.stderr)
                return 2
            log(C["err"], "relay", f"HTTP {e.code}; retrying")
            time.sleep(backoff)
            backoff = min(backoff * 2, 30)
            continue
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as e:
            # A long poll that times out mid-flight is normal, not a failure;
            # the sequence number means nothing is lost by simply asking again.
            log(C["err"], "relay", f"{e}; retrying in {backoff:.0f}s")
            time.sleep(backoff)
            backoff = min(backoff * 2, 30)
            continue

        for ev in payload.get("events", []):
            if ev["kind"] == "notification":
                show_notification(ev, out_dir)
            elif ev["kind"] == "upload":
                show_upload(ev, out_dir, relay, args.key, args.timeout)
            since = ev["seq"]
            seq_file.write_text(str(since))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nstopped")
        sys.exit(130)
