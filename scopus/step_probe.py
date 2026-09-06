#!/usr/bin/env python3
"""Per-step probe: what does each step of a tiled sweep actually see?

`tile run` reports a sweep's merged answer, which is the right thing to report
and the wrong thing to debug with: when a sweep misses a person, the answer
cannot say whether no step saw them, or one step saw them and the merge threw
it away, or they were seen under the wrong class.

This reports every step separately: each 320 px tile, the whole-frame pass, and
the turned copy of either. It renders the step on the host exactly the way
_resize_crop() does in tile_detect.c and pushes it in through `frame upload` +
`frame run`, which reports raw boxes down to the network's own 0.30 floor,
below the sweep's 0.45 counting floor and its 0.34 sustain floor.

    python3 scopus/step_probe.py images_lying --out steps.json

It is what settled ScopusQA #26: over 15 pictures it showed that a turned TILE
is worth 0.71 to 0.86 on a person lying down while the turned WHOLE FRAME is
worth 0.00 to 0.78, which is why `detect rotate full` was not enough and
`auto` looks at tiles. Budget about 75 s an image for the full 26 steps.
"""
import argparse
import json
import os
import re
import struct
import sys
import time
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, "scopus")
sys.path.insert(0, "scopus/lib")
from run_integration_tests import Camera                          # noqa: E402

LIVE_W, LIVE_H = 800, 600
GRID_C, GRID_R = 4, 3
CROP = 320
NN = 256


def axis_origins(n, span, crop, ovl=0):
    """tile_axis_origins() in tile_detect.c."""
    c = min(crop, span)
    last = span - c
    if n <= 1:
        return [0]
    stride = max(1, last // (n - 1)) if ovl == 0 else max(1, c - ovl)
    o = [min(i * stride, last) for i in range(n)]
    o[-1] = last
    return o


def steps_for(rot):
    """The step list a sweep walks, as (cx, cy, cw, ch, rotated, is_tile)."""
    xs = axis_origins(GRID_C, LIVE_W, CROP)
    ys = axis_origins(GRID_R, LIVE_H, CROP)
    base = [(x, y, CROP, CROP, False, True) for y in ys for x in xs]
    base.append((0, 0, LIVE_W, LIVE_H, False, False))
    if rot == "off":
        return base
    if rot == "full":
        return base + [(0, 0, LIVE_W, LIVE_H, True, False)]
    return base + [(x, y, w, h, True, t) for (x, y, w, h, _, t) in base]


def resize_crop(src, cx, cy, cw, ch, rot):
    """_resize_crop() in tile_detect.c: bilinear, clamped, optional 90 deg CW.

    src is an (H, W, 3) uint8 array. Returns (NN, NN, 3) uint8.
    """
    fh, fw = src.shape[:2]
    stepx = (cw - 1) / (NN - 1) if cw > 1 else 0.0
    stepy = (ch - 1) / (NN - 1) if ch > 1 else 0.0
    i = np.arange(NN, dtype=np.float64)

    if not rot:
        fx = cx + i * stepx                      # varies with ox (columns)
        fy = cy + i * stepy                      # varies with oy (rows)
        gx = np.broadcast_to(fx[None, :], (NN, NN))
        gy = np.broadcast_to(fy[:, None], (NN, NN))
    else:
        # dst(ox, oy) samples src (cx + oy*stepx, cy + (N-1-ox)*stepy)
        fx = cx + i * stepx                      # varies with oy (rows)
        fy = cy + (NN - 1 - i) * stepy           # varies with ox (columns)
        gx = np.broadcast_to(fx[:, None], (NN, NN))
        gy = np.broadcast_to(fy[None, :], (NN, NN))

    x0 = np.clip(gx.astype(np.int64), 0, fw - 2)
    y0 = np.clip(gy.astype(np.int64), 0, fh - 2)
    wx = (gx - x0)[..., None]
    wy = (gy - y0)[..., None]

    s = src.astype(np.float64)
    p00 = s[y0, x0]
    p01 = s[y0, x0 + 1]
    p10 = s[y0 + 1, x0]
    p11 = s[y0 + 1, x0 + 1]
    top = p00 * (1.0 - wx) + p01 * wx
    bot = p10 * (1.0 - wx) + p11 * wx
    return np.clip(top * (1.0 - wy) + bot * wy + 0.5, 0, 255).astype(np.uint8)


def inject_bytes(cam, data, tries=3):
    """Camera._inject_once(), but for pixels we already have."""
    for _ in range(tries):
        cam.n6.send("frame clear", max_secs=2.0)
        cam.n6.drain(0.3)
        banner = cam.n6.send("frame upload", "Ready", 4.0)
        if not re.search(r"(\d+)\s+bytes RGB", banner):
            cam.n6.drain(0.5)
            continue
        crc = zlib.crc32(data) & 0xFFFFFFFF
        cam.n6._write(b"FRMI" + struct.pack("<II", len(data), crc))
        for i in range(0, len(data), 1024):
            cam.n6._write(data[i:i + 1024])
        out = cam._read_until((b"ok", b"ERROR"), 20.0)
        if "ERROR" not in out:
            return True
        cam.n6.drain(0.5)
    return False


def frame_run(cam):
    out = cam.n6.send("frame run", "frame run ok", 12.0)
    boxes = [(int(c), float(cf), float(x), float(y), float(w), float(h))
             for c, cf, x, y, w, h in re.findall(
                 r"class=(-?\d+)\s+conf=([\d.]+)\s+"
                 r"bbox=\(([\d.-]+),([\d.-]+),([\d.-]+),([\d.-]+)\)", out)]
    return boxes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--out", default="steps.json")
    ap.add_argument("--glob", default="*")
    args = ap.parse_args()

    files = sorted(p for p in Path(args.dir).iterdir()
                   if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp")
                   and p.match(args.glob))
    steps = steps_for("all")          # every step, both orientations
    cam = Camera()
    result = {}
    try:
        cam.n6.send("detect mode default", "detect mode", 5.0)
        cam.n6.send("detect start", "detect", 5.0)
        for f in files:
            img = np.asarray(Image.open(f).convert("RGB")
                             .resize((LIVE_W, LIVE_H), Image.LANCZOS))
            per_step = []
            t0 = time.time()
            for (cx, cy, cw, ch, rot, is_tile) in steps:
                tile = resize_crop(img, cx, cy, cw, ch, rot)
                if not inject_bytes(cam, tile.tobytes()):
                    per_step.append({"error": "upload failed"})
                    continue
                per_step.append({
                    "cx": cx, "cy": cy, "cw": cw, "ch": ch,
                    "rot": rot, "tile": is_tile,
                    "boxes": frame_run(cam),
                })
            result[f.name] = per_step
            Path(args.out).write_text(json.dumps(result, indent=1))
            print(f"{f.name:44s} {len(per_step)} steps, {time.time()-t0:.0f}s",
                  flush=True)
    finally:
        cam.close()
    Path(args.out).write_text(json.dumps(result, indent=1))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
