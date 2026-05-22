# Statement of Work — N6Cam People-Counter Firmware

**Platform:** SIANA N6Cam Edge AI Sensing Kit
**Date:** 2026-04-24
**Requester:** Ilan Ganor (ITP / Novex)
**Vendor:** SIANA Systems — Sylvain Bernard

---

## 1. Scope

Develop custom application firmware for the N6Cam that performs real-time
people counting, captures images on count changes, logs events to the on-board
SD card, signals events on GPIO, gracefully handles live SD-card removal via a
GPIO interlock, and supports remote firmware update over UART.

Hardware is unchanged — firmware only. Mechanical, optical, and power design
are out of scope.

---

## 2. Functional Requirements

### FR-1 — People Detection & Count Tracking
- Run the on-device person-detection inference continuously on the live camera
  stream.
- Maintain the current instantaneous person count `N`.
- Detect count-change events (`N_prev → N_new`, where `N_new ≠ N_prev`) with
  configurable debounce to suppress detector flicker.
- Configurable inference rate (target: ≥ 5 FPS, to be confirmed against the
  chosen model).

### FR-2 — Image Capture on Change
- On every debounced count-change event, capture one still frame from the
  camera.
- Frame format, resolution, and compression (JPEG preferred) shall be
  configurable at build time.

### FR-3 — Event Logging to SD Card
- Persist each event to the SD card as:
  - An image file (unique, timestamped filename).
  - An entry in an append-only event log (JSONL or CSV) containing:
    - UTC timestamp
    - Previous count, new count, delta
    - Image filename
    - Monotonic event ID
- Directory layout, retention policy, and filesystem (FAT32 assumed) to be
  agreed during design review.

### FR-4 — Event Signal GPIO (Output)
- On each logged event, emit a pulse on a dedicated output GPIO.
- Pulse width, active level, and pin assignment configurable.
- Pulse must be emitted regardless of whether the SD card is currently
  writable (see FR-5).

### FR-5 — SD-Card Interlock GPIO (Input)
- A dedicated input GPIO indicates the operator's intent to remove the SD card
  (**interlock asserted**).
- While the interlock is asserted:
  - No writes may be issued to the SD card.
  - Any in-flight write must complete and the filesystem be cleanly
    flushed/unmounted before the host signals "safe to remove" (mechanism to
    be defined — e.g. a second output GPIO ACK, optional).
  - New events continue to be generated (FR-1, FR-2, FR-4) and are buffered in
    RAM in a bounded FIFO.
- While the interlock is de-asserted (card present and writable):
  - The FW mounts the SD card if not already mounted.
  - All buffered events are drained to the SD card in chronological order
    before new events are written directly.
- Buffer depth, per-event RAM footprint, and overflow policy (drop-oldest vs.
  drop-newest, with a counter of dropped events logged once writes resume) to
  be agreed during design review.
- Debounce on the interlock input is required.

### FR-6 — Firmware Update over UART
- Provide an in-field firmware update path over the UART interface for remote
  servicing.
- Requirements:
  - Bootloader (or resident update agent) able to receive a new FW image over
    UART and program it to flash.
  - Integrity check on the received image (CRC32 minimum; signed image
    preferred — to be decided).
  - A/B or golden-image fallback so that a failed/interrupted update does not
    brick the device.
  - Host-side flasher utility (CLI, Linux) delivered with the FW.
  - Documented protocol (XMODEM/YMODEM/custom — vendor's recommendation
    welcome).
  - Configurable UART baud rate; target ≥ 115200.

---

## 3. Configuration & Build

A single header or config file shall expose at least:
- Inference rate and debounce window
- Image resolution and JPEG quality
- GPIO pin assignments and active levels (event out, interlock in, optional
  "safe to remove" out)
- Event-out pulse width
- RAM buffer depth for FR-5
- UART port and baud rate for FR-6

---

## 4. Deliverables

1. Firmware source code, build scripts, and toolchain instructions sufficient
   to reproduce the release binary on a clean Linux host.
2. Release FW binary (signed if applicable).
3. Bootloader / update agent binary.
4. Host-side UART flasher utility + usage docs.
5. Integration document:
   - GPIO pinout and electrical expectations
   - Event-log file format
   - SD-card directory layout
   - UART update protocol specification
6. Acceptance test procedure (see §6) executable on a bench setup.
7. Known-issues / limitations note.

---

## 5. Assumptions & Open Questions

The following must be resolved during a kickoff / design review before
development starts:

- **Detection model:** Reuse the stock SIANA person-detection model, or
  deliver a custom one? Expected accuracy and FPS on this SoC?
- **Image pipeline:** Maximum capture resolution while inference is running;
  JPEG encoder availability on-device.
- **Free GPIOs:** Confirm which GPIO pins on the N6Cam connector are available
  for FR-4 (event out) and FR-5 (interlock in), and their electrical levels.
- **SD filesystem:** FAT32 assumed. Confirm driver supports safe unmount and
  survives abrupt removal without corruption of *already-written* files.
- **RAM budget:** Available heap for FR-5 buffering while inference is
  running.
- **UART availability for updates:** Is the same UART used for the console /
  debug? Shared or dedicated? Update mode entry (magic sequence, GPIO strap,
  command)?
- **Update security:** Plain CRC, or signed images with on-device public key?
- **Time source:** Is there an RTC, or are timestamps monotonic-only until
  set by an external source?
- **Power / boot:** Cold-boot time budget; behavior on unexpected power loss
  mid-write.

---

## 6. Acceptance Criteria

1. With a known test sequence of people entering/leaving the frame, the FW
   logs an event on every count change and no spurious events under a defined
   lighting/background condition (detailed metric TBD).
2. Every logged event produces exactly one pulse on the event-out GPIO.
3. SD-card removal scenario:
   - Assert interlock, generate ≥ *N* events (N = agreed buffer depth),
     de-assert interlock → all events appear on SD in order, with no
     corruption of the filesystem.
   - Exceed buffer depth → overflow counter logged, no crash.
4. Firmware update:
   - Successful update over UART from a known-good image to a new image,
     verified by version string readback.
   - Interrupted update (cable pulled mid-transfer) → device recovers to a
     working image on next boot.
5. Soak test: continuous operation for ≥ 24 h without memory leak or
   SD-write degradation (metric TBD).

---

## 7. Not In Scope

- Mechanical / enclosure changes.
- Cloud connectivity, Wi-Fi, or networking stacks.
- Face recognition or identity tracking — this is anonymous count only.
- Host-side UI beyond the CLI flasher.

---

## 8. Next Steps

1. Vendor review of §2 and §5.
2. Joint resolution of open questions.
3. Quote and schedule.
4. Kickoff → design review → implementation → acceptance.
