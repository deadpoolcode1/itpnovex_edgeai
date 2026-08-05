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
python3 scopus/run_integration_tests.py     # ~65 s, writes results/integration-<ts>.json
```

Every test restores what it changed, so back-to-back runs give identical
results — verified over three consecutive runs. Env: `SCOPUS_IMAGES`
(default `edgeai/images`), `HOST_IP`, `NTF_PORT`, `MODEM_IP`, `SDVR_PORT`.

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
  run_scopus_tests.py   # runner + group definitions
  lib/devices.py        # N6Shell, ModemAt, ModemSsh (raw termios + sshpass; no pyserial)
  lib/report.py         # Suite/TestResult + HTML/PDF writer (shared style)
  results/              # generated reports
```

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
