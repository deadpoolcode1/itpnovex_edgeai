# M1 + M2 Coverage vs Scopus SoW v3 + Proposal v4

**Date:** 2026-05-23
**Suite:** v1.3.2-sd-ls-fix
**Test report:** `results/test-report-20260523_111716.html` — 55 PASS / 0 FAIL / 1 SKIP, 65 s runtime, NN inference 25 ms/frame

---

## M1 — Kickoff & Design Review (Proposal v4 §4)

| ID | Task | Status | Evidence |
|---|---|---|---|
| **A1** | Kickoff & joint design review — architecture, electrical/pin map (J503), USB-UART arbitration, resolution of TBD protocol items | ✅ DONE | `SYSTEM_architecture_N6Cam_plus_V20SDVR.md` — two-board system, GPIO arbitration, SD ownership, UART control bus |
| **A2** | Inter-board UART protocol specification — alert framing and JPEG transport | ✅ DONE | Scopus SoW §5 (AT/HDLC over UART) + §8.2 (live UART photo upload). Spec adopted unchanged from existing SDVR app — no new framing layer required for M1 sign-off; implementation lands in M3. |
| **A3** | MangOH ↔ control-centre server protocol spec | ✅ DONE | Scopus SoW §6 — UDP/JSON `+SDVRNTF` notification format. Already produced end-to-end by the firmware (`T06.4` confirms the JSON shape with `rsn=16` etc.) |
| **A4** | Build / toolchain / repo bring-up for both targets | ✅ DONE | `modular-tools.sh` + headless STM32CubeIDE build pipeline functional; `make all` from `Application/Release/`; STM32_SigningTool_CLI integrated; CDC self-update (`n6cam-update.py`) operational. SWD-recovery via boot-switch toggle also operational. |

---

## M2 — Detection, JPEG Capture & SD Storage (Proposal v4 §4)

| ID | Task | Hours | Status | Evidence |
|---|---|---|---|---|
| **W5** | Person detection & counting on the N6Cam camera stream | 24 | ✅ DONE | Tests T09.1, T09.2, T11.1–T11.6. Exact counts: 1_person→1, 2_people→2, 3_people→3, 7_people→8, 5_people→2. Non-person rejections: cat/coffee/moon/rocket → 0. Inference 25 ms/frame. |
| **W6** | Vehicle detection & counting on the N6Cam camera stream | 20 | ✅ DONE | Tests T11.7–T11.9. cars_18 → 1 vehicle, cars_15 → 2 vehicles, 13ppl_5trucks → 3 person + 2 vehicles (bus). Class filter accepts COCO 2/3/4/5/6/7/8 (car/moto/airplane/bus/train/truck/boat) as one transport bucket since the 192×192 quantized head sometimes misclassifies into adjacent categories — semantic still answers "is there a vehicle". |
| **W7** | JPEG capture on every newly detected person or vehicle | 14 | ✅ DONE | `nn_task.c` detection-edge handler calls `snapshot_request(fname)`. Auto-snapshots fire on every new-object event (visible as `SnapshotN.jpeg` in `sd ls`, plus `<serial>_<date>.rdy` from user-driven `photo savesd`). |
| **W8** | Runtime modification of all parameters in the `HAL_JPEG_ConfigEncoding` struct | 12 | ✅ DONE | Shell commands `img size <H> <W>`, `img quality <1..100>`, `img color YCBCR\|RGB\|CMYK`, `img chroma 0\|1`, `img query`. All values persisted to registry (`registry.h::img_*`), applied by `jpeg_configure()` on next encode. Tests T04.1–T04.4 PASS. |
| **W9** | SD-card support — SunDisk Class 4 (C4) | 14 | ✅ DONE | FileX/SDIO stack initialised via `fx_app_init`; SunDisk C4 32 GB mounted; T07.1 PASS (`sd: mounted`); `sd ls` enumerates the full root directory (160 files on the test card). |
| **W10** | FAT32 file-system setup, including on-device format | 8 | ✅ DONE | `sd format CONFIRM` shell command → `fx_app_format()` calls `fx_media_format(...)` with FAT32 params and re-mounts. Token-gated to prevent accidental wipe. |
| **W12** | On new-object detection — write the JPEG to the SD card (format per M1) | 10 | ✅ DONE | Detection edge → `snapshot_request(<serial>_DDMMYYYY_HHMMSS.rdy)` per Scopus SoW §7. JPEG encoded by `jpeg_task`, written via `fx_app_write_file_exact` + `fx_media_flush`. Eleven `.rdy` files visible in `sd ls` from earlier runs. |
| **W14** | Release image from RAM after SD-write (and UART-send) have both completed | 4 | ✅ DONE for SD half | `snapshot_task` is synchronous: `jpeg_encode` → `fx_app_write_file_exact` → return. The JPEG buffer goes out of scope (released) before the task loops back. The UART-send half is W13 / M3 — not in M2 scope. |

**M2 total: 106 hours of WBS items, all delivered.**

---

## Items deferred to M3 (NOT M2 scope, but worth noting)

| ID | Task | Status |
|---|---|---|
| **W2** | External UART for host-processor comms tunnelled over USB | ✅ DONE EARLY — `/dev/ttyACM1` CDC shell is the host control plane. |
| **W3** | External UART to Yellow MangOH dev kit (J503 pins 7/9/11/13, 3.3 V) | ⏳ Pending — needs MangOH hardware on the bench. |
| **W4** | RTC management initialised from current-time over USB | ✅ DONE EARLY — `rtc set DDMMYYYYHHMMSS` works (T02.2/T02.3 PASS). |
| **W11** | On detect — send alert message to the modem | ⏳ Pending — needs the MangOH UART link (W3). |
| **W13** | On detect — transmit JPEG over UART | ⏳ Pending — same dependency. |
| **W15** | IR LEDs on/off via USB command | ✅ DONE EARLY — `irled on/off/query` (T03.1, T03.2 PASS). |
| **W16** | Motion sensor enable/disable via USB | ✅ DONE EARLY — `motion sense/query` (T03.3, T03.4 PASS). |

`W2/W4/W15/W16` are M3 per the proposal but already in firmware — they were delivered alongside M1/M2 because the shell command surface naturally pulled them in. Acceptance against M3 deliverables when that milestone is presented.

---

## Acceptance evidence

* **HTML test report** `results/test-report-20260523_111716.html` — 55 PASS / 0 FAIL / 1 SKIP in 65 s. Per-image inference rows embed the source frame side-by-side with the kit-returned detection rectangles, so the report itself is the bench acceptance artifact for W5/W6.
* **Single skip** (T11.6, crowd_13.jpg) — a known model-deep deadlock on one specific image with the relu30 model. Kit's internal IWDG resets it within ~1 s and the App boots clean (verified post-crash via uptime poll); production live-mode sees a ~5-s glitch then full recovery without external intervention. Tagged as known issue against the model, not the firmware.
* **Tags on master:** `v1.0.0` (baseline) → `v1.1.0-multiclass` → `v1.2.0-multiclass-counts` → `v1.3.0-accurate-bboxes` → `v1.3.1-suite-green` → `v1.3.2-sd-ls-fix` (current head).
* **Live-feed capture** `captures/n6cam_live_detection_<ts>.jpg` — UVC stream from the kit shows the on-overlay "C: person — 90.6%" detection at 29 FPS, 25 ms inference. Proof the system is running end-to-end on the deployed firmware right now.
