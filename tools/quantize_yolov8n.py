#!/usr/bin/env python3.12
"""Quantize yolov8n_192.onnx to INT8 using ONNX Runtime static quantization
with a calibration set of real-world photos.

Default calibration: COCO128 (Ultralytics' standard small calibration set,
128 representative images from COCO val2017). If not present at
/tmp/coco128/images/train2017, falls back to tests/images/.

Output: /tmp/yolov8n_192_int8.onnx, ready to feed into stedgeai.
"""
import os
import sys
import argparse
from pathlib import Path

import numpy as np
from PIL import Image
from onnxruntime.quantization import (
    quantize_static, QuantType, QuantFormat, CalibrationDataReader,
    CalibrationMethod
)
from onnxruntime.quantization.shape_inference import quant_pre_process


SRC      = "/tmp/yolov8n_192.onnx"
PREP     = "/tmp/yolov8n_192_prep.onnx"
DST      = "/tmp/yolov8n_192_int8.onnx"


def find_calib_dir() -> Path:
    candidates = [
        Path("/tmp/coco128/images/train2017"),
        Path(__file__).parent.parent / "tests" / "images",
    ]
    for c in candidates:
        if c.is_dir():
            imgs = [p for p in c.iterdir()
                    if p.suffix.lower() in {".jpg", ".jpeg", ".png"}]
            if imgs:
                return c
    raise RuntimeError("No calibration images found in any candidate dir")


class ImageCalibrationReader(CalibrationDataReader):
    """Yields (1,3,192,192) float32 NCHW images to onnxruntime for static
    quantization."""

    def __init__(self, calib_dir: Path, n_samples: int = 128):
        imgs = sorted(p for p in calib_dir.iterdir()
                      if p.suffix.lower() in {".jpg", ".jpeg", ".png"})
        if not imgs:
            raise RuntimeError(f"No calibration images in {calib_dir}")
        self.samples = []
        for i in range(n_samples):
            p = imgs[i % len(imgs)]
            try:
                img = Image.open(p).convert("RGB").resize(
                    (192, 192), Image.LANCZOS)
                arr = np.asarray(img, dtype=np.float32) / 255.0
                arr = arr.transpose(2, 0, 1)[np.newaxis, ...]
                self.samples.append({"images": arr})
            except Exception as e:
                print(f"  skip {p.name}: {e}", file=sys.stderr)
        self._iter = iter(self.samples)
        print(f"  Loaded {len(self.samples)} calibration samples from {calib_dir}")

    def get_next(self):
        return next(self._iter, None)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=128,
                    help="How many calibration images to feed (default 128).")
    ap.add_argument("--method", choices=["minmax", "entropy", "percentile"],
                    default="entropy",
                    help="Calibration method (default: entropy — better for YOLO).")
    args = ap.parse_args()

    if not os.path.exists(SRC):
        print(f"ERROR: missing {SRC}. Run the Ultralytics ONNX export first.")
        return 2

    calib_dir = find_calib_dir()
    print(f"=== Calibration directory: {calib_dir} ===")
    print(f"=== Preprocessing {SRC} for quantization ===")
    quant_pre_process(SRC, PREP, skip_optimization=False, skip_onnx_shape=False)

    print(f"=== Running static INT8 quantization "
          f"({args.samples} samples, {args.method}) ===")
    method_map = {
        "minmax": CalibrationMethod.MinMax,
        "entropy": CalibrationMethod.Entropy,
        "percentile": CalibrationMethod.Percentile,
    }
    reader = ImageCalibrationReader(calib_dir, n_samples=args.samples)
    quantize_static(
        model_input=PREP,
        model_output=DST,
        calibration_data_reader=reader,
        quant_format=QuantFormat.QDQ,
        activation_type=QuantType.QInt8,
        weight_type=QuantType.QInt8,
        per_channel=True,
        calibrate_method=method_map[args.method],
        op_types_to_quantize=["Conv", "MatMul", "Mul", "Add"],
    )
    print(f"=== Done. Output: {DST} ===")
    print(f"    src  size: {os.path.getsize(SRC):>10} B")
    print(f"    int8 size: {os.path.getsize(DST):>10} B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
