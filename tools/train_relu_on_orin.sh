#!/bin/bash
# train_relu_on_orin.sh — produce an ATON-friendly yolov8n for the N6Cam.
#
# Approach:
#   1) Load Ultralytics yolov8n.pt (trained with SiLU activations).
#   2) Swap every SiLU with ReLU in-place (recursive replacement).
#   3) Fine-tune on COCO128 for a handful of epochs to recover accuracy
#      after the activation swap.
#   4) Export INT8 TFLite at 192x192 with COCO128 calibration.
#   5) scp the .tflite back to the laptop, ready for stedgeai compile.
#
# Why this works on N6Cam: STM32N6's ATON NPU accelerates ReLU but falls
# back to slow software-float for SiLU. Replacing SiLU with ReLU keeps
# almost all ops on the NPU, dropping inference from 6 s/frame to a few
# tens of ms.
#
# Usage from the LAPTOP:
#   ./tools/train_relu_on_orin.sh [epochs] [imgsz]
#       epochs : default 10
#       imgsz  : default 192

set -e

ORIN_HOST=${ORIN_HOST:-uvision@10.100.102.137}
ORIN_PASS=${ORIN_PASS:-Aa123456}
EPOCHS=${1:-10}
IMGSZ=${2:-192}

echo "=== ReLU yolov8n training on Orin: $EPOCHS epochs at ${IMGSZ}x${IMGSZ} ==="

sshpass -p "$ORIN_PASS" ssh -o StrictHostKeyChecking=no "$ORIN_HOST" \
  "cat > /tmp/train_relu.py" <<'PY'
"""Swap SiLU -> ReLU in yolov8n.pt, fine-tune briefly, export INT8 TFLite."""
import os, sys, shutil, time
from pathlib import Path
import torch
from ultralytics import YOLO

EPOCHS = int(os.environ.get("EPOCHS", 10))
IMGSZ  = int(os.environ.get("IMGSZ", 192))
WORK   = Path("/tmp/relu_train"); WORK.mkdir(parents=True, exist_ok=True)
os.chdir(WORK)

print(f"torch {torch.__version__}  CUDA {torch.cuda.is_available()}")
print(f"GPU: {torch.cuda.get_device_name(0)}")

def swap_silu_to_relu(mod):
    n = 0
    for name, child in list(mod.named_children()):
        if isinstance(child, torch.nn.SiLU):
            setattr(mod, name, torch.nn.ReLU(inplace=True))
            n += 1
        else:
            n += swap_silu_to_relu(child)
    return n

# Load yolov8n.pt and modify in-memory. Do NOT save+reload — Ultralytics
# YOLO() loader rebuilds the architecture from yaml, which resets the
# activations back to SiLU. Keep the same instance through train+export.
m = YOLO("yolov8n.pt")
n = swap_silu_to_relu(m.model)
print(f"Swapped {n} SiLU -> ReLU in-memory.")

t0 = time.time()
m.train(
    data="coco128.yaml",
    epochs=EPOCHS, imgsz=IMGSZ, batch=16, device=0,
    project="relu_run", name="yolov8n_relu",
    exist_ok=True, cache=False, plots=False, verbose=True,
)
print(f"Train: {time.time()-t0:.1f}s")

# After train(), Ultralytics may also rebuild the model. Re-swap to be sure.
n2 = swap_silu_to_relu(m.model)
print(f"Re-swap after train: {n2} SiLU layers (should be 0 if persistent).")

t0 = time.time()
out = m.export(format="tflite", int8=True, imgsz=IMGSZ, batch=1,
               data="coco128.yaml")
print(f"Export: {time.time()-t0:.1f}s -> {out}")

dst = Path("/tmp/yolov8n_relu_int8.tflite")
shutil.copy(out, dst)
print(f"FINAL: {dst} ({dst.stat().st_size} bytes)")
PY

echo "=== Running on Orin ==="
sshpass -p "$ORIN_PASS" ssh -o StrictHostKeyChecking=no "$ORIN_HOST" \
  "EPOCHS=$EPOCHS IMGSZ=$IMGSZ python3 /tmp/train_relu.py 2>&1" | tail -60

echo "=== Pulling INT8 TFLite back to the laptop ==="
sshpass -p "$ORIN_PASS" scp -o StrictHostKeyChecking=no \
  "$ORIN_HOST:/tmp/yolov8n_relu_int8.tflite" /tmp/yolov8n_relu_int8.tflite

ls -lh /tmp/yolov8n_relu_int8.tflite
echo "=== Done. Next: compile + push ==="
