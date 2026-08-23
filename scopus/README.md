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
                               ├─ HTTPS file upload ─▶ server        ← §8
                               └─ MQTT/TLS ◀───────── server         ← commands
```

The last arrow points the other way on purpose: it is the only leg the server
starts. See *Remote commands* below.

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
| K motion sensor | the LSM6DSO32 reads gravity, its self-test moves the mass, and that produces motion start/stop at the server — and a detection produces neither |

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

Two documents, both generated, for two different readers:

| Document | For | Covers |
|---|---|---|
| `Scopus_QA_Flow.docx` | the run QA repeats | the flow and nothing else: the product on the cable, then the same product over cellular through the customer's server, including driving it from there over MQTT |
| `Scopus_Tester_Manual.docx` | the first walk-through, and anything that goes wrong | what each step proves, per-step troubleshooting, the public relay, and the parts you only reach when something fails |

`Scopus_QA_Flow.docx` is the short one — two parts, one command per line,
a pass-criteria table at the end of each and a cheat sheet of the whole thing
on the last page. It was walked end to end on the bench on 2026-08-17 and
every "Expected" block in it is what came back that day.

```bash
python3 scopus/make_qa_flow.py
```

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

### Remote commands (section 19 of the manual)

Everything above is the unit talking outwards. The command channel is the
other direction: type a command on the server and the unit carries it out,
over cellular, with nothing plugged in and nothing opened on the unit's side.

The unit has no dialable address — carrier NAT — so it holds a connection
*out* to an MQTT broker and commands are pushed down it. Transport is TLS on
port **5912** with a **client certificate**, which is the device's identity:
the broker maps its CN to the MQTT username, so no password travels or is
stored, and a client with no certificate is refused outright.

```bash
# the unit: point it at the broker and switch the channel on
python3 scopus/at.py 'AT+SDVRMQTTSRV="213.8.185.180",5912'
python3 scopus/at.py "AT+SDVRMQTT=1"
python3 scopus/at.py "AT+SDVRMQTT?"    # 1,1,… = enabled, connected

# on the server: watch, then drive. <id> is the unit's IMEI by default.
C=/opt/sdvr-server/certs
mosquitto_sub -h 213.8.185.180 -p 5912 --cafile $C/ca.crt \
        --cert $C/client.crt --key $C/client.key -t 'scopus/<id>/#' -v
mosquitto_pub -h 213.8.185.180 -p 5912 --cafile $C/ca.crt \
        --cert $C/client.crt --key $C/client.key \
        -t 'scopus/<id>/cmd' -q 1 -m 'photo upload'
```

| Topic | Direction | Carries |
|---|---|---|
| `scopus/<id>/cmd` | to the unit | one command per message, plain text |
| `scopus/<id>/rsp` | from the unit | that command's output |
| `scopus/<id>/ntf` | from the unit | §6 notifications, when `AT+SDVRNTFPROTO=2` |
| `scopus/<id>/status` | from the unit | retained `online` / `offline` (the broker publishes `offline` itself if the unit drops) |

A command starting with `AT` is answered by the modem — signal, SIM,
registration, the questions you actually want to ask a unit you cannot reach.
Anything else is run in the camera shell, so `version`, `mdm stats`,
`detect start` and `photo upload` all work as typed.

**Why not the notification channel, which also has a return path.** It does
work — `notify.c` sends from one UDP socket and reads replies off the same fd,
and a reply reached the camera 0.2 s after a report. But the carrier's NAT
mapping closes in **under 30 seconds**: probes sent back 30, 60, 120 and 240 s
after a report never arrived, measured at both ends. That path can answer a
report; it cannot start a conversation, which is what a command has to do.

Two things bite here as well. The broker must own 5912, so ITP's
`sdvr-https.service` is stopped on the bench (`systemctl start sdvr-https`
restores it, but not while mosquitto holds the port) — photos are unaffected
on 8991. And `AT+SDVR*` commands are answered by *our* atServer rather than
the module's AT parser, which is why `mqtt.c` owns a PTY handed to atServer;
without it every `AT+SDVR*` sent remotely comes back a bare ERROR.

## Run

Run on the host the hardware is attached to (the bench/remote PC):

```bash
python3 scopus/run_scopus_tests.py
```

Output: a self-contained `results/test-report-<ts>.html` + same-stem `.pdf`
(same style as the per-device reports). Env overrides:

| Var | Comes from | Meaning |
|---|---|---|
| `N6_TTY` | auto (`usb-STMicroelectronics_N6Cam_*-if02`) | N6 CDC shell |
| `SDVR_PORT` | FTDI host UART (`ttyUSB0`) | modem SDVR command channel |
| `MODEM_IP` | `bench.ini` `[modem] ip` | modem SSH (side-effect checks) |
| `MODEM_PASSWORD` | `bench.ini` `[modem] password` | modem root password |
| `HOST_IP` | `bench.ini` `[modem] host_ip` | host endpoint for §6/§8 E2E |
| `SERVER_HOST` | `bench.ini` `[server] host` | where notifications and photos go |

## Bench settings — `bench.ini`

Every address, port and password the tooling needs lives in **one untracked
file**, `scopus/bench.ini`. Nothing site-specific is committed (ScopusQA #11).

```bash
cp scopus/bench.ini.template scopus/bench.ini
$EDITOR scopus/bench.ini          # fill in your modem password and server
```

`scopus/bench.ini.template` **is** committed, and every value in it is a
deliberate placeholder — `CHANGEME`, `203.0.113.10` (a reserved
documentation address). If you forget to fill one in, the tool that needed it
says which setting, in which file, and stops; it does not silently try to
reach the placeholder.

Resolution order per value: environment variable (upper-case name) → 
`bench.ini` → `bench.ini.template`. So a one-off run can override anything
without editing the file:

```bash
MODEM_PASSWORD=hunter2 python3 scopus/preflight.py
python3 scopus/lib/settings.py       # show what is currently in effect
```

**Two values are committed with real data on purpose**, and the template says
so inline: `[modem] ip = 192.168.2.2` and `[modem] host_ip = 192.168.2.3`.
Those are the two ends of the modem's own USB/ECM link — a fixed property of
the Sierra WP76 interface, identical on every unit ever shipped, and no more
site data than `127.0.0.1`. Treating them as secrets would mean telling every
reader of this repository a constant out of band.

## Certificates

**There is one certificate set, and HTTPS and MQTT both use it.** They are not
configured separately and cannot diverge:

| | |
|---|---|
| On the modem | `/data/sdvr/certs/ca.crt`, `client.crt`, `client.key` (mode `0600`) |
| Single accessor | `Cert_GetPaths()` in `cert_manager.c` |
| HTTPS upload | `upload_file.c` → curl `CAINFO` / `SSLCERT` / `SSLKEY` |
| MQTT/TLS | `mqtt.c` → `SSL_CTX_load_verify_locations` / `use_certificate_file` / `use_PrivateKey_file` |

So yes — provisioning them once from the SD card provisions both channels.
The SD card is the only supported source:

```
AT+SDVRCERTIMPORT          # copy ca.crt / client.crt / client.key off the card
AT+SDVRCERTDEL="ALL"       # remove all three from the modem
```

or, hands-free at boot, through `tconf.ini` on the same card:

```ini
CERTIMPORT      = 1        ; import on this pass
CERTIMPORTONCE  = 1        ; ...and clear the flag afterwards
CERTFILECA      = ca1.crt  ; optional: the names on the CARD differ
CERTFILECLNT    = c1.crt   ; the destination slots above never change
CERTFILECLNTKEY = c1.key
CERTDELSD       = 1        ; wipe them off the card once imported
```

The `CERTFILE*` overrides name a **single file on the card root** — a value
containing `/`, `\`, `.` or `..` is refused with a log line and the default
name is used instead, so a `tconf.ini` cannot be used to read or delete a file
elsewhere on the modem (ScopusQA #12, E·20).

Neither channel will fall back to server-only verification: if any of the three
files is missing, an https upload fails with `+SDVRUPL: ERROR 12` and MQTT
refuses to enable, rather than connecting without a client certificate.

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
| `rtc` | `[set DDMMYYYYHHMMSS \| sync]` — get, set, or take the time from the modem. Syncs automatically at start-up and on the modem's `+SDVRRDY` / `+SDVRNET: UP`; the camera has no battery-backed clock, so otherwise the §7 photo name and the §6 `tim` field both read 2000-01-01 |
| `version` | application version |
| `system` | `[version]` — fw / uid / dev / rev (§3.7) |
| `commands` | list every command with its parameters (§3.7) |
| `echo` | `on \| off \| query` |
| `irled` | `on \| off \| query` |
| `motion` | `sense <0..100> <timeout_s>` \| `query` \| `read` \| `selftest` \| `simulate 0\|1` — the **box** being moved, from the LSM6DSO32 inertial sensor (§3.5, §4.5) |
| `img` | `size H W` \| `quality 1..100` \| `color YCBCR\|RGB\|CMYK` \| `chroma 0\|1` \| `query` |
| `detect` | `start` \| `stop` \| `profile <det_msk> <act_msk>` \| `profile query` \| `debounce <ms>` \| `debounce query` \| `stats` \| `simulate [N]` |
| `notify` | `enable <mask>` \| `disable` \| `trigger <code>` \| `period <s>` \| `query` |
| `photo` | `savesd` \| `upload` — capture a JPEG to SD, or straight out through the modem |
| `sd` | `query` \| `ls` \| `format CONFIRM` |
| `frame` | `upload` \| `load <file.raw>` \| `run` \| `clear` \| `query` — inject a test frame into the NN |
| `tile` | `grid c r` \| `crop px` \| `frame W H` \| `overlap h v` \| `thresh conf iou` \| `upload` \| `run` \| `live [n]` \| `query` \| `clear` \| `default` |
| `mdm` | `<AT command>` \| `relink` \| `stats` \| `raw on\|off` \| `test wedge [baud]` \| `test urc <line>` \| `test echo` — modem pass-through (§4.6) |
| `camera` | `flip H\|V\|off` \| `aec <-2.0..2.0>\|off` \| `awb <0..N>\|auto` (N from the sensor's ISP tuning — 2 on IMX335) \| `gain <0..72000 mdB>` \| `exposure <0..33000 µs>` \| `brightness <0..100>` (not implemented on IMX335) \| `status` — print all of the above |
| `safeboot` | `status` \| `clear` \| `test` — bootloop counter and safe-mode drill |
| `update` | `[app \| model]` — receive new firmware/model over CDC and reflash |
| `recovery` | reboot into FSBL recovery (halts the chip) |

The three masks, since they are the ones testers get wrong:

- `detect profile <det_msk>` — bit0 = people, bit1 = vehicles.
- `detect profile <act_msk>` — bit0 = save to SD, bit1 = report over cellular,
  bit2 = upload the photo. **`7` is the full product**; the default is `0`,
  which detects and does nothing with it.
- `notify enable <mask>` — 1 NetReg, 2 MotionStart, 4 MotionStop, 8 Periodic,
  0x10 People, 0x20 Vehicle. `0x30` is people + vehicles, `0x3f` is everything.

  **The mask is enforced from firmware build 2026-08-19 on.** Until then it was
  stored, echoed back by `notify query`, and consulted by nothing — so enabling
  a bit changed nothing and disabling one changed nothing either. Events whose
  bit is clear are now dropped with a log line naming the mask. Two deliberate
  exceptions: `notify trigger <code>` always sends, because its purpose is to
  exercise the transport regardless of configuration, and the photo event
  (`0x40`) is a local extension outside the §4.2 table and is not gated.

  What produces each bit:

  People and vehicles are reported as **two separate notifications**, each with
  its own count in `rsd`, and each only when that class's own count changed —
  a person walking through a car park does not re-announce the parked cars.
  A single event carries one `rsd`, which is why this is not one event with
  `rsn=0x30`.

  | Bit | Event | Produced by |
  | --- | ----- | ----------- |
  | `0x01` | Network registration | the modem's `+SDVRNET: UP`, and its `+SDVRRDY` start-up banner (the "on power up / reset" half of §4.2) |
  | `0x02` | Motion start | the **unit** being moved — the inertial sensor's deviation from its resting attitude crossing the `motion sense` threshold |
  | `0x04` | Motion stop | that deviation staying under the threshold for the `motion sense` no-motion timeout |
  | `0x08` | Periodic | `notify period <s>`; `0` (the default) switches it off |
  | `0x10` | People detected | the NN's stable person count |
  | `0x20` | Vehicle detected | the NN's stable vehicle count. The detector is person+vehicle (`pv` model, 80 COCO classes); `_class_passes_mask` maps COCO 1-8 (bicycle, car, motorcycle, bus, truck, and the airplane/train/boat bucket) onto this bit. Needs `detect profile` bit1 set. |

  **Motion means the box, not the scene.** Until 2026-08-19 bits 1 and 2 were
  raised off the detector's debounced object count, so a person walking past a
  bolted-down camera reported that the camera was being carried away. §3.5 and
  §4.5 describe a *motion sensor*, and the board carries one — an LSM6DSO32
  6-DOF IMU on the sensors I2C. The two bits now come from it, and objects
  moving in the field of view are reported under `0x10` / `0x20` only.
  (Confirmed by ITP, 2026-08-19: "the motion detection refers to motion sensor
  that exists on the camera to identify movements of the entire board (box)".)

  `detect simulate <N> [people|vehicle]` picks the class, so the bench can
  exercise `0x20`: there is no car to point the lens at.

```
detect profile 0x03 0x02      # detect people AND vehicles, report them
detect simulate 2 vehicle     # -> rsn=2 rsd=2, then rsn=32 rsd=2
detect simulate 3             # -> rsn=16 rsd=3
```

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
| `python3 scopus/make_qa_flow.py` | none — rewrites `Scopus_QA_Flow.docx` |
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
| 5 N6 sensors | §3.5, §4.5 | irled on/off/query, motion sense/query persistence (the sensor itself is group K of the integration suite) |
| 6 N6 camera | §4.3 | awb / exposure / gain passthrough |
| 7 N6 → SD pipeline | §3.2, §7 | sd query/ls, `photo savesd` → `serial_DDMMYYYY_HHMMSS.rdy` appears |
| 8 Modem SDVR control | §5.2 | AT, SDVRPING, server host/port set→SRVGET round-trip |
| 9 Modem SDVR — Scopus | §5.2/§6/§8.2 | **new** NTFHOST/NTFPORT, NTFA→UDP, SENDBIN live upload |
| 10 Modem SD via SDVR | §3.2, §5.2 | MOUNTSD/UNMOUNTSD (verified via /proc/mounts), LSALL |
| 11 N6 ↔ modem tunnel | §4.6 | N6 `mdm AT...` tunnels to modem and back |
| 12 E2E notification | §6 | modem→host UDP datagram with SoW §6 JSON fields |
| 13 E2E HTTPS upload | §8 | SD file lands on HTTPS server with X-Timestamp/X-Ref |

The **remote command channel** (`AT+SDVRMQTT*`, `AT+SDVRCMDR`, `+SDVRCMD`) is
not in the automated suite: proving it needs a broker with the device's client
certificate, which is server-side state the suite does not own. It is covered
by hand instead, in section 19 of the tester manual, with its own pass
criteria — including "every command is answered exactly ONCE", which is there
because the lossy-ack retry published every response twice on the first
end-to-end run.

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
                             # (remote commands need no script here: the
                             #   server end is mosquitto_pub/_sub, section 19)
  relay_pull.py              # pulls from the relay onto your PC (no inbound
                             #   port needed anywhere)
  Scopus_QA_Flow.docx        # the short flow QA repeats: cable, then cellular
                             #   + MQTT against the customer's server
  make_qa_flow.py            # regenerates it — edit this, not the docx
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
