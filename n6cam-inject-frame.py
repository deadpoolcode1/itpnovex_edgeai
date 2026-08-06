#!/usr/bin/env python3
"""
n6cam-inject-frame.py — push a test image into the N6Cam's NN input
buffer over CDC so the detection algorithm can be exercised against a
known scene without depending on the camera lens (helpful when the
optics are out of focus / dirty / being tuned).

Usage:
    n6cam-inject-frame.py <image_path> [/dev/ttyACMx]

The image is resized to 192x192 and converted to RGB888 (the format the
NN ancillary buffer uses). It's sent to the kit framed as:

    'FRMI'  (4)
    size_le (4)  -- always 192*192*3 = 110592
    crc32_le(4)  -- zlib.crc32 of the payload
    payload (192*192*3 bytes, R,G,B,R,G,B,...)

After upload, you'll typically follow with:
    > frame run

over the shell to actually run NN inference. The kit prints the
detection count + top boxes (class index + confidence + bbox).

This script needs Pillow for image resize. Fallback: any external tool
that produces a 192x192 RGB888 raw file; use --raw <file>.
"""
import argparse
import os
import re
import struct
import sys
import time
import zlib


def _write(fd, data: bytes):
    """Blocking-ish write over a non-blocking CDC fd.

    The port is opened O_NONBLOCK so a stalled kit cannot wedge us forever;
    that means partial writes and EAGAIN are normal and must be retried. The
    old code used a plain buffered file object, which surfaced the stall as
    'OSError: [Errno 5] Input/output error' partway through the payload.
    """
    n = 0
    while n < len(data):
        try:
            n += os.write(fd, data[n:])
        except BlockingIOError:
            time.sleep(0.005)


def _drain(fd, secs: float):
    end = time.time() + secs
    while time.time() < end:
        try:
            os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.02)


def _read_until(fd, needles, timeout: float) -> bytes:
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            chunk = b""
        if chunk:
            buf += chunk
            if any(nd in buf for nd in needles):
                break
        else:
            time.sleep(0.02)
    return buf

# Fallback geometry only. The real size is whatever the running firmware
# reports from `frame upload` (CAMERA_ANCILLARY_WIDTH/HEIGHT x BPP), which is
# 256x256x3 = 196608 on the current build. This used to be hardcoded to
# 300x300x3 = 270000, so every upload was rejected with
# "ERROR: size=270000, expected 196608" — the kit then ran inference on
# whatever was already in the buffer, which is what "0 detections on every
# image" looked like from the outside. Negotiate instead of assuming.
FRAME_W = 256
FRAME_H = 256
FRAME_BYTES = FRAME_W * FRAME_H * 3


def load_image_as_rgb888(path: str, w: int = None, h: int = None) -> bytes:
    """Resize + convert to RGB888. Uses Pillow if present."""
    try:
        from PIL import Image  # type: ignore
    except ImportError:
        print(
            "Pillow not installed. Install with 'pip install Pillow', "
            "or provide a pre-converted raw file with --raw."
        )
        sys.exit(1)

    w = w or FRAME_W
    h = h or FRAME_H
    img = Image.open(path).convert("RGB").resize((w, h), Image.LANCZOS)
    data = img.tobytes()  # always R,G,B,R,G,B,...
    if len(data) != w * h * 3:
        print(f"Bad resize: got {len(data)}, expected {w * h * 3}")
        sys.exit(1)
    return data


def discover_tty() -> str:
    """The CDC shell port, resolved from its by-id link.

    The kit enumerates as ttyACM1 or ttyACM2 depending on what else is plugged
    in and on whether it has been reflashed since boot, so a default of
    /dev/ttyACM1 is right about half the time — and when it is wrong it fails
    as "stty failed", which reads like a broken kit rather than a moved port.
    """
    import glob
    for link in sorted(glob.glob(
            "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02")):
        real = os.path.realpath(link)
        if os.path.exists(real):
            return real
    return "/dev/ttyACM1"


def main() -> int:
    ap = argparse.ArgumentParser(description="N6Cam NN test-frame uploader")
    ap.add_argument("image", nargs="?", help="Image file (PNG/JPEG/etc.)")
    ap.add_argument("tty", nargs="?", default=None,
                    help="CDC tty (default: resolved from /dev/serial/by-id)")
    ap.add_argument("--raw", help="Use a pre-prepared raw RGB888 192x192 file instead")
    args = ap.parse_args()

    if not args.raw and not args.image:
        ap.print_help()
        return 1

    if not args.tty:
        args.tty = discover_tty()
        print(f"Camera port: {args.tty}")

    rc = os.system(f"stty -F {args.tty} 115200 cs8 -cstopb -parenb raw -echo")
    if rc != 0:
        print(f"stty failed on {args.tty}")
        return 1

    fd = os.open(args.tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        # A previous aborted upload leaves the kit mid-payload and desyncs the
        # shell; clear before arming or the header is read as pixel data.
        _write(fd, b"\nframe clear\n")
        _drain(fd, 0.4)

        _write(fd, b"\nframe upload\n")
        banner = _read_until(fd, (b"Ready", b"ERROR"), 4.0)

        # The kit tells us exactly what it wants -- believe it rather than a
        # constant in this file, which is how the 300x300/256x256 mismatch went
        # unnoticed for so long.
        m = re.search(rb"(\d+)\s+bytes RGB(?:\s*\((\d+)x(\d+)\))?", banner)
        if not m:
            print(f"No upload banner from kit: {banner[-200:]!r}")
            return 1
        nbytes = int(m.group(1))
        if m.group(2):
            w, h = int(m.group(2)), int(m.group(3))
        else:
            side = int(round((nbytes / 3) ** 0.5))
            w = h = side

        if args.raw:
            with open(args.raw, "rb") as f:
                data = f.read()
            if len(data) != nbytes:
                print(f"Raw file size {len(data)} != kit's expected {nbytes}")
                return 1
        else:
            data = load_image_as_rgb888(args.image, w, h)
        if len(data) != nbytes:
            print(f"Geometry mismatch: built {len(data)}, kit wants {nbytes}")
            return 1

        crc = zlib.crc32(data) & 0xFFFFFFFF
        print(f"Frame: {w}x{h} RGB888 ({len(data)} bytes, CRC32 0x{crc:08x})")

        _write(fd, b"FRMI" + struct.pack("<II", len(data), crc))
        for i in range(0, len(data), 1024):
            _write(fd, data[i:i + 1024])

        resp = _read_until(fd, (b"ok", b"ERROR"), 10.0).decode(errors="replace")
        print(resp.strip().replace("\n", " ")[-200:])
        if "ERROR" in resp:
            return 1
    finally:
        os.close(fd)

    print("Uploaded. Next:  > frame run    (over the same CDC port)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
