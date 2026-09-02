#!/usr/bin/env python3
"""How much does the reported count move when the picture does not? (ScopusQA #24)

ITP's report is "I put a picture in front of the camera, nothing moves, and the
notifications keep coming". A notification is emitted on a *change* in the
believed count, so that report is a claim about **stability**, and stability is
not something a single sweep can show. It needs the same scene sampled many
times.

A live camera looking at a still page does not deliver the same bytes twice:
there is sensor read noise, and there is the millimetre of tremor in whatever
holds the page. Neither is a change any person would call a change. So this
tool takes ONE image and manufactures N frames that differ only by those two
things —

    * per-pixel Gaussian noise, sigma a couple of LSB
    * a sub-pixel translation, under a pixel

— pushes each through the device, and records what came back. Everything the
counts then do is the detector's own instability, measured rather than argued
about.

Both legs run over the same frames, because "tiling is jumpy" is only a finding
next to what the single-frame path does with the identical input:

    default   the frame at 256x256 -> `frame run`      (1 inference)
    tile      the frame at 800x600 -> `tile run`       (13 inferences)

The number that matters at the bottom is **flips**: how many times the count
changed between consecutive samples. That is the notification rate the customer
sees, in the units they see it in.

    python3 scopus/tile_stability.py --image ~/qa-images/3_people_01.jpeg -n 20
    python3 scopus/tile_stability.py --image X --leg tile --conf 45

Writes scopus/results/tile-stability-<ts>.json next to the table.
"""
import argparse
import json
import os
import random
import re
import struct
import sys
import time
import zlib
from collections import Counter
from datetime import datetime
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib"))
from run_integration_tests import Camera                          # noqa: E402
from tile_compare import LIVE_W, LIVE_H, GRID_C, GRID_R, CROP     # noqa: E402
from tile_compare import tile_upload, tile_run, inject_retry      # noqa: E402
from tile_compare import TileRunRefused                           # noqa: E402

VEHICLE_CLASSES = (1, 2, 3, 4, 5, 6, 7, 8)   # _class_passes_mask in nn_task.c


def perturb(img, sigma, shift_px, rng):
    """One frame the camera could plausibly have delivered for the same scene.

    Deliberately conservative: this is meant to be indistinguishable from the
    source to a person, so that any count movement it produces is the
    detector's and not the test's.
    """
    from PIL import Image, ImageChops

    # Sub-pixel tremor. Affine with fractional offsets resamples rather than
    # rolling whole pixels, which is what a real 0.3 px shake looks like.
    dx = rng.uniform(-shift_px, shift_px)
    dy = rng.uniform(-shift_px, shift_px)
    out = img.transform(img.size, Image.AFFINE, (1, 0, dx, 0, 1, dy),
                        resample=Image.BILINEAR)

    if sigma > 0:
        # Image.effect_noise is Gaussian centred on 128 in a single band, so
        # adding it with offset=-128 adds a signed per-pixel deviation — read
        # noise, without a numpy dependency (the bench has PIL and no numpy).
        n = Image.effect_noise(img.size, sigma).convert("L")
        out = ImageChops.add(out, Image.merge("RGB", (n, n, n)),
                             scale=1, offset=-128)
    return out


def split(pairs):
    """[(cls, conf)] -> (people, vehicles) under the SoW §4.2 class split."""
    people = sum(1 for c, _ in pairs if c == 0)
    veh = sum(1 for c, _ in pairs if c in VEHICLE_CLASSES)
    return people, veh


BOX_RE = re.compile(r"\[\d+\]\s+\w+\((-?\d+)\)\s+conf=([\d.]+)\s+"
                    r"bbox=\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\)")


def boxes_of(raw):
    """Every box `tile run` printed, as dicts — kept in the JSON because the
    count alone never says WHICH detection came and went, and that is the
    first thing anyone reading this report wants to know."""
    out = []
    for cls, conf, x1, y1, x2, y2 in BOX_RE.findall(raw):
        x1, y1, x2, y2 = (round(float(v), 3) for v in (x1, y1, x2, y2))
        out.append({"cls": int(cls), "conf": float(conf),
                    "box": [x1, y1, x2, y2],
                    "w": round(x2 - x1, 3), "h": round(y2 - y1, 3)})
    return out


def flips(seq):
    """Consecutive changes in a sequence — one notification each."""
    return sum(1 for a, b in zip(seq, seq[1:]) if a != b)


def summarise(name, people, vehicles):
    tot = [p + v for p, v in zip(people, vehicles)]
    return {
        "leg": name,
        "samples": len(tot),
        "people": people,
        "vehicles": vehicles,
        "people_dist": dict(Counter(people)),
        "vehicle_dist": dict(Counter(vehicles)),
        "people_flips": flips(people),
        "vehicle_flips": flips(vehicles),
        "flips": flips(people) + flips(vehicles),
        "total_dist": dict(Counter(tot)),
    }


def show(s):
    print(f"  {s['leg']:8s} people {str(s['people']):<34s} {s['people_dist']}")
    print(f"  {'':8s} vehicl {str(s['vehicles']):<34s} {s['vehicle_dist']}")
    print(f"  {'':8s} -> {s['flips']} count change(s) over {s['samples']} "
          f"identical scenes "
          f"(people {s['people_flips']}, vehicles {s['vehicle_flips']})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("-n", "--samples", type=int, default=16)
    ap.add_argument("--sigma", type=float, default=2.0,
                    help="per-pixel noise sigma, LSB (0 = byte-identical frames)")
    ap.add_argument("--shift", type=float, default=0.4,
                    help="sub-pixel tremor, px")
    ap.add_argument("--conf", type=int, default=45)
    ap.add_argument("--iou", type=int, default=40)
    ap.add_argument("--leg", choices=("both", "tile", "default"), default="both")
    ap.add_argument("--fullpass", choices=("on", "off"), default=None)
    ap.add_argument("--edgedrop", choices=("on", "off"), default=None)
    ap.add_argument("--seed", type=int, default=24)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    from PIL import Image
    src = Image.open(args.image).convert("RGB").resize((LIVE_W, LIVE_H),
                                                       Image.LANCZOS)
    rng = random.Random(args.seed)
    tmp = Path("/tmp/tile-stability")
    tmp.mkdir(exist_ok=True)

    cam = Camera()
    t_people, t_veh, d_people, d_veh = [], [], [], []
    t_boxes = []
    try:
        # Offline uploads own the sweep accumulator, so the live tiled path has
        # to be off — the firmware refuses to interleave and would answer
        # neither question if it did not.
        #
        # Confirmed rather than assumed. A reply to this one command can be
        # lost behind a notification, and the run that follows then measures a
        # device still in tile mode: every `tile run` refused, every sample
        # recorded as zero people, and a table saying the detector is perfectly
        # stable at nothing. That happened on 2026-08-30.
        for attempt in range(3):
            cam.send("detect mode default", "detect mode", 4.0)
            if "detect mode: default" in cam.send("detect mode query",
                                                 "detect mode", 4.0):
                break
        else:
            print("could not put the device in 'detect mode default' — "
                  "the tiled sweep would be refused and read as zero.")
            return 1
        cam.send("detect profile 3 0", "ok", 4.0)     # both classes, no actions
        cam.send("detect start", "detect", 4.0)
        cam.send(f"tile frame {LIVE_W} {LIVE_H}", "tile", 4.0)
        cam.send(f"tile grid {GRID_C} {GRID_R}", "tile", 4.0)
        cam.send(f"tile crop {CROP}", "tile", 4.0)
        cam.send(f"tile thresh {args.conf} {args.iou}", "tile", 4.0)
        if args.fullpass:
            cam.send(f"tile fullpass {args.fullpass}", "tile", 4.0)
        if args.edgedrop:
            cam.send(f"tile edgedrop {args.edgedrop}", "tile", 4.0)

        print(f"{Path(args.image).name}: {args.samples} frames of the same "
              f"scene (noise sigma={args.sigma} LSB, tremor +/-{args.shift} px)")

        for i in range(args.samples):
            frame = perturb(src, args.sigma, args.shift, rng)
            path = tmp / f"s{i:03d}.png"
            frame.save(path)

            if args.leg in ("both", "default"):
                ok, detail = inject_retry(cam, path)
                if ok:
                    _, raw = cam.run_nn()
                    pairs = [(int(c), float(cf)) for c, cf in
                             re.findall(r"class=(-?\d+) conf=([\d.]+)", raw)]
                    p, v = split(pairs)
                else:
                    print(f"  [{i}] default upload failed: {detail}")
                    p = v = -1
                d_people.append(p)
                d_veh.append(v)
                cam.send("frame clear", "frame", 4.0)

            if args.leg in ("both", "tile"):
                ok, detail = tile_upload(cam, path, LIVE_W, LIVE_H)
                if ok:
                    try:
                        _, pairs, raw = tile_run(cam, GRID_C * GRID_R + 1)
                    except TileRunRefused as e:
                        print(f"  [{i}] tile run refused: {e}")
                        p = v = -1
                        t_boxes.append([])
                    else:
                        p, v = split(pairs)
                        t_boxes.append(boxes_of(raw))
                else:
                    print(f"  [{i}] tile upload failed: {detail}")
                    p = v = -1
                    t_boxes.append([])
                t_people.append(p)
                t_veh.append(v)
                cam.send("tile clear", "tile", 4.0)
    finally:
        cam.close()

    print()
    legs = []
    if args.leg in ("both", "default"):
        legs.append(summarise("default", d_people, d_veh))
    if args.leg in ("both", "tile"):
        legs.append(summarise("tile", t_people, t_veh))
    for s in legs:
        show(s)

    out = args.out or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "results",
        f"tile-stability-{datetime.now():%Y%m%d_%H%M%S}.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump({"image": args.image, "samples": args.samples,
                   "sigma": args.sigma, "shift": args.shift,
                   "conf": args.conf, "iou": args.iou,
                   "fullpass": args.fullpass, "edgedrop": args.edgedrop,
                   "legs": legs, "tile_boxes": t_boxes}, f, indent=2)
    print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
