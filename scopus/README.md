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

### Over cellular (section 18 of the manual)

`test_server.py` only works while the cable is attached. On cellular the modem
has a carrier-NAT address and your PC has no public address, so neither end can
open a connection to the other — a third machine has to hold the middle.

`cloud_relay.py` runs on a host with a public address and receives exactly what
`test_server.py` receives; `relay_pull.py` runs on your PC and pulls it down
over an ordinary outbound request. Both ends dial out, so it works from any
network with nothing to configure on a router.

```bash
# on the public host (already installed as the systemd unit scopus-relay)
python3 scopus/cloud_relay.py --key <secret> --udp-port 39999 --http-port 38080

# on the bench: aim the modem there and switch it to cellular
python3 scopus/at.py --point-cloud 165.22.181.245 --http-port 80 \
        --udp-port 39999 --path /scopus/upload --apn <apn>

# on your PC: watch what arrives
python3 scopus/relay_pull.py --relay http://165.22.181.245/scopus --key <secret>
```

Two things bite here, both documented in `STATUS.md`: the notification host
must be a dotted **IP** (that leg is a raw UDP socket and never resolves
names), and the modem needs `AT+SDVRNET=1` — being registered on LTE is not
the same as having a route, and the difference is invisible unless you look at
the fourth flag of `AT+SDVRNET?`.

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

## Command reference

Everything a tester can type, in one place. Run all of it from the repo root on
the bench PC (the host the two devices are cabled to).

### Before anything else

```bash
python3 scopus/preflight.py
```

Takes no options. One line per check — camera CDC port, modem AT port, this
PC's address on the modem's subnet, modem reachable over Ethernet, server ports
free, test image present — and exits 0 only when the bench is fit to test on.
Every failure prints what to do about it. Run this first: a failure here is a
bench fault, and chasing it as a product fault is where the hours go.

### `cam.py` — talk to the camera

```bash
python3 scopus/cam.py "detect start"
python3 scopus/cam.py "photo savesd" --wait 30
```

| Option | Default | Meaning |
|---|---|---|
| `command…` | — | the shell command, quoted |
| `--wait SEC` | 4 (25 for `photo` / `frame run`) | how long to listen for the reply |
| `--tty PATH` | auto (`usb-STMicroelectronics_N6Cam_*-if02`) | override the CDC port |

Camera shell commands — the device's own list, `cam.py commands` prints it:

| Command | Parameters |
|---|---|
| `rtc` | `[set DDMMYYYYHHMMSS]` — get or set the clock |
| `version` | application version |
| `system` | `[version]` — fw / uid / dev / rev (§3.7) |
| `commands` | list every command with its parameters (§3.7) |
| `echo` | `on \| off \| query` |
| `irled` | `on \| off \| query` |
| `motion` | `sense <0..100> <timeout_s>` \| `query` |
| `img` | `size H W` \| `quality 1..100` \| `color YCBCR\|RGB\|CMYK` \| `chroma 0\|1` \| `query` |
| `detect` | `start` \| `stop` \| `profile <det_msk> <act_msk>` \| `profile query` \| `debounce <ms>` \| `debounce query` \| `stats` \| `simulate [N]` |
| `notify` | `enable <mask>` \| `disable` \| `trigger <code>` \| `period <s>` \| `query` |
| `photo` | `savesd` \| `upload` — capture a JPEG to SD, or straight out through the modem |
| `sd` | `query` \| `ls` \| `format CONFIRM` |
| `frame` | `upload` \| `load <file.raw>` \| `run` \| `clear` \| `query` — inject a test frame into the NN |
| `tile` | `grid c r` \| `crop px` \| `frame W H` \| `overlap h v` \| `thresh conf iou` \| `upload` \| `run` \| `live [n]` \| `query` \| `clear` \| `default` |
| `mdm` | `<AT command>` \| `relink` \| `stats` \| `raw on\|off` \| `test wedge [baud]` \| `test urc <line>` \| `test echo` — modem pass-through (§4.6) |
| `camera` | `flip H\|V\|off` \| `aec <-2.0..2.0>\|off` \| `awb <0..5>\|auto` \| `gain <0..72000 mdB>` \| `exposure <0..33000 µs>` \| `brightness <0..100>` |
| `safeboot` | `status` \| `clear` \| `test` — bootloop counter and safe-mode drill |
| `update` | `[app \| model]` — receive new firmware/model over CDC and reflash |
| `recovery` | reboot into FSBL recovery (halts the chip) |

The three masks, since they are the ones testers get wrong:

- `detect profile <det_msk>` — bit0 = people, bit1 = vehicles.
- `detect profile <act_msk>` — bit0 = save to SD, bit1 = report over cellular,
  bit2 = upload the photo. **`7` is the full product**; the default is `0`,
  which detects and does nothing with it.
- `notify enable <mask>` — 1 NetReg, 2 MotionStart, 4 MotionStop, 8 Periodic,
  0x10 People, 0x20 Vehicle. `0x30` is people + vehicles.

### `at.py` — talk to the modem

```bash
python3 scopus/at.py "AT+SDVRVER"
python3 scopus/at.py --point-here                  # aim the modem at this PC
python3 scopus/at.py --point-cloud 165.22.181.245 --apn <apn>
python3 scopus/at.py --raw "AT+CPIN?"              # ask the modem itself
```

| Option | Default | Meaning |
|---|---|---|
| `command…` | — | the AT command, quoted |
| `--point-here` | — | set all five endpoints to this PC's address on the modem's subnet, then read them back. This is the whole of Step 2 of the manual |
| `--point-cloud IP` | — | same, but aimed at a public relay, and switch the modem to its cellular backhaul (sets the APN, turns `AT+SDVRNET=1` on, and waits for a **route**, not just registration) |
| `--apn APN` | — | APN to set alongside `--point-cloud` |
| `--apn-auth` | `none` | `none` \| `pap` \| `chap` |
| `--apn-user` / `--apn-pass` | empty | APN credentials |
| `--http-port N` | 8080 | photo-upload port to program |
| `--udp-port N` | 9999 | notification port to program |
| `--path P` | `/upload` | upload URL path |
| `--notify-proto` | `http` | how `--point-cloud` sends notifications: `http` rides the same TCP port the photos use and gets a status code back; `udp` is the §5.2 default |
| `--notify-path P` | `/scopus/notify` | request path for HTTP notifications |
| `--raw` | — | send to the **modem's own** AT parser over SSH (`/dev/ttyAT`) instead of the SDVR channel — the only way to ask SIM, slot and radio questions; needs the USB Ethernet link even when testing over cellular |
| `--timeout SEC` | 4.0 | reply timeout |

The 37 `AT+SDVR*` commands and every URC they emit are tabled in
`V20_SDVR/README.md` → *AT Commands (SDVR prefix)*. The ones a Scopus tester
actually reaches for:

| Command | What it does |
|---|---|
| `AT+SDVRVER` | app version — check this first after any deploy |
| `AT+SDVRSIM?` | **check this first when cellular misbehaves** — the board has two SIM holders and running on the wrong one looks exactly like a dead network |
| `AT+SDVRSIM=1\|2\|0` | pin the holder: external slot 1, slot 2, or the eSIM |
| `AT+SDVRNET=1` / `AT+SDVRNET?` | bring up and inspect the data session; the **fourth** flag of the reply is the default route, and "registered" without it sends nothing |
| `AT+SDVRSRVGET` | read back the server config `--point-here` just wrote |
| `AT+SDVRNTFHOST="ip"` / `AT+SDVRNTFPORT=n` | notification endpoint — the host must be a dotted IP, that leg never resolves names |
| `AT+SDVRNTFPROTO=0\|1[,"path"]` | notifications over UDP (0) or HTTP POST (1) |
| `AT+SDVRNTFA=N,SIZE,"msg"[,…][,ENC]` | send a notification; `ENC=1` is required for JSON (see *Why group H exists*) |
| `AT+SDVRSENDBIN=X,"TAG","TIME",REF,SIZE` | arm a live photo upload from the UART |
| `AT+SDVRMOUNTSD` / `AT+SDVRUNMOUNTSD` / `AT+SDVRLSALL` | SD card |
| `AT+SDVRUPLALL` / `AT+SDVRUPL=N` / `AT+SDVRUPLSTOP` | upload pending files |

Anything the SDVR app does not recognise it bridges through to the modem, so
plain `AT`, `AT+CSQ` and friends work on the same channel — except the SIM and
radio commands, which need `--raw` (their replies come back at the wrong line
settings and read as a broken modem).

### Receivers

```bash
# on your PC, cable attached
python3 scopus/test_server.py --http-port 8080 --udp-port 9999 \
        --dir ~/scopus-received --from-modem 192.168.2.2 --fresh
```

| Option | Default | Meaning |
|---|---|---|
| `--http-port N` | 8080 | port for photo uploads |
| `--udp-port N` | 9999 | port for notifications |
| `--dir PATH` | `scopus-received` | where received files and logs land |
| `--from-modem IP` | any | only accept datagrams from this address |
| `--fresh` | — | start empty, moving an earlier run to `<dir>-old-<ts>` |

```bash
# on the public host (installed as the systemd unit scopus-relay)
python3 scopus/cloud_relay.py --key <secret> --udp-port 39999 --http-port 38080
```

| Option | Default | Meaning |
|---|---|---|
| `--key K` | **required** | shared secret the pull client must present |
| `--http-port` / `--udp-port` | 8080 / 9999 | device-facing ports |
| `--dir PATH` | `scopus-relay-data` | storage |
| `--device-token T` | — | also require the device's `X-Token` header (set with `AT+SDVRTOK`) |
| `--max-photos N` | 500 | ring size |

```bash
# on your PC, over cellular
python3 scopus/relay_pull.py --relay http://165.22.181.245:38080 --key <secret>
```

| Option | Default | Meaning |
|---|---|---|
| `--relay URL` | **required** | base URL of the relay |
| `--key K` | **required** | the relay's shared secret |
| `--dir PATH` | `scopus-received` | same layout as `test_server.py`, deliberately |
| `--from-start` | — | replay everything the relay still holds, ignoring the remembered position in `<dir>/.relay-seq` |
| `--fresh` | — | start empty, keeping an earlier run in `<dir>-old-<ts>` |
| `--timeout SEC` | 40.0 | long-poll timeout |

### Suites and manuals

| Command | Options |
|---|---|
| `python3 scopus/run_scopus_tests.py` | none — env vars only (see *Run*) |
| `python3 scopus/run_integration_tests.py` | `-v` / `--verbose` |
| `python3 scopus/inference_test.py` | `--image PATH` (default `images/3_people.jpg`), `--expect N` (default 3), `--tries N` (default 4) |
| `python3 scopus/make_tester_manual.py` | none — rewrites `Scopus_Tester_Manual.docx` |
| `python3 scopus/make_tracked_manual.py` | `--baseline old.docx` (default: the docx at git HEAD) — also writes the tracked-changes copy for review in Word |

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
  preflight.py               # is the bench fit to test on? run this first
  cam.py                     # send one command to the camera shell
  at.py                      # send one AT command to the modem (+ --point-*)
  run_scopus_tests.py        # per-command/per-seam suite (HTML+PDF report)
  run_integration_tests.py   # whole-product chain, hop by hop (JSON report)
  inference_test.py          # NN only: known image in, people count out
  test_server.py             # the "server" end: receives notifications (UDP)
                             #   and photo uploads (HTTP), on your PC
  cloud_relay.py             # the same receiver, on a public host, for the
                             #   cellular test — plus a pull API
  relay_pull.py              # pulls from the relay onto your PC (no inbound
                             #   port needed anywhere)
  Scopus_Tester_Manual.docx  # step-by-step MANUAL E2E test (generated)
  make_tester_manual.py      # regenerates the .docx — edit this, not the docx
  make_tracked_manual.py     # + a tracked-changes copy for review in Word
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
