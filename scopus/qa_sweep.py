#!/usr/bin/env python3
"""Run every ScopusQA test image through the device's NN and report what it
sees, by COCO class. Answers ScopusQA #17 with measurements rather than
opinion: which vehicles the shipped network finds, and which it does not."""
import json, re, sys, os
from pathlib import Path
sys.path.insert(0, "scopus"); sys.path.insert(0, "scopus/lib")
from run_integration_tests import Camera

COCO = {0: "person", 1: "bicycle", 2: "car", 3: "motorcycle", 4: "airplane",
        5: "bus", 6: "train", 7: "truck", 8: "boat"}

d = Path(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/qa-images"))
cam = Camera()
cam.send("detect profile 3 0", "ok", 4.0)      # both classes, no side effects
cam.send("detect start", "detect", 4.0)
rows = []
try:
    for img in sorted(d.iterdir()):
        if img.suffix.lower() not in (".jpg", ".jpeg", ".png", ".webp"):
            continue
        ok, detail = cam.inject(img)
        if not ok:
            rows.append({"image": img.name, "error": detail}); continue
        count, raw = cam.run_nn()
        dets = [(int(c), float(cf)) for c, cf in
                re.findall(r"class=(-?\d+) conf=([\d.]+)", raw)]
        by = {}
        for c, cf in dets:
            by.setdefault(COCO.get(c, f"class{c}"), []).append(round(cf, 2))
        rows.append({"image": img.name, "count": count, "classes": by})
        print(f"{img.name:42s} {count if count is not None else '-':>2}  "
              f"{json.dumps(by)}", flush=True)
finally:
    cam.send("frame clear", "frame", 4.0)
    cam.close()
Path("scopus/results/qa-image-sweep.json").write_text(json.dumps(rows, indent=2))
print("\nwrote scopus/results/qa-image-sweep.json")
