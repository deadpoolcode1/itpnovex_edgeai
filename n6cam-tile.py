#!/usr/bin/env python3
"""
n6cam-tile.py — drive the N6Cam's tiled multi-crop detector over CDC.

The firmware runs the *unchanged* 256x256 yolov8n detector, but slices an
uploaded high-res frame into an overlapping grid of square tiles and runs the
NN per tile, remapping + NMS-merging detections back to full-frame space. This
recovers small/distant people that a single full-frame downscale loses.

This host tool:
  1. Loads an image, resizes it to the configured full-frame WxH (RGB888).
  2. Configures the grid (`tile frame/grid/crop/thresh`).
  3. Ensures the NN is running (`detect start`).
  4. Uploads the frame (`tile upload` + FRMI-framed payload, CRC32 checked).
  5. Runs `tile run` and prints the merged detections.

Usage:
    n6cam-tile.py <image> [/dev/ttyACMx]
                  [--frame WxH] [--grid CxR] [--crop PX]
                  [--conf 0..100] [--iou 0..100]

Defaults mirror the I.T.P. Novex 90-deg-FOV example: 2592x1944 frame,
5x4 grid, 576 px crops.
"""
import argparse
import os
import sys
import time
import zlib
from pathlib import Path

FRAME_MAGIC = b"FRMI"


class Port:
    """Minimal non-blocking serial wrapper (stdlib only, no pyserial)."""

    def __init__(self, path):
        if os.system(f"stty -F {path} 115200 cs8 -cstopb -parenb raw -echo") != 0:
            sys.exit(f"stty failed on {path}")
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    def write(self, data: bytes):
        n = 0
        while n < len(data):
            try:
                n += os.write(self.fd, data[n:])
            except BlockingIOError:
                time.sleep(0.003)

    def drain(self, quiet=0.4, hard=None):
        """Read until `quiet` seconds pass with no new bytes (or `hard` cap)."""
        buf = b""
        start = time.time()
        last = start
        while True:
            try:
                d = os.read(self.fd, 4096)
            except BlockingIOError:
                d = b""
            if d:
                buf += d
                last = time.time()
            else:
                time.sleep(0.01)
            now = time.time()
            if now - last >= quiet:
                break
            if hard and now - start >= hard:
                break
        return buf.decode(errors="replace")

    def cmd(self, line, quiet=0.4, hard=None, echo=True):
        self.write((line + "\r\n").encode())
        out = self.drain(quiet, hard)
        if echo:
            sys.stdout.write(out)
        return out

    def close(self):
        os.close(self.fd)


def parse_pair(s, sep, name):
    try:
        a, b = s.lower().split(sep)
        return int(a), int(b)
    except Exception:
        sys.exit(f"bad --{name} '{s}', expected e.g. 2592{sep}1944")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("tty", nargs="?", default="/dev/ttyACM0")
    ap.add_argument("--frame", default="2592x1944")
    ap.add_argument("--grid", default="5x4")
    ap.add_argument("--crop", type=int, default=576)
    ap.add_argument("--conf", type=int, default=45, help="confidence %% (0..100)")
    ap.add_argument("--iou", type=int, default=40, help="NMS IoU %% (0..100)")
    args = ap.parse_args()

    fw, fh = parse_pair(args.frame, "x", "frame")
    cols, rows = parse_pair(args.grid, "x", "grid")

    from PIL import Image  # type: ignore
    img = Image.open(args.image).convert("RGB").resize((fw, fh), Image.LANCZOS)
    payload = img.tobytes()
    assert len(payload) == fw * fh * 3, (len(payload), fw * fh * 3)
    crc = zlib.crc32(payload) & 0xFFFFFFFF

    print(f"Image  : {args.image} -> {fw}x{fh} RGB ({len(payload)} bytes, CRC 0x{crc:08x})")
    print(f"Tiling : grid {cols}x{rows}, crop {args.crop}px, conf>={args.conf/100:.2f}, iou={args.iou/100:.2f}")
    print(f"Port   : {args.tty}\n")

    p = Port(args.tty)
    try:
        p.drain(0.3)
        # Configure. detect start first so the NN is alive for `tile run`.
        p.cmd("detect start", 0.5)
        p.cmd(f"tile frame {fw} {fh}", 0.5)
        p.cmd(f"tile grid {cols} {rows}", 0.5)
        p.cmd(f"tile crop {args.crop}", 0.5)
        p.cmd(f"tile thresh {args.conf} {args.iou}", 0.5)

        # Upload the full frame. Drain stragglers, then wait (up to 3s) for the
        # 'Ready' prompt before streaming the framed payload.
        p.drain(0.3)
        p.write(b"tile upload\r\n")
        ready = ""
        t0 = time.time()
        while "Ready" not in ready and time.time() - t0 < 3.0:
            ready += p.drain(0.4)
        sys.stdout.write(ready)
        if "Ready" not in ready:
            sys.exit("device did not enter upload mode")
        hdr = FRAME_MAGIC + len(payload).to_bytes(4, "little") + crc.to_bytes(4, "little")
        p.write(hdr)
        p.write(payload)
        up = p.drain(1.0, hard=15)
        sys.stdout.write(up)
        if "ok" not in up:
            sys.exit("upload failed")

        # Run. The firmware is silent during compute (~100 ms/tile + resize)
        # then emits the whole report at once, so poll until we see the result
        # line ('run:') or the ack, with a generous hard cap.
        budget = cols * rows * 0.20 + 5.0
        print(f"\n--- tile run (waiting up to {budget:.0f}s) ---")
        p.write(b"tile run\r\n")
        out = ""
        t0 = time.time()
        while "tile run ok" not in out and time.time() - t0 < budget:
            out += p.drain(0.5)
        sys.stdout.write(out)
    finally:
        p.close()


if __name__ == "__main__":
    main()
