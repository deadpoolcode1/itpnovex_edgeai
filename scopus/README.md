# Scopus PoC — Whole-System Test Suite

End-to-end tests for the **integrated** Scopus system described in
`../edgeai/Scopus_SoW_v3.pdf`, exercising both devices and the seams between
them in a single pass:

```
Control PC ─(UART/USB)─▶ N6 Main CPU (camera + detection + shell)   ← §3, §4
                               │  AT-over-HDLC (internal UART)
                               ▼
                        WP76 modem (SDVR app)                        ← §5
                               ├─ UDP notifications ─▶ server        ← §6
                               └─ HTTPS file upload ─▶ server        ← §8
```

The two per-device suites already cover each box in isolation
(`edgeai/tests/run_tests.py` for the N6, `V20_SDVR` `modular-tools.sh test-run`
for the modem). **This suite covers the system as a whole** — the N6 control
channel, the modem SDVR command channel, the N6→modem tunnel (§4.6), and the
modem→server notification/upload paths (§6/§8).

## Two suites

| Suite | Question it answers |
|---|---|
| `run_scopus_tests.py` | Does every command and seam behave per the SoW? |
| `run_integration_tests.py` | **Does the product work?** — point the camera at people and see whether an event leaves the modem. |

The second exists because the first can pass completely while the product does
nothing useful: wherever it tests an "end to end" path it drives the modem with
an AT command directly, so the camera→modem→server chain is never actually
walked. `run_integration_tests.py` injects an image with a known number of
people, runs inference on the device, and follows the event outward hop by hop:

```
injected frame → NN detection → camera notification
    → camera/modem UART → modem SDVR app → UDP datagram on the host
```

Each hop is asserted separately, so a failure names the hop. It adds a **GAP**
status for hops that are genuinely unimplemented in firmware, kept distinct
from FAIL so a missing feature can never be mistaken for a passing one — the
GAP count is the honest distance to a working product.

```bash
python3 scopus/run_integration_tests.py     # ~90 s, writes results/integration-<ts>.json
```

**Current: 56 PASS / 0 FAIL / 0 GAP / 1 SKIP**, identical over three
consecutive runs. The SKIP is the physically absent SD card. Point the camera
at people and the modem sends the event.

Every test restores what it changed, so back-to-back runs give identical
results. That is load-bearing rather than decorative: the suite stops live
inference before the first group and after the last (`quiesce_detector`),
because a detector left running emits `+SDVRNTF` between a command and its
response and derails the *next* run from group A onward. Group D additionally
waits for the camera's notification queue to drain (`wait_for_notify_drain`)
before measuring link retries, since the notifier is a second producer on the
same UART and its frames would otherwise be counted as D's own.

Env: `SCOPUS_IMAGES` (default `edgeai/images`), `HOST_IP`, `NTF_PORT`,
`MODEM_IP`, `SDVR_PORT`.

### Integration groups

| Group | What it proves |
|---|---|
| A prerequisites | both devices up, tunnel live |
| B NN detection | injected images with known people counts detect correctly |
| C camera notification | §6 JSON shape, RTC-backed timestamp, real detection notifies |
| D camera↔modem tunnel | round-trip, data integrity, idle-gap recovery |
| E full chain | detection → modem → **UDP datagram on the host** |
| F photo upload | JPEG → SENDBIN → modem ingests it (not discards) |
| G state hygiene | LiveBin arm/reject/release is repeatable |
| H NTFA payload transport | the §6 JSON survives the AT channel byte-exact — see below — and a retry is idempotent |
| I CN805 link recovery | a deliberately wedged link is recovered without rebooting the camera |
| J `mdm` pass-through | a quoted AT parameter reaches the modem intact |

### Why group H exists

The §6 notification body is JSON, and JSON does not survive an AT command line
unaided. atServer's parameter parser **consumes embedded double quotes**, so
`{"ser":1}` arrives at the modem as `{ser:1}` — accepted, answered `OK`, and no
longer JSON. On top of that, one AT parameter is capped at **128 bytes**
(measured: 128 accepted, 129 a clean `ERROR`), which a fully-populated §6 body
exceeds.

So `AT+SDVRNTFA` takes an optional trailing `ENC`; `ENC=1` means "rejoin the
payload parameters and restore `` ` `` → `"`". The camera splits the body into
128-byte chunks and the modem reassembles. Full protocol:
`V20_SDVR/README.md` → *Notifications & Control channel*.

Group H pins that contract and **asserts on the received datagram, never on the
AT reply** — every failure mode it guards against answers `OK`.

## Manual end-to-end test

`Scopus_Tester_Manual.docx` walks the whole product by hand in seven steps —
start a server on your PC, point the modem at it, configure the camera to
detect people, inject an image, and watch the event and the photo arrive. It
ends with a JSON event and a JPEG file on your PC that came off the device,
which is the pass condition: a device log line saying it sent them is not.

The manual is **generated** from `make_tester_manual.py` so the procedure stays
reviewable as a diff. Edit the script and re-run it; do not hand-edit the docx:

```bash
python3 scopus/make_tester_manual.py
```

The server it uses is `test_server.py`, which listens for both things the
product sends — notifications on UDP and photos on HTTP — in one process with
one log, so neither can be missed because the wrong listener was running:

```bash
python3 scopus/test_server.py --http-port 8080 --udp-port 9999 \
        --dir ~/scopus-received --from-modem 192.168.2.2
```

## Run

Run on the host the hardware is attached to (the bench/remote PC):

```bash
python3 scopus/run_scopus_tests.py
```

Output: a self-contained `results/test-report-<ts>.html` + same-stem `.pdf`
(same style as the per-device reports). Env overrides:

| Var | Default | Meaning |
|---|---|---|
| `N6_TTY` | auto (`usb-STMicroelectronics_N6Cam_*-if02`) | N6 CDC shell |
| `SDVR_PORT` | FTDI host UART (`ttyUSB0`) | modem SDVR command channel |
| `MODEM_IP` | `192.168.2.2` | modem SSH (side-effect checks) |
| `MODEM_PASSWORD` | `Ss123` | modem root password |
| `HOST_IP` | auto (modem default gw) | host endpoint for §6/§8 E2E |

## Coverage (mapped to Scopus SoW v3)

| Group | SoW | What it proves |
|---|---|---|
| 0 Prerequisites | — | N6 shell up + app-vs-stock; modem AT + SSH + SDVR app/version/cmd-count |
| 1 N6 control & system | §4.1, §3.7 | echo ack, rtc round-trip, app version, command list |
| 2 N6 detection | §3.1, §4.2 | detect start/stop, profile set/query (people/vehicles, save-SD) |
| 3 N6 notifications | §3.1, §4.2, §6 | notify enable/period/query, trigger → +SDVRNTF JSON, disable |
| 4 N6 photo settings | §3.4, §4.4 | img quality/color/chroma set + query round-trip |
| 5 N6 sensors | §3.5, §4.5 | irled on/off/query, motion sense/query persistence |
| 6 N6 camera | §4.3 | awb / exposure / gain passthrough |
| 7 N6 → SD pipeline | §3.2, §7 | sd query/ls, `photo savesd` → `serial_DDMMYYYY_HHMMSS.rdy` appears |
| 8 Modem SDVR control | §5.2 | AT, SDVRPING, server host/port set→SRVGET round-trip |
| 9 Modem SDVR — Scopus | §5.2/§6/§8.2 | **new** NTFHOST/NTFPORT, NTFA→UDP, SENDBIN live upload |
| 10 Modem SD via SDVR | §3.2, §5.2 | MOUNTSD/UNMOUNTSD (verified via /proc/mounts), LSALL |
| 11 N6 ↔ modem tunnel | §4.6 | N6 `mdm AT...` tunnels to modem and back |
| 12 E2E notification | §6 | modem→host UDP datagram with SoW §6 JSON fields |
| 13 E2E HTTPS upload | §8 | SD file lands on HTTPS server with X-Timestamp/X-Ref |

Groups self-skip with a precise reason when a prerequisite is missing (N6 on
stock firmware, modem on a pre-Scopus build, no data session / server, etc.) —
the suite never silently passes over an unavailable channel.

## Layout

```
scopus/
  run_scopus_tests.py        # per-command/per-seam suite (HTML+PDF report)
  run_integration_tests.py   # whole-product chain, hop by hop (JSON report)
  test_server.py             # the "server" end: receives notifications (UDP)
                             #   and photo uploads (HTTP), on your PC
  Scopus_Tester_Manual.docx  # step-by-step MANUAL E2E test (generated)
  make_tester_manual.py      # regenerates the .docx — edit this, not the docx
  STATUS.md                  # RESUME HERE: bench access, build/deploy, what's open
  bench-tools/               # link probes and soak tools (see STATUS.md §6)
  lib/devices.py             # N6Shell, ModemAt, ModemSsh (raw termios + sshpass; no pyserial)
  lib/report.py              # Suite/TestResult + HTML/PDF writer (shared style)
  results/                   # generated reports (gitignored)
```

This is the **only** Scopus directory — it is tracked in the `edgeai` repo on
`master`, and it drives both devices. A second untracked copy at
`itpnovex/scopus/` was removed on 2026-08-05; don't recreate it.

## Bench notes (2026-06-22)

- **N6 CDC shell** is the N6Cam by-id `*-if02` (→ `/dev/ttyACM1`).
- **Modem SDVR command channel** is the FTDI host UART (`/dev/ttyUSB0` →
  `/dev/ttyHSL1` on the modem). The Sierra *native* AT port (`/dev/ttyUSB3`)
  answers bare `AT` but returns ERROR for `AT+SDVR*` — it is **not** the SDVR
  channel. Note `ttyHSL1` is also the modem's kernel console, so the app's
  UartFilter shares it with console/getty and the first command after an idle
  gap is often swallowed; `ModemAt` primes + retries to compensate.
- **Cold-boot quirk:** the SDVR app self-restarts once per reboot to fix
  le_atServer response binding (`/data/sdvr/boot_id` marker, see
  `main.c`). If responses go missing after reflash churn,
  `rm /data/sdvr/boot_id && app restart sdvrApp` re-triggers it.
