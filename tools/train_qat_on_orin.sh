#!/bin/bash
# train_qat_on_orin.sh — Fine-tune yolov8n on the Orin GPU, export INT8
# TFLite for stedgeai compilation, scp back to the laptop.
#
# Run from the LAPTOP — it SSHes into the Orin to do the GPU work.
#
# Prereq:
#   - Orin reachable at $ORIN_HOST (default uvision@10.100.102.137)
#   - Orin has torch+CUDA+ultralytics+torchvision installed
#     (one-time setup; the JetPack 6 + Tegra wheels are in /tmp on Orin)
#
# Output: /tmp/yolov8n_pv_int8.tflite  (locally; scp'd from Orin)
#
# Usage:
#   ./tools/train_qat_on_orin.sh [epochs] [imgsz]
#       epochs : default 10
#       imgsz  : default 192 (matches the N6's camera ancillary buffer)
#
# After this completes, compile + deploy:
#   stedgeai generate --model /tmp/yolov8n_pv_int8.tflite --target stm32n6 \
#                     --st-neural-art default@$BSP/Model/user_neuralart.json
#   cp stedgeai_out/network*.{c,h} $BSP/Model/
#   ./modular-tools.sh build
#   ./modular-tools.sh update model
#   ./modular-tools.sh update app

set -e

ORIN_HOST=${ORIN_HOST:-uvision@10.100.102.137}
ORIN_PASS=${ORIN_PASS:-Aa123456}
EPOCHS=${1:-10}
IMGSZ=${2:-192}

echo "=== QAT training on Orin: $EPOCHS epochs at ${IMGSZ}x${IMGSZ} ==="

# Push the training script body, run it remotely, pull result back.
sshpass -p "$ORIN_PASS" ssh -o StrictHostKeyChecking=no "$ORIN_HOST" \
  "cat > /tmp/train_qat.py" <<'PY'
"""Fine-tune yolov8n then export INT8 TFLite at the requested image size."""
import os, sys
from pathlib import Path
import torch
from ultralytics import YOLO

EPOCHS = int(os.environ.get("EPOCHS", 10))
IMGSZ  = int(os.environ.get("IMGSZ", 192))
WORKDIR = Path("/tmp/qat_work")
WORKDIR.mkdir(parents=True, exist_ok=True)
os.chdir(WORKDIR)

print(f"torch: {torch.__version__}  CUDA: {torch.cuda.is_available()}")
print(f"GPU: {torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CPU'}")

model = YOLO("yolov8n.pt")
results = model.train(
    data="coco.yaml",
    epochs=EPOCHS, imgsz=IMGSZ, batch=16, device=0,
    project="qat_run", name="yolov8n_pv",
    exist_ok=True, cache=False, plots=False, verbose=True,
)

best_pt = Path("qat_run/yolov8n_pv/weights/best.pt")
if not best_pt.exists():
    print(f"ERROR: no best.pt at {best_pt}", file=sys.stderr); sys.exit(2)

final = YOLO(str(best_pt))
out_tflite = final.export(format="tflite", int8=True, imgsz=IMGSZ, batch=1)
print(f"OUTPUT: {out_tflite}")

import shutil
dst = Path("/tmp/yolov8n_pv_int8.tflite")
shutil.copy(out_tflite, dst)
print(f"COPIED: {dst}")
PY

echo "=== Running training on Orin (first run downloads COCO ~6 GB) ==="
sshpass -p "$ORIN_PASS" ssh -o StrictHostKeyChecking=no "$ORIN_HOST" \
  "EPOCHS=$EPOCHS IMGSZ=$IMGSZ python3 /tmp/train_qat.py"

echo "=== Pulling INT8 TFLite back to laptop ==="
sshpass -p "$ORIN_PASS" scp -o StrictHostKeyChecking=no \
  "$ORIN_HOST:/tmp/yolov8n_pv_int8.tflite" /tmp/yolov8n_pv_int8.tflite

ls -lh /tmp/yolov8n_pv_int8.tflite
echo "=== Done. Compile with stedgeai + push via ./modular-tools.sh update ==="
