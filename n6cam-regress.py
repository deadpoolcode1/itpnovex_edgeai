#!/usr/bin/env python3
"""
n6cam-regress.py — regression-test the N6Cam's detection pipeline against
a folder of images, without depending on the camera optics.

For each image:
  1. Resize to 192x192 and convert to RGB888.
  2. Upload to the kit over CDC (FRMI framed, CRC32 validated).
  3. Issue `frame run` on the shell.
  4. Parse the kit's reply for detection count and per-box (class, conf, bbox).
  5. Record wall-clock latency.

Prints a per-image summary + an aggregate by class. Useful for:
- comparing firmware versions (does W6 vehicle detection light up?)
- comparing different test sets (real photos vs synthetic)
- benchmarking inference throughput at the shell layer.

Usage:
    n6cam-regress.py <images_dir> [/dev/ttyACMx]

Detection class codes (COCO mapping, used by our class filter):
    0  person     (the current PeopleNet model only outputs this)
    2  car
    3  motorcycle
    5  bus
    7  truck
"""
import argparse
import os
import re
import struct
import sys
import time
import zlib
from pathlib import Path

FRAME_W = 192
FRAME_H = 192
FRAME_BYTES = FRAME_W * FRAME_H * 3
EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".webp"}
CLASS_NAMES = {0: "person", 2: "car", 3: "motorcycle", 5: "bus", 7: "truck"}

# Regex parsers for the kit's shell replies
RE_RUN_COUNT = re.compile(r"frame run:\s+(\d+)\s+detection")
RE_NN_TIME   = re.compile(r"NN\s+([\d.]+)\s*ms")
RE_BOX = re.compile(
    r"\[(\d+)\]\s+class=(-?\d+)\s+conf=([\d.]+)\s+bbox=\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\)"
)


import fcntl


class Port:
    """Minimal non-blocking serial wrapper."""

    def __init__(self, path: str):
        rc = os.system(f"stty -F {path} 115200 cs8 -cstopb -parenb raw -echo")
        if rc != 0:
            print(f"stty failed on {path}")
            sys.exit(1)
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    def write(self, data: bytes):
        # Loop because non-blocking write may return short
        n = 0
        while n < len(data):
            try:
                n += os.write(self.fd, data[n:])
            except BlockingIOError:
                time.sleep(0.005)

    def read_chunk(self, n=4096) -> bytes:
        try:
            return os.read(self.fd, n)
        except BlockingIOError:
            return b""

    def close(self):
        os.close(self.fd)


def open_tty(tty: str) -> Port:
    return Port(tty)


def drain(port: Port, secs=0.2):
    end = time.time() + secs
    while time.time() < end:
        port.read_chunk(4096)
        time.sleep(0.02)


def read_until(port: Port, sentinel: bytes, max_secs=5.0):
    buf = b""
    end = time.time() + max_secs
    while time.time() < end:
        chunk = port.read_chunk(4096)
        if chunk:
            buf += chunk
            if sentinel in buf:
                break
        else:
            time.sleep(0.02)
    return buf


def send_line(port: Port, line: str):
    port.write(line.encode() + b"\n")


def load_image_as_rgb888(path: Path) -> bytes:
    from PIL import Image  # type: ignore
    img = Image.open(path).convert("RGB").resize((FRAME_W, FRAME_H), Image.LANCZOS)
    return img.tobytes()


def upload_frame(port: Port, data: bytes):
    crc = zlib.crc32(data) & 0xFFFFFFFF
    send_line(port, "frame upload")
    time.sleep(0.4)
    port.write(b"FRMI" + struct.pack("<II", len(data), crc))
    # chunk payload
    for i in range(0, len(data), 4096):
        port.write(data[i:i + 4096])
    # wait for ack
    reply = read_until(port, b"frame upload ok", max_secs=4.0).decode(errors="replace")
    if "frame upload ok" not in reply:
        return False, reply
    return True, reply


def run_inference(port):
    drain(port, 0.1)
    t0 = time.perf_counter()
    send_line(port, "frame run")
    reply = read_until(port, b"frame run ok", max_secs=4.0).decode(errors="replace")
    dt = (time.perf_counter() - t0) * 1000.0
    m = RE_RUN_COUNT.search(reply)
    count = int(m.group(1)) if m else -1
    boxes = []
    for bm in RE_BOX.finditer(reply):
        boxes.append({
            "idx": int(bm.group(1)),
            "class": int(bm.group(2)),
            "conf": float(bm.group(3)),
            "bbox": tuple(float(bm.group(i)) for i in (4, 5, 6, 7)),
        })
    nn_m = RE_NN_TIME.search(reply)
    nn_ms = float(nn_m.group(1)) if nn_m else None
    return count, boxes, dt, nn_ms, reply


def main() -> int:
    ap = argparse.ArgumentParser(description="N6Cam detection regression test")
    ap.add_argument("images_dir", help="Folder of test images")
    ap.add_argument("tty", nargs="?", default="/dev/ttyACM1")
    args = ap.parse_args()

    img_dir = Path(args.images_dir)
    sources = sorted(p for p in img_dir.rglob("*")
                     if p.is_file() and p.suffix.lower() in EXTS)
    if not sources:
        print(f"No images found under {img_dir}")
        return 1

    port = open_tty(args.tty)
    # Ensure detection is running
    drain(port, 0.3)
    send_line(port, "detect start")
    time.sleep(0.2)
    drain(port, 0.3)

    print(f"\nTesting {len(sources)} image(s) against {args.tty}")
    print("=" * 78)
    print(f"{'image':40s} {'boxes':>6s} {'classes':>10s} {'wall':>8s} {'NN':>8s}")
    print("-" * 78)

    totals = {"images": 0, "detections": 0, "by_class": {}}
    for src in sources:
        try:
            data = load_image_as_rgb888(src)
        except Exception as e:
            print(f"{src.name:40s}  SKIP  ({e})")
            continue
        ok, _ = upload_frame(port, data)
        if not ok:
            print(f"{src.name:40s}  UPLOAD-FAIL")
            continue
        count, boxes, lat, nn_ms, raw = run_inference(port)
        per_cls = {}
        for b in boxes:
            per_cls[b['class']] = per_cls.get(b['class'], 0) + 1
        cls_summary = ",".join(
            f"{CLASS_NAMES.get(c, str(c))}={n}"
            for c, n in sorted(per_cls.items())
        ) or "-"
        nn_str = f"{nn_ms:>6.1f}ms" if nn_ms is not None else "       -"
        print(f"{src.name:40s}  {count:>4d}  {cls_summary:>10s}  {lat:>6.1f}ms  {nn_str}")
        totals["images"] += 1
        totals["detections"] += max(count, 0)
        for b in boxes:
            totals["by_class"][b["class"]] = totals["by_class"].get(b["class"], 0) + 1

    print("-" * 78)
    print(f"Total: {totals['images']} images, {totals['detections']} detection(s)")
    if totals["by_class"]:
        for c, n in sorted(totals["by_class"].items()):
            print(f"  class {c} ({CLASS_NAMES.get(c, '?')}): {n}")

    # Clean up the override so live camera is back
    send_line(port, "frame clear")
    drain(port, 0.3)
    port.close()
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
