# N6Cam Phase 1 — Integration Document

**Vendor:** Kamacode Ltd.
**Client:** ITP / Novex
**Target:** SIANA N6Cam Edge AI Sensing Kit (STM32N657 + ATON NPU)
**Date:** 2026-05-23
**Status:** M1 + M2 delivered (v1.4.x), M3 firmware-side cellular plane ready (v1.5.x), awaiting MangOH hardware for end-to-end M3 integration; M5 acceptance + soak procedure documented below.

---

## 1.  Hardware setup

```
                       USB-C (CDC shell + UART trace + power)
                       │
            ┌──────────┴───────────┐
            │   Dev host (laptop)  │
            └──────────┬───────────┘
                       │
              ┌────────┴────────┐    USART2 J503 pins 7/9/11/13
              │  SIANA N6Cam    │────────────────────────────────►  MangOH Yellow + WP76 modem
              │  STM32N657      │    115200 8N1 HDLC               (M3/M4 — hardware pending)
              │  ATON NPU       │
              │  IMX335 camera  │
              │  SD card slot   │
              └─────────────────┘
```

### 1.1 Pin map — N6Cam side (J503 header)

| Signal       | J503 pin | STM32N657 pin | Function | Direction |
|---|---|---|---|---|
| USART2 TX    | 7        | PF7 / AF7     | UART transmit to MangOH RX | OUT |
| USART2 RX    | 9        | PF6 / AF7     | UART receive from MangOH TX | IN |
| GND          | 11       | —             | Common ground | — |
| +3V3 (opt)   | 13       | —             | Logic-level reference | — |

All header logic is **3.3 V**. Match this on the MangOH side — do not drive 5 V into PF6.

### 1.2 SD card

| Field | Value |
|---|---|
| Slot | Onboard microSD (J504) |
| Tested card | SanDisk Class 4 32 GB |
| Filesystem | FAT32 |
| Format | `sd format CONFIRM` (shell command, destructive) |
| Mount | `sd query` reports `mounted` after boot |

### 1.3 USB endpoints

| Endpoint | Path on Linux host | Purpose |
|---|---|---|
| CDC ACM | `/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02` | Operator shell, CDC self-update channel |
| UVC video | `/dev/video2` (typical) | 800×600 H.264-encoded live preview from the kit |
| STLink VCP | `/dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_*-if01` | Trace UART (boot log + LINFO/LERROR output) |

The STLink VCP is the secondary read channel — the shell never echoes its trace there, but the firmware's `trace()` calls land there.

---

## 2.  Firmware architecture

The firmware is **ThreadX**-based with each subsystem in its own task. Producer/consumer wiring at the task boundary:

| Task | Priority | Owns | Talks to |
|---|---|---|---|
| `camera_task` | IMPORTANT | IMX335 + display pipeline | display, nn |
| `display_task` | IMPORTANT | H.264 encode + UVC TX | snapshot (every frame) |
| `nn_task` | CRITICAL | ATON inference + post-proc | snapshot, modem (on detect) |
| `snapshot_task` | DEFAULT | JPEG encode + dispatch | fx (SD), modem (UART upload) |
| `fx_task` | DEFAULT | FileX / SDIO | snapshot |
| `modem_task` | USER_INTERFACE | USART2 + HDLC framing | nn, shell, snapshot |
| `shell_task` | USER_INTERFACE | CDC ACM + lwshell | every other task via public APIs |
| `registry_task` | CRITICAL | NV-persistent config | shell (write), every task (read) |
| `system_task` | BACKGROUND | RTC, trace, FPS counters | trace UART |
| `watchdog_task` | CRITICAL | IWDG pet | — |

### 2.1 Detection → action pipeline

```
                        Camera                                  /dev/video2
                         │                                          ▲
                         │ frame                                    │
                         ▼                                          │
                    ┌────────┐    ┌────────┐    ┌──────────┐        │
                    │ camera │───►│   nn   │───►│ display  │────────┘
                    │  task  │    │  task  │    │   task   │
                    └────────┘    └───┬────┘    └──────────┘
                                      │ on object-count edge
                                      ▼
                              ┌───────────────────┐
                              │  snapshot_request │   ← shell `photo savesd|upload`
                              │       (atomic)    │     also flows here
                              └─────┬─────────────┘
                                    │ target=SD       target=UART
                                    ▼                 ▼
                            ┌───────────┐     ┌──────────────┐
                            │  fx_task  │     │  modem_task  │
                            │ FAT32 SD  │     │  USART2 HDLC │
                            └───────────┘     └──────────────┘
```

---

## 3.  Protocols

### 3.1 CDC shell protocol (M2 — already in production)

Text-line shell on the CDC ACM endpoint. Each command terminated with `\n` (or `\r\n`). Output lines end with `\r\n`. Echo controllable via `echo on|off`. The full surface is the Scopus SoW §4 table — see `tests/run_tests.py` for executable references. Highlights:

- `detect start|stop|profile <det> <act>|profile query|simulate [N]`
- `notify enable|disable|trigger <code>|period <s>|query`
- `photo savesd|upload`
- `img size <H> <W>|quality 1..100|color YCBCR|RGB|CMYK|chroma 0|1|query`
- `rtc set DDMMYYYYHHMMSS|rtc`
- `irled on|off|query`
- `motion sense <0..100> <timeout_s>|motion query`
- `sd query|ls|format CONFIRM`
- `frame upload|load|run|clear|query` (NN test-frame injection)
- `update app|model` (CDC self-update, see §3.4)
- `mdm <at-cmd>|test echo|test urc <line>` (modem tunnel, see §3.2)

### 3.2 Modem channel (M3 — N6 side complete, awaiting MangOH)

**Transport:** USART2 @ 115200 8N1 with HDLC framing — `0x7E` start/end flags, `0x7D` escape with `XOR 0x20`, CRC-16/XMODEM appended big-endian. See `vendor/.../Application/Core/Inc/hdlc.h` for the C contract and `tools/hdlc.py` for the host-side mirror.

**Wire shape** for an AT command:

```
                          ┌───────────────────────────────────┐
                          │ AT+CSQ\r\n  (4 bytes, big-endian   │
                          │            CRC, byte-stuffed)      │
                          └───────────────────────────────────┘
0x7E  41 54 2B 43 53 51 0D 0A  ━━ CRC-HI ━━ ━━ CRC-LO ━━  0x7E
```

If any payload or CRC byte is `0x7E` or `0x7D`, it's split into `0x7D` + (byte XOR `0x20`).

**Higher-level dispatch**: each HDLC frame is one or more CRLF-terminated lines. The `modem_task` worker:

1. Decodes the frame.
2. If a synchronous caller is waiting in `modem_send_at()`, accumulates lines into the response buffer; on `OK` / `ERROR` / `+CME ERROR:` / `+CMS ERROR:` the wait fires.
3. Otherwise (or for any `+SDVR*` line received outside a pending response), invokes the URC callback. `shell_task` installs a forwarder that re-emits the URC on the CDC shell stream so the operator sees `+SDVRRDY:`, `+SDVRUPL:`, etc. as they arrive.

**Photo upload (SoW §8.2)** uses the binary transport variant:

```
host → kit:  shell `photo upload`
kit  → modem: SDVR+SENDBIN=<ref>,"<tag>","<DDMMYYYYHHMMSS>",<ref>,<size>\r\n
kit  → modem: <size bytes of JPEG>                  (HDLC-framed, may span frames)
modem → kit: +SDVRUPL: START,"<tag>_<ref>"
modem → kit: +SDVRSRVR: OK                          (after HTTP POST succeeds)
modem → kit: +SDVRUPL: END,"<filename>"
modem → kit: OK
```

The whole `SENDBIN` + binary tail is serialised under `modem_task`'s TX mutex so a concurrent shell `mdm <at>` from the operator can't interleave its frame between the prefix and the payload.

### 3.3 Server-bound notification (SoW §6)

When the kit fires a detect event (or any other notification-capable event), `shell_task::_notify_emit()` builds a JSON object:

```json
{
  "ser":  4325412,
  "num":  10,
  "rsn":  16,
  "rsd":  3,
  "tim":  "20260523111111",
  "mtn":  0,
  "mod":  "",
  "bat":  0.0,
  "vol":  0.0
}
```

| Field | Meaning |
|---|---|
| `ser` | Kit's MCU UID (HAL_GetUIDw0) |
| `num` | Per-boot rolling notification number |
| `rsn` | Reason bitmask: 1 NetReg, 2 MotStart, 4 MotStop, 8 Periodic, 16 People, 32 Vehicle, 64 PhotoEvent |
| `rsd` | Reason-specific data (e.g. for People: detected count; for PhotoEvent: 1=SD, 2=upload) |
| `tim` | RTC timestamp `YYYYMMDDHHMMSS` |
| `mtn`/`mod`/`bat`/`vol` | Motion / mode / battery / voltage placeholders |

Emitted on:
- the CDC shell as `+SDVRNTF: { … }\r\n`
- (when M3 wires up) over the modem channel via `SDVR+NTFA=…`

### 3.4 Safe-boot / bootloop guard

The FSBL increments a counter in `TAMP->BKP1R` on every boot, before clock
/ xSPI / App-jump — so even App-init crashes (bad model, ATON HW init,
camera bring-up) get counted. The App's `shell_task` reads the counter
as soon as it's up; once it crosses **3** consecutive crash reboots, the
App enters **safe mode**:

- NN auto-start is suppressed (the most common crash vector — bad model
  weights — can't run again).
- Operator's CDC shell stays alive; `update app` / `update model` work
  normally, so the kit recovers without SWD.

After ~30 s of healthy uptime, the App clears `BKP1R` and the next reboot
starts the count over.

| BKP register | Purpose | Set by | Cleared by |
|---|---|---|---|
| `BKP0R` | Recovery magic `0xDEADBEEF` — halts FSBL pre-xSPI for SWD reflash | App `recovery` cmd | FSBL on next boot |
| `BKP1R` | Bootloop counter (0–255) | FSBL increments every boot | App after 30 s uptime, or `safeboot clear` |

**Shell command (App side):**

```
> safeboot status         # current counter, threshold, safe-mode flag
boot_count = 1/3, safe_mode = no
safeboot status ok

> safeboot clear          # zero the counter (use after fixing root cause)
> safeboot test           # force counter to 3 and reboot — verifies safe
                          # mode engages correctly (DEV USE ONLY)
```

This closes the only remaining "needs JTAG" recovery scenario: a bad App
or model push that today would require boot-switch + SWD reflash now
self-recovers via USB after 3 boot attempts. TAMP survives reset but not
power-off, which is the right semantic — a user-initiated power cycle is
a fresh start.

### 3.5 Self-update protocol (M1 / A4)

See `M1_M2_WORKFLOW.md` for the diagram. Wire format:

```
host → kit:  update {app|model}\n
                         (1 line, plain text shell command)
host → kit:  UPDT
host → kit:  <size_le_4>
host → kit:  <crc32_le_4>
host → kit:  <size bytes of payload>
kit  → host: …Writing N bytes…
kit  → host: Done. Resetting...
             (and NVIC_SystemReset)
```

`size` cap: 1 MiB for `app`, 16 MiB for `model`. CRC must match (zlib `crc32`). Tool: `./n6cam-update.py --target {app|model} <file>`.

---

## 4.  Storage layout

### 4.1 xSPI NOR (MX66UW1G45G, 32 MiB)

| Slot | Address | Cap | Signed? | Self-update? |
|---|---|---|---|---|
| FSBL | `0x70000000` | 108 KB today | yes (`-hv 2.3`) | no — SWD only |
| Application | `0x70400000` | ≤ 1 MiB | yes (`-hv 2.3`) | yes, via CDC `update app` |
| Model weights | `0x70600000` | ≤ 16 MiB | raw bytes (mmaped by ATON runtime) | yes, via CDC `update model` |

### 4.2 SD card layout

```
/Snapshot{N}.jpeg                            ← auto-snapshot on NN detect
/{serial}_{DDMMYYYY}_{HHMMSS}.rdy            ← `photo savesd` (SoW §7)
/{serial}_{DDMMYYYY}_{HHMMSS}.upd            ← `.rdy` renamed after a
                                                successful HTTPS upload
                                                (SoW §8.1; M3 wiring)
```

`sd ls` (chunked output via the shell — previously truncated past 38 entries, fixed in v1.3.2) shows everything in the FAT root.

---

## 5.  Performance characteristics

| Metric | Value | Source |
|---|---|---|
| NN inference (P50 / P95) | 25.0 ms / 25.5 ms | `tests/run_tests.py`, 5-iter soak (75 NN samples) |
| Camera FPS | 29 Hz | `system_task` FPS counter |
| App boot time (cold) | ~5 s | uptime poll after `n6cam-update.py --target app` |
| Multi-class model size | 3.4 MB INT8 (192×192, 80-class YOLOv8n) | `tools/qat_artifacts/yolov8n_relu30_u8in_f32out.tflite` |
| Compiled NPU blob size | 3.0 MB | `vendor/.../Firmware/Model/network_atonbuf.xSPI2.raw` |
| Detection accuracy (test set) | 55 / 56 PASS, 1 SKIP | `results/test-report-20260523_165724.{html,pdf}` |

---

## 6.  Bench acceptance (Proposal v4 / A5)

### 6.1 Quick smoke (~1 min)

```bash
./modular-tools.sh test
# or, plain Python:
python3 tests/run_tests.py
```

Output:
- `results/test-report-<ts>.html` — self-contained, with detection thumbnails inlined as base64 data URIs
- `results/test-report-<ts>.pdf` — same content via headless Chrome
- Stdout: PASS/FAIL summary table

Reference baseline: **55 PASS / 0 FAIL / 1 SKIP, NN P95 25.5 ms.**

### 6.2 ≥ 24 h soak (A5 deliverable)

```bash
python3 tools/bench_soak.py --hours 24
```

What it does:

- Loops the full regression suite (~65 s/iteration → ~1330 iterations / 24 h).
- Tracks **kit uptime** between iterations — flags any drop as an unexpected reboot.
- Logs to `results/soak-<ts>/soak.csv` — per-iteration row: `wall_s, kit_uptime_s, rebooted, pass, fail, skip, total, test_time_s, nn_med_ms, nn_p95_ms`.
- Each iteration also drops a same-numbered HTML report under `results/soak-<ts>/soak-NNNNN.html`.
- Prints a summary at end: total iterations, pass rate, unexpected reboots, NN median + P95 across all samples.

**Pass criterion:** `unexpected_reboots == 0` and `fail == 0` over the full window. Verified 6-min smoke (97.5 % pass rate, 0 reboots, NN P95 25.5 ms — only failures are the documented T07.4/T11.6 known issues on cold-start).

### 6.3 M3 cellular plane validation (when MangOH lands)

1. Wire J503 pin 7 (N6 TX) → MangOH UART RX, pin 9 (N6 RX) → MangOH UART TX, pin 11 → MangOH GND. Match 3.3 V logic levels.
2. On the kit's CDC shell:
   ```
   > mdm test echo            # HDLC framing roundtrip — verified locally
   > mdm AT                   # should answer "OK" from the MangOH
   > mdm AT+CSQ               # signal strength
   > mdm SDVR+HOSTIP=…        # configure server
   > mdm SDVR+PING=42         # echoes back "+SDVRPING: 42"
   > photo upload             # captures + ships JPEG via SoW §8.2
   ```
3. For pre-MangOH testing, use the host-side stub:
   ```
   sudo python3 tools/fake_modem.py /dev/ttyUSB0
   ```
   wired through any 3.3 V USB-UART adapter into J503. It answers AT/SDVR+ commands and consumes SDVR+SENDBIN binary tails so the full transport stack can be wrung out without the actual modem on the bench.

---

## 7.  Known limitations

| ID | Symptom | Workaround |
|---|---|---|
| T11.6 / crowd_13.jpg | Specific dense-crowd image triggers ATON hardfault; kit IWDG-resets in ~1 s, App boots clean | None — production live-mode sees a ~5-s glitch on this exact input then full recovery. Documented in `MULTICLASS_STATUS.md`. |
| 5_people.jpg under-count | Model reports 1-2 people instead of 5 | Model-quality limitation at 192×192 input. Closes with bigger input + longer training (Lane C of the upcoming improvement plan). |
| 1-class baseline kept | `vendor/.../Model/_backup_person_only/` holds the original 17 ms/frame person-only model | Swap if a faster single-class baseline is needed; the rest of the App is class-count-agnostic. |

---

## 8.  Repository layout

```
.
├── M1_M2_COVERAGE.md           — WBS coverage doc (v1.4.0)
├── M1_M2_WORKFLOW.md           — pipeline + self-update flow (v1.4.1)
├── MULTICLASS_STATUS.md        — model accuracy / stability journal
├── INTEGRATION_DOC.md          — this file (A6)
├── STATUS_EMAIL_M1_M2.md       — client-ready acceptance email
├── Scopus_SoW_v3.pdf           — authoritative SoW
├── Kamacode_N6Cam_Proposal_v4.docx — WBS + milestone plan
│
├── docs/
│   ├── diagrams/               — graphviz sources + rendered PNGs
│   └── docx/                   — DOCX exports of the .md docs
├── results/                    — bench-test HTML + PDF + soak CSVs
├── captures/                   — live-detection JPEGs from the kit UVC
│
├── tests/
│   ├── run_tests.py            — main regression suite
│   ├── images/                 — deterministic test scenes
│   ├── c/test_hdlc.c           — HDLC host-test binary
│   └── test_hdlc_crosscheck.py — Python ↔ C HDLC equivalence check
│
├── tools/
│   ├── hdlc.py                 — Python HDLC encoder/decoder
│   ├── fake_modem.py           — host stub for M3 pre-MangOH bring-up
│   ├── bench_soak.py           — A5 ≥ 24 h soak script
│   └── qat_artifacts/          — saved model + compiled NPU blob
│
├── n6cam-update.py             — CDC self-updater (App / Model)
├── modular-tools.sh            — vendor-style build/sign/flash wrapper
│
└── vendor/n6cam.core.bsp/      — vendored SIANA BSP with our overlays
    └── Firmware/Application/Core/
        ├── Inc/{,Tasks/}*.h
        └── Src/{,Tasks/}*.c   — modem_task.c, hdlc.c, snapshot_task.c,
                                  shell_task.c are our additions/edits
```

---

## 9.  Tags on master

| Tag | Scope |
|---|---|
| `v1.0.0` | Baseline (single-class person detector working) |
| `v1.1.0-multiclass` | Multi-class person+vehicle detection plumbing |
| `v1.2.0-multiclass-counts` | NMS + bbox sigmoid fixed, counts match GT on clean cases |
| `v1.3.0-accurate-bboxes` | Dequantize zero-point bias fix — `person@0.91` instead of `0.34` |
| `v1.3.1-suite-green` | 55 PASS / 0 FAIL / 2 SKIP, full bench acceptance |
| `v1.3.2-sd-ls-fix` | `sd ls` truncation fixed, `photo savesd` test passes |
| `v1.4.0-m1-m2-delivery` | M1 + M2 coverage doc + acceptance email |
| `v1.4.1-m1-m2-docs` | Workflow doc + diagrams + DOCX exports |
| `v1.4.2-self-contained-report` | HTML report embeds base64 images; auto PDF render |
| **`v1.5.0-m3-cellular-plane`** | **M3 part 1: HDLC + mdm + SoW §8.2 photo upload (this doc)** |

`git log --oneline | head -20` for individual commits.
