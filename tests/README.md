# N6Cam Test Suite

End-to-end test runner driving the kit's CDC shell. Verifies every SoW UART command except `update` / `recovery` (we live in the update flow, it gets tested by every iteration), exercises the SD photo pipeline, and runs the detection algorithm against a folder of known images so the NN can be validated without depending on the camera optics.

## Run

```bash
./modular-tools.sh test
# or
python3 tests/run_tests.py
```

Optional flags:
- `--tty <PATH>` — CDC port (default: auto-detect N6Cam by USB id, falls back to `/dev/ttyACM1`)
- `--images <DIR>` — folder of test images for the algorithm group (default: `tests/images/`)
- `--out <FILE>` — explicit HTML report path (default: `results/test-report-<timestamp>.html`)

Output:
- Coloured PASS/FAIL/SKIP table on stdout
- `results/test-report-<timestamp>.html` (V20_SDVR-style)
- `results/test-report-<timestamp>.pdf` if `wkhtmltopdf` or headless Chrome is on PATH
- `results/test-report-<timestamp>_artifacts/` — for every per-image
  inference test (group 9 + 11) this directory contains the 192×192
  source frame the kit actually saw plus the same frame with the
  kit-reported detection rectangles drawn on top. The HTML report
  embeds both side-by-side under each row so you can verify what the
  device detected without re-running anything.

## What's covered

| Group | Tests | What it proves |
|---|---|---|
| 0 Prerequisites | T00.1–T00.5 | CDC port present, `help` parseable, fw / uid / uptime built-ins |
| 1 Shell mechanics | T01.1–T01.4 | `echo on/off/query`, persisted ack format (SoW §4.1) |
| 2 System | T02.1–T02.4 | `rtc` query, `rtc set` round-trip, `version` |
| 3 Sensors | T03.1–T03.4 | `irled on/off`, `motion sense/query` persistence (§3.5) |
| 4 Photo settings | T04.1–T04.4 | `img quality/color/chroma` runtime apply + `img query` (§3.4 / W8) |
| 5 Detection control | T05.1–T05.3 | `detect start`, profile bitmask set/get (§3.1 / §4.2) |
| 6 Notifications | T06.1–T06.5 | `notify enable/period/query`, `trigger` emits SoW §6 JSON, `disable` |
| 7 SD + photo | T07.1–T07.4 | `sd query` mounted, `sd ls`, `photo savesd` creates a new `.rdy` (W7/W9/W10/W12/W14) |
| 8 Frame injection | T08.1–T08.4 | Synthetic frame upload + run with NN-time reporting |
| 9 Algorithm | T09.1–T09.7 | Real photos through NN: person photos → detect; non-person → 0; NN-time gate ≤ 30 ms avg |
| 10 Camera passthrough | T10.1–T10.3 | `camera awb / exposure / gain` |

47 tests today. Latest run: `results/test-report-*.html`.

## Test images

`tests/images/` ships 15 deterministic samples sourced from `skimage.data` (NASA PD + scikit-image's BSD-licensed sample set):

| File | Source | Has person? |
|---|---|---|
| `astronaut.jpg` | skimage.data.astronaut (NASA PD, Eileen Collins) | ✓ |
| `camera.jpg` | skimage.data.camera (the classic "cameraman") | ✓ |
| `cat.jpg` / `chelsea.jpg` | skimage.data.cat / chelsea | ✗ (cats) |
| `coffee.jpg` | skimage.data.coffee | ✗ |
| `moon.jpg` / `rocket.jpg` / `hubble_deep_field.jpg` | skimage data | ✗ |
| `clock.jpg` / `colorwheel.jpg` / `logo.jpg` / `page.jpg` | skimage data | ✗ |
| `cell.jpg` / `grass.jpg` / `gravel.jpg` | skimage data | ✗ |

To extend with your own images, drop them in `tests/images/` and adjust the `person_imgs` / `non_imgs` lists in `tests/run_tests.py group_algorithm()`.

## Multi-class (vehicle) detection — current status

**Multi-class is live on master.** The firmware now ships the 80-class
`yolov8n_relu30` model (compiled with stedgeai 4.0 + matching LL_ATON
runtime) and both people and vehicle detections fire end-to-end on the
kit at ~42 ms / inference. The test suite's GROUP 11 covers
`cars_15.jpg`, `cars_18.jpg`, and `13ppl_5trucks.jpg`; the embedded
detection images in the HTML report show the actual bounding boxes the
kit returned.

What's still soft is **detection quality, not pipeline plumbing**: at
192×192 the model often misclassifies cars into the adjacent COCO
transport categories (airplane / train / boat). The App-side filter in
`nn_task.c::_class_passes_mask` treats the whole transport-vehicle
band (COCO 2/3/4/5/6/7/8) as one bucket so the W6 SoW "is there a
vehicle?" semantic works as expected. Confidences in this quantised
multi-class model cap around 0.30–0.34 — see `MULTICLASS_STATUS.md` in
the repo root for the longer story about why the pristine pretrained
weights deadlock the ATON runtime and why we ended up on the 30-epoch
fine-tuned variant.

See the section below for the historical journey through PTQ variants;
the conclusion (point 2 — light fine-tuning) is what is now deployed.

### Pipeline status

All the toolchain pieces are working end-to-end:

| Step | Tool | Working? |
|---|---|---|
| Float ONNX export from Ultralytics | `yolo export model=yolov8n.pt format=onnx imgsz=192` | ✅ |
| INT8 quantization with calibration | `tools/quantize_yolov8n.py --samples 128` | ✅ |
| Compile to ATON code + weights | `stedgeai generate --model … --target stm32n6 --st-neural-art …` | ✅ |
| Push App + model to kit over UART | `./modular-tools.sh update [app\|model]` | ✅ |
| Detection accuracy on real photos | (model dependent) | ❌ |

### What we tried (this session)

Starting from Ultralytics' `yolov8n.pt` (full 80-class COCO), exported to ONNX at 192×192, then four quantization variants through `tools/quantize_yolov8n.py`:

| Variant | Calibration | Method | Quantized ops | Model size | NN time | Detection on astronaut |
|---|---|---|---|---|---|---|
| v0 (vendor's PeopleNet, 1 class) | (ST internal) | (ST internal) | — | 3.0 MB | **17 ms** | ✅ conf 0.9+ |
| v1 (PTQ, small) | 16 imgs | MinMax | Conv/MatMul/Mul/Add | 3.5 MB | **42 ms** | ⚠️ conf 0.09 (faint hit) |
| v2 (PTQ, more ops) | 128 imgs (COCO128) | Entropy | + MaxPool/Resize/Concat/Split/Sigmoid | 3.4 MB | 486 ms | ❌ 0 detections |
| v3 (PTQ, narrow ops) | 128 imgs | MinMax | Conv/MatMul only | 3.4 MB | 491 ms | ❌ 0 detections |
| v4 (PTQ, broad ops) | 128 imgs | MinMax | Conv/MatMul/Mul/Add | 3.5 MB | wedged | ❌ NN init issue |

**Key takeaways:**
- The `op_types_to_quantize` set matters enormously for inference time. Too few quantised ops → most layers fall back to Cortex-M55 float → 500 ms. The right set keeps ~95% of compute on the ATON NPU.
- Adding more calibration data (16 → 128 from COCO128) didn't recover detection accuracy on this PTQ pipeline. The Q/DQ insertion + scale calibration doesn't line up with what `app_postprocess_od_yolov8.c` expects after the trained YOLOv8n weights are quantised.
- This is consistent with public reports of YOLOv8 PTQ losing 5–10% mAP on small backbones — for a single class, that's tolerable; spread over 80 classes, conf scores drop into the noise.

### How to make multi-class work for production

Three paths, in increasing effort:

1. **Ask ST for a multi-class N6 variant** — `stm32-hotspot/ultralytics` ships only `coco-person-st.tflite` today, but ST internally has multi-class deployments (the Cube AI Developer Cloud examples reference them). One support email could be the shortest path. See `EMAIL_TO_SYLVAIN.md` for a draft.

2. **QAT on a GPU** — fine-tune `yolov8n.pt` with quantisation-aware training, then export to INT8 ONNX, then `stedgeai`. Preserves 70–80% of the float mAP. Tooling: `ultralytics` + `pytorch-quantization`. Runs in ~10 min on a modern GPU. We have a Jetson Orin available at the office for this.

3. **ST Cube AI Developer Cloud** — upload float ONNX to https://stedgeai-dc.st.com, the server runs QAT on its end, downloads a working quantised model. Needs an ST account login.

### What's already in the firmware for multi-class

- `AI_OD_YOLOV8_PP_NB_CLASSES` in `app_config.h` controls the class count — bump from 1 to 80 (or however many the new model has) and rebuild.
- `SAI_CLASSES[]` table — add display names + colours for the new classes (person + car + motorcycle + bus + truck are pre-staged in a commit comment).
- `nn_task_det_set(mask)` filters by class bitmask: bit 0 = person (COCO 0), bit 1 = vehicles (COCO 2/3/5/7).
- `detect profile <det_msk> <act_msk>` persists the mask via registry.
- `run_tests.py group_algorithm` already understands `car=N`, `truck=N`, etc. — when a vehicle photo returns those classes, the regression table prints them automatically.

The moment a working `network_data.hex` is in flash and `NB_CLASSES` matches, vehicles light up — no additional code changes.
