#!/usr/bin/env python3
"""
n6cam-prep-tests.py — convert a folder of test images (JPEG / PNG /
anything Pillow can read) into 192x192 RGB888 raw files ready to drop
on the kit's SD card. The kit then reads them back via:

    > frame load <file.raw>
    > frame run

For the test-frame regression workflow (skip the camera optics, validate
the algorithm against known scenes).

Usage:
    n6cam-prep-tests.py <input_dir> <output_dir>
    n6cam-prep-tests.py ./test_images ./sd_card_root/tests

Each input image is resized to 192x192 (Lanczos) and saved as
'<original_basename>.raw' (always 110,592 bytes).
"""
import argparse
import os
import sys
from pathlib import Path

FRAME_W = 192
FRAME_H = 192
FRAME_BYTES = FRAME_W * FRAME_H * 3

# Pillow-recognised extensions we'll convert. Skip anything else silently.
EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".webp", ".gif"}


def main() -> int:
    ap = argparse.ArgumentParser(description="N6Cam test-frame prep")
    ap.add_argument("input_dir", help="Folder of source images")
    ap.add_argument("output_dir", help="Folder to write .raw files into")
    ap.add_argument("--prefix", default="", help="Prefix for output filenames")
    args = ap.parse_args()

    try:
        from PIL import Image  # type: ignore
    except ImportError:
        print("Pillow not installed. Run: pip install Pillow")
        return 1

    in_dir = Path(args.input_dir)
    out_dir = Path(args.output_dir)
    if not in_dir.is_dir():
        print(f"input_dir {in_dir} is not a directory")
        return 1
    out_dir.mkdir(parents=True, exist_ok=True)

    sources = sorted(p for p in in_dir.iterdir()
                     if p.is_file() and p.suffix.lower() in EXTS)
    if not sources:
        print(f"No supported images found in {in_dir}")
        return 1

    print(f"Converting {len(sources)} image(s) → {out_dir}/")
    written = 0
    for src in sources:
        try:
            img = Image.open(src).convert("RGB")
            img = img.resize((FRAME_W, FRAME_H), Image.LANCZOS)
            data = img.tobytes()
        except Exception as exc:
            print(f"  skip {src.name}: {exc}")
            continue
        if len(data) != FRAME_BYTES:
            print(f"  skip {src.name}: bad bytes {len(data)}")
            continue
        # FAT-friendly 8.3-ish output names. We keep the stem but cap at 8 chars
        # so generic SD card hosts don't truncate, then add .raw (3 chars).
        stem = (args.prefix + src.stem).replace(" ", "_")[:8]
        out_path = out_dir / f"{stem}.raw"
        with open(out_path, "wb") as f:
            f.write(data)
        print(f"  {src.name:30s} -> {out_path.name}")
        written += 1

    print(f"\nDone. {written} file(s) written to {out_dir}/.")
    print("Copy them to the kit's SD card root, then on the kit:")
    print("    > detect start")
    print(f"    > frame load {written and (args.prefix + sources[0].stem)[:8] + '.raw' or 'NAME.raw'}")
    print("    > frame run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
