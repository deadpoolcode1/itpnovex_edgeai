#!/usr/bin/env python3.12
"""Quantize yolov8n_192.onnx to INT8 using ONNX Runtime static quantization
with COCO-derived calibration data.

Outputs: /tmp/yolov8n_192_int8.onnx
"""
import os
import sys
import numpy as np
from pathlib import Path
from PIL import Image
from onnxruntime.quantization import (
    quantize_static, QuantType, QuantFormat, CalibrationDataReader,
    CalibrationMethod
)
from onnxruntime.quantization.shape_inference import quant_pre_process


SRC      = "/tmp/yolov8n_192.onnx"
PREP     = "/tmp/yolov8n_192_prep.onnx"
DST      = "/tmp/yolov8n_192_int8.onnx"
CALIB_DIR = Path("/home/ilan/work/itpnovex/edgeai/tests/images")


class ImageCalibrationReader(CalibrationDataReader):
    """Yields (1,3,192,192) float32 NCHW images to onnxruntime for static
    quantization. Uses any image files in CALIB_DIR; samples cyclically
    until N samples consumed (we use 16 for a quick pass)."""

    def __init__(self, n_samples: int = 16):
        all_imgs = sorted(p for p in CALIB_DIR.iterdir()
                          if p.suffix.lower() in {".jpg", ".jpeg", ".png"})
        if not all_imgs:
            raise RuntimeError(f"No calibration images in {CALIB_DIR}")
        self.samples = []
        for i in range(n_samples):
            p = all_imgs[i % len(all_imgs)]
            img = Image.open(p).convert("RGB").resize((192, 192), Image.LANCZOS)
            arr = np.asarray(img, dtype=np.float32) / 255.0   # HWC, [0,1]
            arr = arr.transpose(2, 0, 1)[np.newaxis, ...]     # 1xCxHxW
            self.samples.append({"images": arr})
        self._iter = iter(self.samples)

    def get_next(self):
        return next(self._iter, None)


def main():
    if not os.path.exists(SRC):
        print(f"ERROR: missing {SRC}")
        return 2
    print(f"=== Preprocessing {SRC} for quantization ===")
    quant_pre_process(SRC, PREP, skip_optimization=False, skip_onnx_shape=False)
    print(f"=== Running static INT8 quantization (16 calibration samples) ===")
    reader = ImageCalibrationReader(n_samples=16)
    quantize_static(
        model_input=PREP,
        model_output=DST,
        calibration_data_reader=reader,
        quant_format=QuantFormat.QDQ,
        activation_type=QuantType.QInt8,
        weight_type=QuantType.QInt8,
        per_channel=True,
        calibrate_method=CalibrationMethod.MinMax,
        op_types_to_quantize=["Conv", "MatMul", "Mul", "Add"],
    )
    print(f"=== Done. Output: {DST} ===")
    print(f"    src  size: {os.path.getsize(SRC):>10} B")
    print(f"    int8 size: {os.path.getsize(DST):>10} B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
