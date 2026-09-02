#!/usr/bin/env python3
"""Measure what main-path tiling actually buys, image by image (ScopusQA #22).

Two legs over the same picture, on the same device, with the same network:

  default   the image at 256x256 -> `frame run`
            — the whole field of view in one downscale, which is what the NN
              ate on every camera frame before tiling.

  tile      the image at 800x600 -> `tile run`, 4x3 grid, 256 px crops
            — the geometry `detect mode tile` arms, over the same resolution
              the live main pipe carries. Twelve inferences, merged with
              cross-tile NMS.

800x600 is not a choice: it is CAMERA_MAIN_WIDTH x CAMERA_MAIN_HEIGHT, the pipe
the live sweep snapshots. Feeding the tile leg a sensor-resolution image would
measure a camera the product does not have.

    python3 scopus/tile_compare.py [image-dir] [--conf 45] [--iou 40]

Writes scopus/results/tile-compare.json alongside the table.
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

sys.path.insert(0, "scopus")
sys.path.insert(0, "scopus/lib")
from run_integration_tests import Camera                          # noqa: E402

COCO = {0: "person", 1: "bicycle", 2: "car", 3: "motorcycle", 4: "airplane",
        5: "bus", 6: "train", 7: "truck", 8: "boat"}

# The live main pipe. Both legs are anchored to it — see the module docstring.
LIVE_W, LIVE_H = 800, 600
GRID_C, GRID_R = 4, 3
CROP = 256


def _classes(pairs):
    """[(cls, conf), ...] -> {"person": [0.81, 0.62], ...}, confidences sorted
    high first so the table reads as 'best evidence for this class'."""
    by = {}
    for c, cf in pairs:
        by.setdefault(COCO.get(c, f"class{c}"), []).append(round(cf, 2))
    return {k: sorted(v, reverse=True) for k, v in sorted(by.items())}


# ── Ground truth, from the file names ─────────────────────────────────────
#
# The ScopusQA set labels itself: 5_people.jpeg has five people in it,
# 1_person_1_vehicle_02.jpeg has one of each. That is the only ground truth
# available without someone re-annotating 36 photographs, and it is enough to
# turn "tile finds more" — which is not a result — into "tile is closer to the
# right number", which is. Images whose names carry no count are still run and
# still printed; they just do not score.
_WORD = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
         "seven": 7, "eight": 8, "nine": 9, "ten": 10}


def truth(name):
    """(people, vehicles) if the name says, else (None, None)."""
    n = name.lower()
    p = v = None

    m = re.match(r"^(\d+)_(?:people|person)(?=_|\.|$)", n)
    if m:
        p = int(m.group(1))
    else:
        m = re.match(r"^([a-z]+)-people(?=-|_|\.|$)", n)
        if m and m.group(1) in _WORD:
            p = _WORD[m.group(1)]

    m = re.search(r"(\d+)_(?:car|cars|vehicle|vehicles|truck|trucks)(?=_|\.|$)", n)
    if m:
        v = int(m.group(1))
    elif re.match(r"^1_[a-z]+_car(_night)?(?=_|\.|$)", n):
        v = 1                      # 1_yellow_car, 1_gray_car_night, ...

    # A name that counts one class and is silent about the other is silent,
    # not zero: 3_people_01.jpeg does not promise there are no cars behind them.
    return p, v


def _vehicles(by):
    """SoW §4.2 vehicle bit — COCO 1..8, per _class_passes_mask in nn_task.c."""
    return sum(len(v) for k, v in by.items()
               if k in ("bicycle", "car", "motorcycle", "airplane",
                        "bus", "train", "boat", "truck"))


def inject_retry(cam, img, tries=3):
    """Kept as a name: `Camera.inject` retries by itself now, so every caller
    of it gets what only this wrapper used to have. See its docstring for the
    race — a `+SDVRNTF` line landing between the command and its banner."""
    cam.n6.drain(0.5)
    return cam.inject(img, tries=tries)


def tile_upload(cam, image_path, w, h):
    """Push an RGB888 frame in over `tile upload`. Same FRMI framing as
    `frame upload`, different destination buffer and no size constraint beyond
    the 16 MB stash."""
    from PIL import Image
    cam.n6.drain(0.3)
    banner = cam.n6.send("tile upload", "Ready", 5.0)
    if "Ready" not in banner:
        return False, f"no upload banner: {banner[-120:]!r}"

    data = Image.open(image_path).convert("RGB").resize((w, h),
                                                        Image.LANCZOS).tobytes()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    cam.n6._write(b"FRMI" + struct.pack("<II", len(data), crc))
    for i in range(0, len(data), 1024):
        cam.n6._write(data[i:i + 1024])
    out = cam._read_until((b"ok", b"ERROR"), 20.0)
    return ("ERROR" not in out), out.strip().replace("\n", " ")[-120:]


class TileRunRefused(RuntimeError):
    """`tile run` answered with a refusal instead of a sweep.

    It refuses for two reasons — the NN is stopped, or main-path tiling owns
    the accumulator — and both answers contain no boxes. Read as a result that
    is a count of zero, which is a number a report will happily print and
    nobody can tell from a real empty scene. It cost a wrong "0 people, 0
    changes" table on 2026-08-30, so it is an exception now, not a value."""


def tile_run(cam, tiles):
    """`tile run` is silent for the whole sweep then emits the report at once,
    so the timeout has to cover every tile — ~100 ms of inference each, plus
    the resize, plus slack for a notification landing mid-sweep."""
    budget = tiles * 0.5 + 8.0
    out = cam.n6.send("tile run", "tile run ok", budget)
    if ("owns the engine" in out) or ("NN stopped" in out):
        raise TileRunRefused(out.strip().replace("\n", " ")[-160:])
    dets = [(int(c), float(cf)) for c, cf in
            re.findall(r"\]\s+\w+\((-?\d+)\)\s+conf=([\d.]+)", out)]
    m = re.search(r"->\s*(\d+)\s+after NMS", out)
    if m is None:
        raise TileRunRefused(f"no sweep report in the reply: "
                             f"{out.strip()[-160:]!r}")
    return int(m.group(1)), dets, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", nargs="?",
                    default=os.path.expanduser("~/qa-images"))
    ap.add_argument("--conf", type=int, default=45, help="confidence %%")
    ap.add_argument("--iou", type=int, default=40, help="NMS IoU %%")
    args = ap.parse_args()

    d = Path(args.dir)
    imgs = sorted(p for p in d.iterdir()
                  if p.suffix.lower() in (".jpg", ".jpeg", ".png", ".webp"))
    if not imgs:
        sys.exit(f"no images in {d}")

    cam = Camera()
    rows = []
    try:
        # Both legs are offline uploads, so the main path must be OFF: the
        # sweep accumulator is a single static and the live loop owns it
        # whenever `detect mode tile` is on. The firmware refuses rather than
        # interleaving, which would corrupt both answers.
        cam.send("detect mode default", "detect mode", 4.0)
        cam.send("detect profile 3 0", "ok", 4.0)   # both classes, no actions
        cam.send("detect start", "detect", 4.0)
        cam.send(f"tile frame {LIVE_W} {LIVE_H}", "tile", 4.0)
        cam.send(f"tile grid {GRID_C} {GRID_R}", "tile", 4.0)
        cam.send(f"tile crop {CROP}", "tile", 4.0)
        cam.send(f"tile thresh {args.conf} {args.iou}", "tile", 4.0)

        print(f"{'image':38s} {'default':>18s}   {'tile':>18s}")
        print("-" * 80)

        for img in imgs:
            row = {"image": img.name}

            # ── default leg ────────────────────────────────────────────
            ok, detail = inject_retry(cam, img)
            if ok:
                count, raw = cam.run_nn()
                pairs = [(int(c), float(cf)) for c, cf in
                         re.findall(r"class=(-?\d+) conf=([\d.]+)", raw)]
                row["default"] = {"count": count, "classes": _classes(pairs)}
            else:
                row["default"] = {"error": detail}
            cam.send("frame clear", "frame", 4.0)

            # ── tile leg ───────────────────────────────────────────────
            ok, detail = tile_upload(cam, img, LIVE_W, LIVE_H)
            if ok:
                kept, pairs, _ = tile_run(cam, GRID_C * GRID_R)
                row["tile"] = {"count": kept, "classes": _classes(pairs)}
            else:
                row["tile"] = {"error": detail}
            cam.send("tile clear", "tile", 4.0)

            def brief(leg):
                if "error" in leg:
                    return "ERR:" + leg["error"][-40:]
                by = leg["classes"]
                p = len(by.get("person", []))
                v = _vehicles(by)
                return f"{p}p {v}v"

            rows.append(row)
            print(f"{img.name:38s} {brief(row['default']):>18s}   "
                  f"{brief(row['tile']):>18s}", flush=True)
    finally:
        cam.send("frame clear", "frame", 4.0)
        cam.send("tile clear", "tile", 4.0)
        cam.close()

    out = Path("scopus/results/tile-compare.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(rows, indent=2))

    # ── score against the labelled images ─────────────────────────────
    #
    # Absolute count error, because the count IS the product: `rsd` in a §4.2
    # notification is what the customer's server acts on. Nineteen people where
    # there are five is not a richer answer, it is a wrong one.
    err = {"default": [], "tile": []}
    print("-" * 80)
    print(f"{'labelled image':38s} {'truth':>8s} {'default':>10s} {'tile':>10s}")
    for r in rows:
        tp_, tv_ = truth(r["image"])
        if tp_ is None and tv_ is None:
            continue
        if "error" in r["default"] or "error" in r["tile"]:
            continue
        line = [f"{r['image']:38s}"]
        cell = []
        for leg in ("default", "tile"):
            by = r[leg]["classes"]
            e = 0
            if tp_ is not None:
                e += abs(len(by.get("person", [])) - tp_)
            if tv_ is not None:
                e += abs(_vehicles(by) - tv_)
            err[leg].append(e)
            cell.append(f"{len(by.get('person', []))}p {_vehicles(by)}v")
        t_s = f"{tp_ if tp_ is not None else '?'}p {tv_ if tv_ is not None else '?'}v"
        print(f"{line[0]} {t_s:>8s} {cell[0]:>10s} {cell[1]:>10s}")

    print("-" * 80)
    n = len(err["default"])
    if n:
        for leg in ("default", "tile"):
            tot = sum(err[leg])
            exact = sum(1 for e in err[leg] if e == 0)
            print(f"{leg:8s}  total count error {tot:4d} over {n} labelled "
                  f"image(s), exact on {exact}")
        better = "tile" if sum(err["tile"]) < sum(err["default"]) else "default"
        print(f"closer to the truth: {better.upper()}")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
