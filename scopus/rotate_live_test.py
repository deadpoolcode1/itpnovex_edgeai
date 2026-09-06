#!/usr/bin/env python3
"""What the LIVE detector does with a picture, per `detect rotate` setting.

ScopusQA #26 was closed on a measurement and reopened on a live test, and the
two could not be compared: the bench measurement ran the offline sweep
(`tile run`, which reports to the console), QA ran the product (the live sweep,
which reports counts and notifications), and nobody could run the other's test
because one needs a file and the other needs a person lying on the ground in
front of a lens.

This runs the PRODUCT's path over a file. `tile inject` puts the uploaded frame
in front of the live sweep, so the counts, the debounce and the §4.2
notifications are the real ones, the only thing standing in is the lens.

    python3 scopus/rotate_live_test.py images_lying/1_person_lying_omer_park.png
    python3 scopus/rotate_live_test.py <image> --modes off,auto --sweeps 6

For each mode it reports what the detector counted, how long a sweep took, how
many turned second looks it spent, and whether a §4.2 notification came out and
after how long. Every setting it changes is put back at the end.
"""
from __future__ import annotations
import argparse
import re
import struct
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, "scopus")
sys.path.insert(0, "scopus/lib")
from run_integration_tests import Camera                          # noqa: E402

LIVE_W, LIVE_H = 800, 600

# §4.2 bit 4 (0x10) is the people event, which is what a person on the ground
# has to raise. Anything else in the mask would only add noise to the read.
NOTIFY_PEOPLE = 0x10


def upload(cam, image_path):
    from PIL import Image
    cam.n6.drain(0.3)
    banner = cam.n6.send("tile upload", "Ready", 5.0)
    if "Ready" not in banner:
        return False, f"no upload banner: {banner[-120:]!r}"
    data = Image.open(image_path).convert("RGB").resize((LIVE_W, LIVE_H),
                                                        Image.LANCZOS).tobytes()
    cam.n6._write(b"FRMI" + struct.pack("<II", len(data),
                                        zlib.crc32(data) & 0xFFFFFFFF))
    for i in range(0, len(data), 1024):
        cam.n6._write(data[i:i + 1024])
    out = cam._read_until((b"ok", b"ERROR"), 20.0)
    return ("ERROR" not in out), out.strip().replace("\n", " ")[-120:]


def stats(cam):
    """`detect stats`, what the live detector counts right now, and what the
    last sweep cost."""
    out = cam.n6.send("detect stats", "detect stats ok", 8.0)
    m = re.search(r"counting now: people=(\d+) vehicles=(\d+)", out)
    people, vehicles = (int(m.group(1)), int(m.group(2))) if m else (None, None)
    m = re.search(r"last sweep: (\d+) step\(s\), (\d+) ms", out)
    steps, ms = (int(m.group(1)), int(m.group(2))) if m else (None, None)
    m = re.search(r"(\d+) turned second look\(s\) of (\d+) asked", out)
    looks, asked = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
    return people, vehicles, steps, ms, looks, asked


def watch(cam, seconds):
    """Read the console for `seconds`, returning the §4.2 notifications seen
    and when each arrived.

    The camera pushes these unprompted, so this reads the port directly rather
    than sending anything: `N6Shell.drain()` throws away what it reads, which
    is the opposite of what is wanted here.
    """
    import os
    t0 = time.time()
    seen, buf = [], b""
    while time.time() - t0 < seconds:
        try:
            chunk = os.read(cam.n6.fd, 4096)
        except BlockingIOError:
            chunk = b""
        if not chunk:
            time.sleep(0.05)
            continue
        buf += chunk
        *lines, buf = buf.split(b"\n")
        for raw in lines:
            line = raw.decode("utf-8", "replace").strip()
            # `+SDVRNTF: END,<n>` is the terminator the modem leg prints after
            # a message, not a message. Counting those as notifications says a
            # scene raised an event when it did not, which is the exact
            # confusion this tool exists to settle.
            if "+SDVRNTF" in line and "{" in line:
                seen.append((round(time.time() - t0, 1), line))
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--modes", default="off,full,auto,all")
    ap.add_argument("--sweeps", type=int, default=5,
                    help="how many sweeps to let each mode run (default 5)")
    args = ap.parse_args()

    modes = [m.strip() for m in args.modes.split(",") if m.strip()]
    cam = Camera()
    rows = []
    was = {}
    try:
        # ── remember what we are about to change ──────────────────────────
        was["mode"] = "tile" if "detect mode: tile" in cam.n6.send(
            "detect mode query", "detect mode ok", 6.0) else "default"
        m = re.search(r"detect rotate: (\w+)",
                      cam.n6.send("detect rotate query", "detect rotate ok", 6.0))
        was["rotate"] = m.group(1) if m else "auto"
        m = re.search(r"enable_mask=0x([0-9A-Fa-f]+)",
                      cam.n6.send("notify query", "notify query ok", 6.0))
        was["notify"] = int(m.group(1), 16) if m else 0

        # ── arm the live path over the picture ────────────────────────────
        cam.n6.send("detect start", "detect", 6.0)
        cam.n6.send("detect mode tile", "detect mode", 8.0)
        cam.n6.send(f"tile frame {LIVE_W} {LIVE_H}", "tile", 6.0)
        ok, detail = upload(cam, args.image)
        if not ok:
            print("upload failed:", detail)
            return 1
        print(cam.n6.send("tile inject on", "tile inject", 6.0).strip())
        cam.n6.send(f"notify enable 0x{was['notify'] | NOTIFY_PEOPLE:02X}",
                    "notify enable", 6.0)

        for mode in modes:
            cam.n6.send(f"detect rotate {mode}", "detect rotate", 6.0)
            # A rotate change takes effect on the next sweep, and the debounce
            # is two sweeps, so give it the sweeps it needs before reading.
            _, _, _, ms, _, _ = stats(cam)
            budget = max(2.0, (ms or 1600) / 1000.0) * args.sweeps
            notes = watch(cam, budget)
            people, vehicles, steps, ms, looks, asked = stats(cam)
            rows.append((mode, people, vehicles, steps, ms, looks, asked,
                         notes))
            print(f"  {mode:5s} people={people} vehicles={vehicles} "
                  f"steps={steps} sweep={ms}ms looks={looks}/{asked} "
                  f"notifications={len(notes)}")
    finally:
        cam.n6.send("tile inject off", "tile inject", 6.0)
        cam.n6.send("tile clear", "tile", 6.0)
        cam.n6.send(f"detect rotate {was.get('rotate', 'auto')}",
                    "detect rotate", 6.0)
        cam.n6.send(f"detect mode {was.get('mode', 'tile')}", "detect mode", 8.0)
        cam.n6.send(f"notify enable 0x{was.get('notify', 0):02X}",
                    "notify enable", 6.0)
        cam.close()

    print(f"\n{Path(args.image).name}\n")
    print(f"{'rotate':8s} {'people':>7s} {'vehicles':>9s} {'steps':>6s} "
          f"{'sweep':>7s} {'2nd looks':>10s} {'notifications':>14s}")
    for mode, people, vehicles, steps, ms, looks, asked, notes in rows:
        first = f"{notes[0][0]}s" if notes else "none"
        print(f"{mode:8s} {str(people):>7s} {str(vehicles):>9s} "
              f"{str(steps):>6s} {str(ms) + 'ms':>7s} "
              f"{f'{looks} of {asked}':>10s} {f'{len(notes)}, 1st {first}':>14s}")
    for mode, *_rest, notes in rows:
        for at, line in notes:
            print(f"  {mode:5s} +{at}s  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
