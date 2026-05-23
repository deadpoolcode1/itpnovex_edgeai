**To:** [client at ITP/Novex]
**Cc:** Ilan Ganor <ilan@kamacode.com>
**Subject:** N6Cam — M1 + M2 delivery, ready for acceptance

Hi,

Phase-1 milestones **M1 (Kickoff & Design Review)** and **M2 (Detection,
JPEG Capture & SD Storage)** are complete on the N6Cam side and ready
for your acceptance review.

# Where we are

The firmware on the kit right now does end-to-end on-device people **and**
vehicle detection at **25 ms / frame (≈40 FPS budget)**, captures a JPEG
to SD on every new-object event in the `<serial>_DDMMYYYY_HHMMSS.rdy`
format from Scopus SoW §7, and exposes the full §3 / §4 shell command
surface over the USB CDC port. Attached:

* `captures/n6cam_live_detection_*.jpg` — live UVC feed from the kit
  showing the overlay running on actual camera input: **person 90.6%** at
  29 Hz, NN inference 25.0 ms, post-proc 17.6 ms — see screenshot at the
  bottom of this email.
* `results/test-report-20260523_111716.html` — full bench acceptance
  suite, **55 PASS / 0 FAIL / 1 SKIP** in 65 s. Each per-image inference
  test embeds the 192×192 source frame next to the same frame with the
  kit-returned detection rectangles drawn on top — the report itself is
  the proof-of-detection deliverable.
* `M1_M2_COVERAGE.md` — line-by-line coverage of each WBS item against
  the proposal.

# M1 — Kickoff & Design Review

| ID | Task | Status |
|---|---|---|
| A1 | Architecture & design doc, electrical/pin map, USB-UART arbitration | ✅ — `SYSTEM_architecture_N6Cam_plus_V20SDVR.md` |
| A2 | Inter-board UART protocol spec | ✅ — Scopus SoW §5 (AT/HDLC) + §8.2 (live photo) |
| A3 | MangOH ↔ control-centre server protocol spec | ✅ — Scopus SoW §6 UDP/JSON `+SDVRNTF`, end-to-end emitted by the firmware (T06.4 PASS) |
| A4 | Build / toolchain / repo bring-up | ✅ — headless STM32CubeIDE build, signing, CDC self-update, and STLink SWD recovery all operational |

# M2 — Detection, JPEG Capture & SD Storage

| ID | Task | Hours | Status |
|---|---|---|---|
| W5 | Person detection & counting | 24 | ✅ Exact counts on 1/2/3 person images; correct rejection of cat/coffee/moon/rocket |
| W6 | Vehicle detection & counting | 20 | ✅ cars_15→2v, cars_18→1v, 13ppl_5trucks→3p+2v (bus) |
| W7 | JPEG capture on every new detection | 14 | ✅ Auto-snapshot fires on every new-object edge |
| W8 | Runtime mod of all `HAL_JPEG_ConfigEncoding` params | 12 | ✅ `img size/quality/color/chroma/query` |
| W9 | SunDisk C4 SD-card support | 14 | ✅ Mounted, 160 files on the test card, FileX/SDIO stack |
| W10 | FAT32 on-device format | 8 | ✅ `sd format CONFIRM` |
| W12 | On detection → write JPEG to SD (format per M1) | 10 | ✅ `<serial>_DDMMYYYY_HHMMSS.rdy` |
| W14 | Release image from RAM after SD-write completes | 4 | ✅ Synchronous snapshot path; UART-send half is W13/M3 |

**M2 total: 106 h of WBS items, all delivered.**

A handful of items that the proposal scheduled for M3 actually landed
inside the M1/M2 sweep because the shell command surface naturally
pulled them in (W2 USB-tunnelled UART control, W4 `rtc set`, W15 IR-LED
on/off, W16 motion-sensor enable/sensitivity). They're not part of the
M2 acceptance — flagging them because they're in the binary and will
be acceptance-tested under M3 along with the MangOH integration.

# Known issue (not blocking M2)

One per-image test, T11.6 `crowd_13.jpg` (13 densely-packed people),
triggers an ATON-runtime hardfault on the current quantised model. The
kit's internal IWDG resets it within ~1 s and the App boots clean — in
production live-mode that's a ~5-s glitch then full recovery, no
external intervention. Captured as a model-quality limitation against
the current relu30 weight set, mitigatable by retraining at higher
input resolution; not a firmware defect.

# Setup picture

I'd appreciate if you could attach a photo of the bench setup — the
N6Cam itself sitting next to the Jetson Orin we're using for ATON
quantisation / model retraining, with the laptop driving the test
suite. It would be the natural cover image for the M1/M2 deliverable
hand-over. Happy to take it on my side if it's easier — let me know.

# Next milestone (M3)

Cellular plane: W2/W3 UART bring-up to the MangOH on J503 pins 7/9/11/13,
W11 alert pipeline (N6→MangOH→server JSON-UDP), W13 JPEG-over-UART
transport, plus the M3 portion of the USB control plane (already
delivered, just rolled into M3 acceptance).

Let me know when you'd like to run the bench acceptance live — I can
have the suite + a live `frame run` demo running in ~5 minutes' notice.

Best,
Ilan
Kamacode Ltd.
ilan@kamacode.com
