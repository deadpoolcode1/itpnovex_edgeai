# QAT artifacts (from session ending 2026-05-22)

These TFLite files are the fruits of the full-QAT-on-Orin pipeline.

## What's here

| File | Description | tensors |
|---|---|---|
| `yolov8n_relu_full_int8.tflite` | yolov8n with SiLU→ReLU swap, fine-tuned 5 epochs on COCO128, TF converter forced int8 in/out + TFLITE_BUILTINS_INT8 | 211 int8, 89 int32, 0 float32 |
| `yolov8n_relu_hybrid_int8.tflite` | Same as above but with `inference_output_type=tf.float32` for compatibility with vendor's `app_postprocess_od_yolov8` | 211 int8, 89 int32, 1 float32 (output) |

Stedgeai compile of the hybrid variant produces **113 HW + 10 hybrid + 2 SW epochs** on the STM32N6 ATON NPU (vs 30 HW / 13 hybrid / 196 SW for the SiLU-Sigmoid-Mul version).

## What was reproduced

1. NVIDIA Orin (10.100.102.137, uvision/Aa123456) provisioned with:
   - `torch 2.3.0` (NVIDIA JetPack 6 build, CUDA enabled on the Orin GPU)
   - `torchvision 0.18.1` built from source against the NVIDIA torch
   - `ultralytics 8.4.53`, `tensorflow 2.19.1`, `ai_edge_litert 2.1.5`
2. `tools/train_relu_on_orin.sh` ran end-to-end:
   - Loaded yolov8n.pt → swapped 56 SiLU layers with ReLU in-memory.
   - Fine-tuned 5 epochs on COCO128 at 192×192 on the Orin GPU.
   - Exported INT8 TFLite (using Ultralytics' SavedModel as the intermediate).
3. Custom converter (`tf.lite.TFLiteConverter` with `TFLITE_BUILTINS_INT8` + `inference_input_type=int8` + `inference_output_type=float32`) re-exported the SavedModel into a fully-int8 model (just the output stays float for vendor post-proc compatibility).
4. `stedgeai 3.0` compiled the resulting TFLite into a 3.1 MB `network_atonbuf.xSPI2.raw`. Op placement report: 113 HW / 10 hybrid / 2 SW (vs 30 / 13 / 196 for the SiLU-Sigmoid-Mul version, and vs the vendor's 1-class model running on ATON at ~17 ms).
5. Model + multi-class App pushed via UART (`./modular-tools.sh update model` + `update app`). Push succeeded mechanically.
6. **Open**: App's `nn_task` faults on the first inference using this model. Likely cause: post-processing layout assumes specific Q/DQ scale handling that vendor's `app_postprocess_od_yolov8.c` doesn't apply to the new tensor — needs a few hours of post-proc debugging or a safe-boot fallback (don't auto-start NN on cold boot).

## Next session

Resume by:
1. Reading this README.
2. Investigating `app_postprocess_od_yolov8.c` — does it handle a quantised output tensor's scale + zero_point, or does it assume fp32 [0,1]? The model now emits fp32 outputs but the *intermediates* are int8 — there may be a quantization-aware layer right before output that needs handling.
3. Implementing the "safe boot" (registry: detect_enable=false on cold boot, set true only on explicit `detect start`). This avoids auto-NN crash → CDC dead → SWD recovery on every model swap.
