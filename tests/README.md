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

## Multi-class (vehicle) detection

The default model `vendor/n6cam.core.bsp/Firmware/Model/yolov8n_192_quant_pc_uf_od_coco-person-st.tflite` is **person-only** — `detect profile 2 X` (vehicles bit) will always return 0 detections regardless of input.

To extend to vehicles (proposal W6):

1. Install `stedgeai` from ST (free, requires ST account). The downloaded `stedgeai-lin.zip` extracts to a Qt installer; run it with:
   ```bash
   /tmp/stedgeai-linux-onlineinstaller --root ~/STMicroelectronics/stedgeai \
     --accept-licenses --confirm-command --default-answer install stedgeai0300
   ```
2. Source a multi-class quantized YOLOv8n model:
   - **ST's stm32-hotspot/ultralytics repo only ships person-only variants** (`coco-person-st.tflite`). Their listed `map_52.36` ONNX still has 1-class output (we checked).
   - Path forward: take Ultralytics `yolov8n.pt` (default COCO 80-class), export to ONNX (`yolo export model=yolov8n.pt format=onnx`), quantize with stedgeai's `--quantize` flag against a calibration dataset.
3. Run the generate step (see `vendor/n6cam.core.bsp/Firmware/Model/generate.sh`):
   ```bash
   export PATH=$HOME/STMicroelectronics/stedgeai/3.0/Utilities/linux:$PATH
   stedgeai generate --model your_multi_class_model.tflite --target stm32n6 \
            --st-neural-art default@user_neuralart.json
   ```
4. Reflash `network_data.hex` to xSPI offset `0x70600000` (the SLOT1_WEIGHTS region). Either:
   - Switch to dev mode + SWD: `STM32_Programmer_CLI -c port=SWD mode=HOTPLUG ap=1 -el "$EL" -w network_data.hex` ; or
   - Extend `n6cam-update.py` to also accept a weights payload (~30 min of work).
5. Update `vendor/n6cam.core.bsp/Firmware/Application/Core/Inc/app_config.h`:
   - `AI_OD_YOLOV8_PP_NB_CLASSES` from `1` to the new class count
6. Rebuild + push the Application via `./modular-tools.sh update`.

The firmware-side wiring is already done:
- `nn_task_det_set(mask)` filters boxes by class: person (COCO 0) when bit0, vehicles (COCO 2/3/5/7) when bit1.
- `detect profile <det_msk> <action_msk>` persists the mask via registry.
- `run_tests.py group_algorithm` already understands `car=N`, `truck=N`, etc. — when a vehicle photo returns those classes, the regression table prints them automatically.

So the moment the new model is in flash, vehicles light up; no additional code changes.
