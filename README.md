# EdgeAI — SIANA N6Cam People-Counter Firmware

Custom firmware for the SIANA N6Cam (STM32N657, IMX335 5MP camera + Neural-Art NPU) per the Scopus PoC SoW. Streams H.264 over USB-C UVC, runs on-device **person detection** (NN at ~17 ms / frame on the ATON NPU), captures JPEGs to SD, and (when wired) tunnels alerts + photos to a WP76 modem over UART.

**Detection status:** the firmware-side wiring supports both people and vehicle classes (bitmask filter, COCO classes 0/2/3/5/7). The model currently in flash is the vendor's `yolov8n_192_quant_pc_uf_od_coco-person-st.tflite` (person-only). Multi-class deployment is mechanism-complete — `update model` over UART works end-to-end — but blocked on a production-grade multi-class quantized model for STM32N6 (see [§6 Detection model](#6-detection-model)).

| Property | Value |
|---|---|
| USB VID:PID | `0483:5740` (UVC 1.50 + CDC-ACM) |
| Resolution | 800×600 H.264 30 fps |
| Camera serial console | `/dev/ttyACM1` @ 115200 8N1 |
| STLink VCP (trace) | `/dev/ttyACM0` @ 115200 8N1 |

## 1. Connect and view the live stream

```bash
sudo apt-get install -y v4l-utils ffmpeg mpv
# Plug the kit's USB-C into the laptop. Wait 3s.

VIDEODEV=/dev/v4l/by-id/usb-STMicroelectronics_N6Cam_*-video-index0
ffmpeg -hide_banner -loglevel warning -f v4l2 -input_format h264 \
       -video_size 800x600 -framerate 30 -i $VIDEODEV \
       -c copy -f h264 - | \
mpv --profile=low-latency --demuxer-lavf-format=h264 -
```

Press `q` to quit. For a still frame instead of live: `… -frames:v 1 -update 1 -y frame.jpg`.

## 2. Talk to the firmware

The kit exposes a CDC-ACM shell on `/dev/ttyACM1`. Use any serial terminal at 115200 8N1, or send one-shot commands:

```bash
N6CAM_TTY=$(readlink -f /dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02)
stty -F $N6CAM_TTY 115200 raw -echo
(timeout 3 cat $N6CAM_TTY) & sleep 0.5
printf "help\n" > $N6CAM_TTY
wait
```

### Shell commands

| Command | What it does |
|---|---|
| `help` | List all commands. |
| `fw` / `hw` / `uid` / `uptime` | Built-ins: firmware version, hardware rev, MCU UID, uptime ms. |
| `version` | Application version string. |
| `reboot` | Soft reboot. |
| `echo on\|off\|query` | Terminal echo + prompt (off = scripted/silent). Persists in flash. |
| `rtc` | Print current RTC time. |
| `rtc set DDMMYYYYHHMMSS` | Set RTC. Example: `rtc set 22052026140000` = 2026-05-22 14:00:00. |
| `camera flip [H\|V\|off]` | Image flip. |
| `camera aec [value\|off]` | Auto-exposure compensation, range `-2.0..2.0`. |
| `camera awb [value\|auto]` | White-balance profile, `0..5`. |
| `camera gain [value]` | Analog gain `0..72000` mdB. |
| `camera exposure [value]` | Shutter `0..33000` µs. |
| `camera brightness [value]` | Brightness `0..100`. |
| `irled on\|off\|query` | IR LED state. (PoC: drives LED_USER3 until real GPIO wired.) |
| `motion sense <0..100> <timeout_s>` / `motion query` | Motion sensitivity + no-motion timeout. Persists. |
| `img size H W` | Photo dimensions, persists. |
| `img quality <1..100>` | JPEG quality, persists. |
| `img color YCBCR\|RGB\|CMYK` | Color space, persists. |
| `img chroma 0\|1` | Chroma subsampling — 0=4:4:4, 1=4:2:2. Persists. |
| `img query` | Print current photo settings. |
| `detect start\|stop` | Run/halt on-device people detection. Persists across reboots. |
| `detect profile <det_msk> <act_msk>` | det_msk bit0=people bit1=vehicles; act_msk bit0=save SD bit1=cellular report. Persists. |
| `detect profile query` | Print current detection profile. |
| `notify enable <mask>` / `disable` | Notification bitmask: `0x1`=NetReg `0x2`=MotStart `0x4`=MotStop `0x8`=Periodic `0x10`=People `0x20`=Vehicle. |
| `notify period <s>` | Periodic-notification interval. |
| `notify trigger <code>` | Emit one notification now with `rsn=code`. |
| `notify query` | Current notify state + message count. |
| `photo savesd` / `photo upload` | Capture JPEG → SD card (`savesd`) or modem (`upload`, needs WP76). Filename `serial_DDMMYYYY_HHMMSS.rdy`. |
| `recovery` | Reboot into FSBL recovery halt (limited use on this kit — debug auth locks SWD in op mode regardless). |
| `update [app\|model]` | Receive new App firmware (default) or NN model weights over CDC and reflash xSPI. Used by `n6cam-update.py` — you won't run this by hand. |
| `frame upload\|load\|run\|clear\|query` | Test-frame injection — push a 192×192 RGB image into the NN input from the host, run inference, read back detections. Used by the regression suite to validate the algorithm without depending on the lens. |
| `sd query\|ls\|format CONFIRM` | SD card status, root listing, destructive reformat (FAT32). |

Successful commands respond `<cmd> [<sub>] ok`. Notifications appear on the same port as `+SDVRNTF: {"ser":...,"num":...,"rsn":...,...}` (SoW §6 JSON).

## 3. Build the firmware

Toolchain:

```bash
# STM32CubeIDE 1.18+ (bundles the right ARM GCC 13.3); 1.19 tested
# STM32CubeProgrammer 2.18+ (signing + SWD flash tool); 2.21 tested
# Both installable as native x86_64 from st.com.
# Default paths used below:
CUBEIDE=/opt/st/stm32cubeide_1.19.0
CUBEPROG=$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer
```

The entire SIANA BSP is vendored at `vendor/n6cam.core.bsp/` with our additions — no separate fetch step.

```bash
# 1. Build (~30s first time, ~2s incremental). Outputs Application.bin + .elf.
WS=/tmp/cubeide_ws_n6cam
mkdir -p $WS
$CUBEIDE/stm32cubeide --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data $WS \
  -import vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application \
  -build "Application/Release"

# 2. Sign (STM32N6 rejects unsigned firmware). Outputs Application_signed.bin.
export PATH="$CUBEPROG/bin:$PATH"
STM32_SigningTool_CLI \
  -bin vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application.bin \
  -nk -of 0x80000000 -t fsbl -hv 2.3 -align \
  -o   vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin
```

To build the FSBL too, use `-import vendor/.../STM32CubeIDE/FSBL -build "FSBL/Release"` and sign its output the same way. **You rarely need to** — once the recovery-capable FSBL is in flash, only the Application changes during normal iteration.

## 4. Flash the firmware

### Daily path: USB-C self-update (no SWD, no boot switch)

```bash
# App update (default):
./modular-tools.sh update
# or directly:
./n6cam-update.py vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin

# Model update (NN weights, e.g. after re-running stedgeai on a new model):
./modular-tools.sh update model
# or with explicit path:
./n6cam-update.py --target model vendor/n6cam.core.bsp/Firmware/Model/network_data.hex
```

The scripts open the kit's CDC port, trigger the `update [app|model]` shell command, stream the signed payload (auto-converting Intel HEX → binary on the host if needed), and the kit reboots. App = ~5 s, model = ~30–60 s (large erase). Both targets work without SWD or the boot switch.

### Recovery / FSBL path: SWD via STLink-V3 (boot switch needed)

Only required when changing the FSBL itself, or when the Application is bricked enough that USB-C won't enumerate.

1. Set the kit's boot switch to **development mode**, power-cycle (unplug **all** cables to the kit for ≥2s, replug).
2. Run:
   ```bash
   export PATH="$CUBEPROG/bin:$PATH"
   EL="$CUBEPROG/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"
   STM32_Programmer_CLI -c port=SWD mode=HOTPLUG ap=1 -el "$EL" \
     -w vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/FSBL/Release/FSBL_signed.bin 0x70000000 \
     -w vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin 0x70400000 \
     -w vendor/n6cam.core.bsp/Firmware/Model/network_data.hex
   ```
3. Set the boot switch back to **operation mode**, fully power-cycle again.

Why two paths? STM32N6's Debug Authentication locks the SWD access ports while the FSBL is running in op mode, so SWD only works from dev-mode boot. The CDC self-updater is in-application — it writes xSPI flash from inside the running App, bypassing SWD entirely. See `memory/project_n6cam_debug_auth_locked.md` for the full story.

## 5. Test suite + HTML report

A comprehensive regression suite drives the kit's CDC shell and exercises every SoW command, the SD photo pipeline, frame injection, the NN algorithm, and inference performance. See [`tests/README.md`](tests/README.md) for the full breakdown.

```bash
./modular-tools.sh test
```

Latest baseline: **50 / 56 PASS in ~100 s, NN inference ~42 ms / frame** on the multi-class people+vehicles model. Output goes to `results/test-report-<timestamp>.html` (and `.pdf` if `wkhtmltopdf` or headless Chrome is on PATH) in the same format as the V20_SDVR reports.

Per-image inference tests (groups 9 + 11) embed the 192×192 source frame next to a copy with the device-returned detection rectangles drawn on top, so the report is a self-contained record of what the kit actually saw and what it labeled. The image pairs land in `results/test-report-<timestamp>_artifacts/` next to the HTML.

## 6. Detection model

The model in flash today is `yolov8n_relu30` — 80-class COCO YOLOv8n, lightly fine-tuned on COCO128, INT8 on the ATON NPU at 192×192 input, ~42 ms / inference. People and vehicle classes both fire end-to-end via the firmware's expanded class filter (`nn_task.c::_class_passes_mask` treats COCO 2/3/4/5/6/7/8 — car/motorcycle/airplane/bus/train/truck/boat — as one transport bucket because the 192×192 quantised model frequently misclassifies vehicles into adjacent categories). The historical 1-class model (`yolov8n_192_quant_pc_uf_od_coco-person-st.tflite`, 17 ms / frame) is preserved at `vendor/n6cam.core.bsp/Firmware/Model/_backup_person_only/` if you ever need a smaller / faster single-class baseline.

The firmware-side wiring is multi-class capable — `detect profile <det_msk> <act_msk>` filters by class bitmask (bit 0 = people, bit 1 = vehicles → COCO 2/3/5/7), and the regression suite already understands `car=N` / `truck=N` / etc. The moment a multi-class quantized model lands in `vendor/.../Model/network_data.hex`, vehicles light up automatically with no firmware code changes — just bump `AI_OD_YOLOV8_PP_NB_CLASSES` from 1 to N and re-run `./modular-tools.sh build && ./modular-tools.sh update` + `./modular-tools.sh update model`.

The full pipeline for producing a custom model is committed under `tools/quantize_yolov8n.py` (ONNX Runtime static quantization with COCO128 calibration). What's blocking production multi-class today is **model accuracy after PTQ** — Ultralytics' post-training quantization on `yolov8n.pt` collapses confidence scores when fed through `stedgeai → ATON → vendor's app_postprocess_od_yolov8`. Production-grade accuracy requires either QAT (quantization-aware training on a GPU) or a vendor-blessed multi-class N6 model from ST. See `tests/README.md` for the experimental results and what each variant produced.

## 7. Troubleshooting

- **`lsusb` shows nothing for the kit after plug-in** — cable is charge-only (no data lines), or you plugged into a power-only port. Try a different USB-C cable / port directly on the laptop, not the dock.
- **`Device or resource busy` on `/dev/videoN`** — another process holds the V4L2 device. UVC is single-reader; close mpv/ffmpeg first.
- **`non-existing PPS 0 referenced` from ffmpeg at start** — H.264 decoder warming up before the first I-frame. Harmless; video starts within ~1s.
- **`/dev/ttyACM1` is permission-denied right after self-update** — race during USB re-enumeration. Wait 5–10s and re-`stty`. Future re-runs are fine.
- **`STM32_Programmer_CLI` says "Cannot connect to access port"** — chip is in op mode (FSBL is running and debug auth has engaged). Flip the boot switch to dev mode and full power-cycle.

## Reference

- **`Scopus_SoW_v3.pdf`** — Scopus PoC system spec (this firmware implements §3.1, 3.4, 3.5, 3.7, 3.8, 4.1–4.5, partial §6).
- **`Kamacode_N6Cam_Proposal_v4.docx`** — WBS-based cost proposal + milestone plan.
- **`SIANA.N6Cam_Using the N6Cam Viewer_250306-1430.pdf`** — vendor viewer GUI (Windows build works; Linux build broken on Ubuntu 24.04 Wayland; use `ffmpeg | mpv` instead).
- **`e2ip EdgeAI_Datasheet.pdf`** — hardware spec, pinout, sensor list.
- **`vendor/n6cam.core.bsp/Documentation/SIANA.N6Cam.*_Schematics-RevB4-PVT.pdf`** — full kit schematics (camera, compute, IO).
- **`memory/`** — long-form engineering notes on debug-auth lock, flash procedure, and the self-update internals.

## Source layout

```
modular-tools.sh           # one-liner workflows: setup / build / flash / update [app|model] / test / demo-* / doctor
n6cam-update.py            # host-side self-update script (CDC, supports --target app|model)
n6cam-inject-frame.py      # host-side test-frame inject (any image → 192x192 RGB → NN)
n6cam-regress.py           # standalone batch detection-regression script (older; superseded by tests/run_tests.py)
n6cam-prep-tests.py        # convert folder of images → .raw files for SD-based testing
tests/
  run_tests.py             # main test runner (CDC shell + image inject + HTML report)
  README.md                # what each test group covers, image sourcing, multi-class workflow
  images/                  # 15 deterministic test photos (skimage / NASA PD)
tools/
  quantize_yolov8n.py      # ONNX → ORT static INT8 quantization for stedgeai
results/                   # test-report-<timestamp>.html + .pdf (V20_SDVR format)
captures/                  # sample JPEGs from the running kit (gitignored)
vendor/n6cam.core.bsp/     # SIANA BSP snapshot with our additions
  Firmware/
    FSBL/Core/Src/main.c                       # recovery-magic hook
    Application/Core/Src/Tasks/shell_task.c    # all our shell commands + self-updater
    Application/Core/Src/Tasks/nn_task.c       # detect gate + class filter + suspend/resume
    Application/Core/Src/Tasks/snapshot_task.c # JPEG → SD via FileX (vendor)
    Application/Core/Src/fx_app.c              # FileX SD mount/read/write (vendor + our helpers)
    Application/Core/Inc/registry.h            # V3 persistent settings
    Model/network_data.hex                     # Neural-Art model weights (currently PeopleNet)
    Model/_backup_person_only/                 # vendor's 1-class model artifacts (recovery baseline)
```

## Vendor links

- Product: https://www.siana-systems.com/n6cam · https://e2ip.com/edge-ai-sensing-kit/
- Source upstream: https://bitbucket.org/sianasystems/n6cam.core.bsp
- Wiki: https://siana-systems.atlassian.net/wiki/spaces/N6Cam
- Vendor support: `n6cam.support@siana-systems.com`
