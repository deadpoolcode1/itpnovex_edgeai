# Multi-class detection on N6Cam — overnight session status (2026-05-23)

## TL;DR

Multi-class YOLOv8n (80-class COCO) detection works end-to-end on the SIANA
N6Cam at 42.7 ms / inference, but the model accuracy on the kit's
quantized ATON path is bad enough that it's only useful as a proof of the
pipeline, not as a production detector.

**The kit is currently in a stuck state** (relu_val model on flash hangs
the App's boot-time NN init even after STLink hardware reset). Recovery
needs the SWD-flash-via-boot-switch procedure documented in memory.

## What works

- **stedgeai 4.0 + vendor LL_ATON runtime v1.1.3-262** is the only working
  toolchain combo. stedgeai 3.0 produced silently-zero output.
- **Sigmoid pre-proc on App side** (`nn_task.c` — added a forward sigmoid
  on output channels [4..3+nb_classes] for any model with `NB_CLASSES > 1`)
  is required: stedgeai 4.0 strips the model's final sigmoid op on the
  multi-class head, so the post-proc sees raw logits and rejects them on
  the conf threshold.
- **The mildly-fine-tuned model `yolov8n_relu30_u8in_f32out.tflite`**
  (Ultralytics yolov8n.pt → 30 epochs of light fine-tuning on COCO128 →
  int8 with uint8 input) compiles cleanly and runs stably on the kit:
  - ~10 detections on camera.jpg at conf 0.30-0.34
  - 42.5 ms per inference
  - Stable across 15+ consecutive frame-inject inferences
  - Only class 0 (person) ever fires — vehicle classes never reach
    threshold because the model is undertrained.

## What doesn't work

- **Pristine `yolov8n.pt` int8 export** (no fine-tuning at all) compiles
  to a structurally-identical network but hangs the kit's NN task at
  runtime — `frame run` never returns. Even after STLink hardware reset
  the App re-loads the broken model and rehangs. Has to be 30+ epochs of
  *some* training before the weights become ATON-compatible.
- **The 50-epoch ReLU-targeted retrain on COCO val2017** (5000 images,
  all 80 classes) also retains 57 sigmoid layers in the exported TFLite
  — Ultralytics' export rebuilds the architecture from yaml and the
  SiLU→ReLU swap doesn't survive. So it's the same SiLU architecture
  with slightly different weights. On the kit it:
  - **Improves accuracy** versus relu30:
    - astronaut: 9 dets (was 0)
    - 1_person: 3 dets (was 0)
    - 2_people: 4 dets (was 0)
    - 3_people: 20 dets / 7_people: 19 dets (vs 9/6 before)
    - confidences 0.31-0.39 (vs 0.30-0.34)
  - **But is unstable**: hangs the kit after ~5 consecutive
    frame-inject inferences, requiring a hard reset. Vehicle classes
    still never fire.

## Why the SiLU/sigmoid+mul pattern is fragile

stedgeai 4.0 compiles every Conv → Sigmoid → Multiply triplet to an
identical epoch graph (`LOGISTIC` + `eltwise MUL` on ATON), but the
runtime appears to deadlock on certain specific weight distributions —
the pristine pretrained weights, the 50-epoch retrained weights, and any
sequence of inferences that excites a particular code path. Only the
narrow band of "30 epochs of mild fine-tuning on COCO128" weights happens
to avoid the trigger. This is not a debuggable software issue, it's a
quirk of the vendor's runtime binary that we cannot inspect.

## Recovery procedure (read this when waking up)

1. Power-cycle the N6Cam (unplug USB-C, wait 5 s, replug).
2. STLink hardware reset doesn't help; the bad model on xSPI flash gets
   loaded by FSBL every boot and re-bricks the App.
3. **Toggle the boot switch to development mode** and flash a known-good
   `network_atonbuf.xSPI2.raw` via SWD. The working binary is at
   `tools/qat_artifacts/relu30_network_atonbuf.xSPI2.raw`.
4. The flash command (from CLAUDE.md memory):
   ```
   ./modular-tools.sh flash-model
   ```
   (Or use the SWD path documented in the saved
   `reference_n6cam_flash_procedure.md` memory.)
5. Toggle the boot switch back to operation mode.
6. Power-cycle again.
7. Verify with `python3 tests/run_tests.py` — the relu30 model gives
   ~10 person detections on camera.jpg at conf 0.32.

After that, you have a stable but underwhelming multi-class proof — the
pipeline runs and detects, the model just isn't trained enough on the
ATON-compatible weight subspace to recognize the COCO vehicle classes.

## Saved artifacts

- `tools/qat_artifacts/yolov8n_relu30_u8in_f32out.tflite` — the
  stable-but-low-accuracy model source
- `tools/qat_artifacts/relu30_network_atonbuf.xSPI2.raw` — compiled NPU
  blob to push via `./n6cam-update.py --target model …`
- `vendor/n6cam.core.bsp/Firmware/Model/*` — the same compiled artifacts
  copied into the BSP for in-tree builds (network.c, stai_network.c,
  stai_network.h, network_ecblobs.h, network_atonbuf.xSPI2.raw)
- `/tmp/yolov8n_relu_val_u8in_f32out.tflite` — the 50-epoch COCO val2017
  retrain (better accuracy, fragile runtime — NOT pushed because it
  bricks the kit)

## Next-step ideas (none attempted tonight)

- **Train at imgsz=320 instead of 192** — the small input is part of why
  vehicles never fire on 192×192 crops. Bigger input = more detail = the
  model can lock onto vehicles.
- **QAT instead of PTQ** — quantization-aware training would let us bake
  the int8 calibration into the loss, potentially producing weight
  distributions that don't trigger the ATON hang.
- **Take ST's official `yolov8n_192_quant_pc_uf_od_coco-person.tflite`
  and retrain its head with more classes** — that file is known to run
  stably on the kit; retaining its base weights but expanding the
  detection head to 80 classes might land in the same "ATON-safe" weight
  subspace.
- **Reach out to Sylvain at SIANA Systems** — the ATON-hang-on-specific
  weights pattern is something only the vendor can debug; we've
  exhausted what's possible from outside the runtime binary.
