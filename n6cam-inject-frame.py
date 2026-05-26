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
import struct
import sys
import time
import zlib

# SSD-MobileNetV2/VOC takes 300x300x3 (the NN ancillary buffer size).
FRAME_W = 300
FRAME_H = 300
FRAME_BYTES = FRAME_W * FRAME_H * 3


def load_image_as_rgb888(path: str) -> bytes:
    """Resize + convert to RGB888. Uses Pillow if present."""
    try:
        from PIL import Image  # type: ignore
    except ImportError:
        print(
            "Pillow not installed. Install with 'pip install Pillow', "
            "or provide a pre-converted raw file with --raw."
        )
        sys.exit(1)

    img = Image.open(path).convert("RGB").resize((FRAME_W, FRAME_H), Image.LANCZOS)
    data = img.tobytes()  # always R,G,B,R,G,B,...
    if len(data) != FRAME_BYTES:
        print(f"Bad resize: got {len(data)}, expected {FRAME_BYTES}")
        sys.exit(1)
    return data


def main() -> int:
    ap = argparse.ArgumentParser(description="N6Cam NN test-frame uploader")
    ap.add_argument("image", nargs="?", help="Image file (PNG/JPEG/etc.)")
    ap.add_argument("tty", nargs="?", default="/dev/ttyACM1", help="CDC tty (default /dev/ttyACM1)")
    ap.add_argument("--raw", help="Use a pre-prepared raw RGB888 192x192 file instead")
    args = ap.parse_args()

    if args.raw:
        with open(args.raw, "rb") as f:
            data = f.read()
        if len(data) != FRAME_BYTES:
            print(f"Raw file size {len(data)} != expected {FRAME_BYTES}")
            return 1
    elif args.image:
        data = load_image_as_rgb888(args.image)
    else:
        ap.print_help()
        return 1

    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Frame: {FRAME_W}x{FRAME_H} RGB888 ({len(data)} bytes, CRC32 0x{crc:08x})")

    rc = os.system(f"stty -F {args.tty} 115200 cs8 -cstopb -parenb raw -echo")
    if rc != 0:
        print(f"stty failed on {args.tty}")
        return 1

    with open(args.tty, "r+b", buffering=0) as port:
        # Trigger the shell command.
        port.write(b"\nframe upload\n")
        port.flush()
        # Give the App a moment to switch into binary RX mode.
        time.sleep(0.4)

        # Header
        hdr = b"FRMI" + struct.pack("<II", len(data), crc)
        port.write(hdr)
        port.flush()

        # Payload — chunked so the device buffer stays ahead of CDC.
        CHUNK = 4096
        for i in range(0, len(data), CHUNK):
            port.write(data[i:i + CHUNK])
            port.flush()

    print("Uploaded. Next:  > frame run    (over the same CDC port)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
