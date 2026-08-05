# Scopus integration — status / resume point

Last updated: 2026-08-05. Everything below was measured on the bench, not inferred.

---

## RESUME HERE (next session)

### Bench — which machine, how to get on it

The devices are on the **Linux bench PC `t7aryz0009769z2`**, reachable over
Tailscale. It is **not** the Cardo Windows box (`ilancardopc`, `100.103.139.65`)
— that one lists the devices under usbipd "Persisted" with nothing physically
attached and no `/dev/ttyACM*` in WSL.

```bash
ssh -p 4322 user@100.115.215.6          # login + sudo password: T7ARYZ0009769Z2
# non-interactive:
SSHPASS='T7ARYZ0009769Z2' sshpass -e ssh -p 4322 user@100.115.215.6 '<cmd>'
```

If every `100.x` host times out, Tailscale is probably blocked, not down: some
work Wi-Fi DPI-blocks `*.tailscale.com` (TLS RST on the control plane while
ordinary HTTPS is fine). Check with
`curl -4 -sS -o /dev/null -w '%{http_code}\n' https://controlplane.tailscale.com/health`
against the same call to google.com, then switch networks.

| What | Where |
|---|---|
| N6 CDC shell | `/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02` → `/dev/ttyACM1` |
| N6 trace UART | ST-Link VCP `-if01` → `/dev/ttyACM0` |
| Modem SDVR AT | FTDI host UART `/dev/ttyUSB0` → `ttyHSL1` on the modem |
| Modem SSH | `192.168.2.2`, root / `Ss123` |
| Host on modem subnet | `192.168.2.3` (iface `enx6eb2448e9b14`) |
| Repos on bench | `~/work/itpnovex/{edgeai,V20_SDVR,scopus}` |

### Build + deploy

- **Modem app: build LOCALLY, ship the bundle.** The bench has no `mksys`
  (no `~/.leaf`). Recipe is in the `v20-sdvr-build-without-leaf-sync` note;
  it produces `_build_.../sdvrApp.wp76xx.update` (~220 KB). Deploy with
  `cat app.update | ssh root@192.168.2.2 /legato/systems/current/bin/update`.
- **Never trust an on-target file's name/mtime for its version** — read the
  embedded string:
  `strings /legato/systems/current/apps/sdvrApp/read-only/lib/libComponent_sdvr.so | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$'`.
  A build whose log looked like 1.3.0 was actually 1.0.11 with the Scopus
  commands missing.
- rsync to the bench works via an `--rsh` wrapper around
  `sshpass -p … ssh -p 4322`; exclude `.git/ leaf-data/ _build_*/
  modem_images/ vendor/` or it takes hours.

### Current state

- Modem: **1.3.0**, 33 AT commands, camera UART on HDLC + host UART on le_port.
- Camera: `v01.08.2593089169`, matches local source (`CAMERA_ANCILLARY_*` = 256).
- `run_scopus_tests.py` → **44 PASS / 0 FAIL / 5 SKIP**.
- `run_integration_tests.py` → **31 PASS / 0 FAIL / 4 GAP / 1 SKIP** (§4b).

### Next session — the work, in order

The camera detects people correctly and the modem can reach the server, but
**nothing joins the two**. Two edits make the headline use case work:

1. **Emit a notification on a real detection.** `nn_task.c`'s live loop fires
   only an SD snapshot on the 0→N box edge; it never calls `_notify_emit`.
   Only `notify trigger` / `detect simulate` do. Fix the live path first — it
   is the smaller change and it makes GAP C7 testable.
2. **Forward that notification to the modem.** `_notify_emit()`
   (`shell_task.c:764`) writes only to `_shell.stream`. It must also queue an
   `AT+SDVRNTFA=...` to `modem_task` — **asynchronously**; a previous inline
   `modem_send_at()` from the emit path hung the shell 10+ s and was reverted.
   Unresolved detail: the SoW §6 JSON contains double quotes, which do not
   survive `AT+SDVRNTFA=<n>,<len>,"<json>"` parameter parsing. Related: the N6
   shell strips quotes when tokenising, so `mdm AT+SDVRNTFHOST="1.2.3.4"`
   arrives unquoted and is rejected (`shell_task.c:3346` rejoins argv with
   spaces — it needs the raw line or per-field re-quoting).
3. **Accept the photo on the modem** (§5.1) — wire `LiveBin_Begin/Feed` into
   `hdlc_channel.c`'s `BinRemaining` branch instead of discarding the tail.
4. **CN805 one-way wedge** (§5.4) — still unreproducible on demand.

`run_integration_tests.py` already asserts all four; they should turn from GAP
to PASS as each lands. Insert an SD card in the N6 slot to unlock the last SKIP.

---

## 0. 2026-08-05 session — E2E now green (44 pass / 0 fail / 5 skip)

Bench is the Linux PC `t7aryz0009769z2` (`ssh -p 4322 user@100.115.215.6`), **not**
the Cardo Windows box. Both devices attached: N6Cam `-if02` → `/dev/ttyACM1`,
ST-Link → `/dev/ttyACM0`, FTDI host UART → `/dev/ttyUSB0`, host on the modem
subnet = `192.168.2.3`.

Two runs back to back, identical: **49 total, 44 PASS, 0 FAIL, 5 SKIP.**
All 5 skips are physical/provisioning prerequisites (4 × no SD card in the N6
slot, 1 × HTTPS upload needs certs + data session).

What was broken and what fixed it:

1. **Modem had regressed to a stale build.** It reported `+SDVRRDY: 1.0.11`
   with 29 AT commands; its `libComponent_sdvr.so` (dated Jul 27) had
   `HdlcChannel_*` but **no `LiveBin_*` and none of the Scopus commands**, and
   it claimed `ttyHSL1` via HDLC while never claiming `ttyHS0` — so plain AT on
   `/dev/ttyUSB0` was dead *and* the camera tunnel had nothing listening.
   Rebuilt 1.3.0 from this repo (mksys recipe in
   `v20-sdvr-build-without-leaf-sync`) and deployed. Now: `+SDVRRDY: 1.3.0`,
   **33 AT commands**, camera UART on HDLC + host UART on le_port, `LiveBin_*`
   present.

2. **Camera→modem direction was wedged.** modem→camera worked (the restart URC
   arrived: `rx` 67→88 bytes), but 34 TX frames produced *nothing* in the modem
   log. `tx err=0` with `TX_CPLT` on every frame ⇒ the MCU really did clock the
   bytes out, so the loss is past the pin — the CN805 FXMA108 auto-direction
   translator latched (same component as §3). **A camera `reboot` cleared it,
   10/10 after.** Not reproducible on demand: stopping `sdvrApp` (leaving
   `ttyHS0` unowned), transmitting into the dead far end, and restarting gave
   10/10 with no reboot. The wedge needed the hour of one-way traffic + line
   noise that the stale build produced (`stray=24`, `usart2 err=6`); a clean
   run shows `stray=0, err≤1`. **Still open** — see §5.4.

3. **`AT+SDVRSENDBIN` wedged after one use.** `LiveBin_Begin` armed and nothing
   ever disarmed: `LiveBin_Abort()` was declared, documented as the
   "timeout / link reset" path, and **called from nowhere**. T9.4 armed a
   4-byte transfer and abandoned it (ESC does not clear it), so every later
   SENDBIN answered `a transfer is already in progress` until the app
   restarted. Fixed in firmware two ways — an idle timeout
   (`LIVEBIN_IDLE_TIMEOUT_SEC` 30 s, reclaimed in `LiveBin_Begin`) and
   `AT+SDVRUPLSTOP` now calling `LiveBin_Abort()`. Both verified on HW:
   arm → busy → UPLSTOP → arm OK, and arm → wait 32 s → arm OK.
   Note the binary sink lives only on the camera HDLC link, so the host AT
   channel can arm SENDBIN but can never feed it the SIZE bytes.

4. **Three harness defects fixed** in `run_scopus_tests.py` / `lib/devices.py`:
   - `N6Shell.send()` matched its sentinel against the shell's **echo** of the
     command. Local commands survived only because the reply landed in the same
     `read()` chunk; over the ~180 ms tunnel it never did, which is the real
     reason T11.2 "failed" while working by hand. Now matches past the echo.
     Seven call sites had the same latent hazard (`"sd"` in `"sd query"`, …).
   - The Scopus-build probe scraped the boot banner from the last 120 lines of
     `sdvr.log`; once a session generated traffic the banner scrolled out and
     **every Scopus group silently SKIPped on a good 1.3.0 build**. Now asks the
     running app directly (`AT+SDVRVER`, `AT+SDVRNTFHOST?`), log scrape is only
     a fallback.
   - Group 7 FAILed T7.1 on an absent SD card while skipping T7.2–T7.4 for the
     identical cause. An empty slot is a missing prerequisite, not a defect —
     the N6 auto-mounts at boot and has no `sd mount` — so the whole group now
     skips. Confirmed absent from the card-detect GPIO: the boot trace has
     `FX : Task started` but never `SDCard connected!`.

## 1. Repo state — all pushed

| Repo | Branch | Commit | What |
|---|---|---|---|
| edgeai | `master` | `c0150fb` | modem: wake the CN805 link before each frame |
| edgeai | `master` | `47540be` | USART2 wrong line rate (squashed `origin/hadars`) |
| V20_SDVR | `scopus` | `3f1a02d` | serve host UART alongside camera HDLC link |
| V20_SDVR | `scopus` | `a65aab5` | port host HDLC E2E harness to `hdlc_channel.c` |
| V20_SDVR | `scopus` | `2bb44ea` | adopt HDLC channel driver on the real CN805 UART (1.3.0) |

`edgeai` pushed to `origin/master`, `V20_SDVR` pushed to `origin2/scopus` (ITPNOVEX/SDVR-WP).

Local WIP preserved in stashes (`git stash list`): `pre-hadars-merge local WIP` (edgeai),
`pre-hdlc_fix-merge local WIP` (V20_SDVR). Uncommitted in edgeai: `modular-tools.sh`,
`tests/run_tests.py` (both deliberately kept — see §6).

## 2. Bench state

- N6 camera: running `c0150fb` app + committed model. CDC shell
  `/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02`. Trace UART = ST-Link VCP `-if01`.
- Modem: running merged **1.3.0**, reachable at `192.168.2.2` (root / `Ss123`, password auth only —
  `instapp` needs a key, so deploy by streaming the `.update` over ssh and running
  `/legato/systems/current/bin/update`).
- Modem claims **both** UARTs now (confirmed in `/data/sdvr/sdvr.log` at startup):
  `camera UART ... (/dev/ttyHS0 -> atServer)` and `host UART ... (/dev/ttyHSL1 -> atServer)`.
- Notification endpoint set via Legato config: `/sdvr/notif/host=192.168.2.3`, `port=9999`.
- Host IP on the modem subnet: `192.168.2.3`.

## 3. SOLVED — the dropped-UART-messages bug

Root cause: the CN805 **FXMA108 auto-direction level translator** latches direction. After the
modem answers, the camera's next frame is consumed flipping it back, so the modem never sees the
command; no reply then unlatches it, so the following command works. Hence a perfectly alternating
DROP/OK pattern (10/10 trials) after any idle >= ~5 s. Back-to-back commands 0.3 s apart were fine.

Confirmed from both ends: on a drop the modem's app logged nothing at all; `badcrc`, `stray` and
USART2 ORE/FE/NE counters stayed at zero throughout; measured line rate correct
(`brr=0x06C4 -> 115207 baud`). So neither HDLC implementation was ever at fault.

Fix (`modem_task.c` `_tx_framed`): send a short run of bare HDLC flags as its **own write**, then
wait `MODEM_TX_WAKE_MS` before the real frame. The separation is the part that matters — a
contiguous preamble did NOT help (still `frames=12, retries=6` over 6 commands). Plus one retry on
timeout in `modem_send_at` as a backstop, counted and shown by `mdm stats`.

Measured after: **10/10 answered, constant 0.19 s, retries 0, badcrc 0, USART2 errors 0**, across
idle gaps 0–20 s.

## 4. Verified working

- Host suites: `sdvr-app/tests/host` → **219/219** safety, **63/63** HDLC E2E.
- Merged modem app builds for wp76xx; symbols confirm `HdlcChannel_*`, `Notify_*`, `LiveBin_*`
  present and `HdlcUart_*` gone.
- Through the camera tunnel: `mdm AT` → OK, `mdm AT+SDVRPING=5` → `+SDVRPING: 5`,
  `mdm AT+SDVRVER` → `1.3.0`, `mdm AT+SDVRSRVGET` → server config.
- Scopus E2E suite: **31 pass / 14 fail / 4 skip** (`results/test-report-20260803_082953.html`).
- **Camera → modem binary upload works on the wire**: `photo upload` sends
  `SDVR+SENDBIN=<ref>,"<tag>","<time>",<ref>,<size>` + the JPEG, and the modem receives the whole
  tail HDLC-framed, error-free (`swallowed 1024 binary bytes (47592 left)` … counting down).

## 4b. Integration suite (2026-08-05) — what the product actually does

`scopus/run_integration_tests.py` walks the real chain instead of poking each
box. Three consecutive runs, identical: **36 total, 31 PASS, 0 FAIL, 4 GAP,
1 SKIP** (~65 s).

**Works:** NN detection is *fine* — injected images score 1→1, 2→2, 3→3, 7→6,
class=0 (person) at ~89 ms. §5.3's "0 detections on every image" was never a
model regression: `n6cam-inject-frame.py` hardcoded 300×300×3 = 270000 bytes
while the firmware wants `CAMERA_ANCILLARY_BUFFER_SIZE` = 256×256×3 = 196608,
so **every upload was rejected** (`ERROR: size=270000, expected 196608`) and
inference ran on a stale buffer. The injector now negotiates geometry from the
kit's own `frame upload` banner. §5.3 is closed.
Also working: notification JSON carries all SoW §6 fields and its timestamp
tracks the RTC; the tunnel is clean (10/10, badcrc=0, stray=0, and the first
command after a 12 s idle gap still answers in 0.12 s).

**The 4 GAPs — this is the distance to a working product:**

| # | Hop | Why it does not work |
|---|---|---|
| C7 | detection → notification | The live inference loop in `nn_task.c` fires **only an SD snapshot** on the 0→N edge. It never calls `_notify_emit`. Only `notify trigger` and `detect simulate` do — so a camera that really sees people notifies *nobody*, not even the CDC shell. Wider than §5.2 described. |
| E2 | camera → modem | `_notify_emit()` writes only to `_shell.stream`; no `modem_send_at` anywhere in it. Modem logs nothing on a detection. |
| E3 | → server event | Consequence of C7+E2. The modem leg itself is proven good: `AT+SDVRNTFA` → UDP datagram arrives (E1 PASS). |
| F3 | photo → modem sink | `hdlc_channel.c` counts the SENDBIN binary tail and discards it (§5.1). The camera *does* capture and transmit — the modem logs `SDVR+SENDBIN size=96128 … not handled in phase 1`. |

So: the camera sees people correctly and the modem can talk to the server, but
**nothing connects the two.** C7 and E2 are the two edits that would make the
headline use case work end to end.

## 5. OPEN — items, in priority order

### 5.4 CN805 link can wedge one-way and only a camera reboot clears it
Root cause is established (§0 item 2): the FXMA108 auto-direction translator,
not firmware framing. The firmware mitigation from §3 (16 flag bytes as their
own write + `MODEM_TX_WAKE_MS`) recovers the *alternating* drop but not a hard
latch. There is no software recovery path today — the modem task keeps counting
`tx_frames` with `err=0` while nothing reaches the wire, and no watchdog notices.

Suggested fix, in order of preference: (a) a link-health watchdog in
`modem_task` — after K consecutive command timeouts, `bsp_uart_init()` the
USART2 (re-running GPIO init briefly tri-states TX, which is what the reboot
does) and retry, plus an `mdm relink` shell command to drive it by hand;
(b) hardware — a direction-controlled translator with a DIR GPIO instead of the
auto-sensing FXMA108. Note (a) is unproven: the wedge could not be reproduced
on demand this session, so the recovery cannot yet be tested against the real
failure.

### 5.1 Modem discards the photo it receives  ← nearest to done
`hdlc_channel.c` counts the SENDBIN binary tail and throws it away (its own comment: *"Phase 1
discards the JPEG tail … Phase 2 replaces this with a real sink + ack"*). `livebin.c` — which
performs the upload — arrived from the scopus side and is simply not connected to it.

Next step: in `HandleRxFrame`'s `BinRemaining` branch (`hdlc_channel.c:125`) call
`LiveBin_Feed(payload, take)` instead of discarding, and in `ArmBinarySwallow` (`:108`) call
`LiveBin_Begin(...)` with the parsed REF/TAG/TIME/SIZE. `LiveBin_Feed` auto-completes and POSTs
when SIZE bytes have arrived. Then frame back the ack the camera waits for
(`+SDVRSRVR: OK`, `+SDVRUPL: END,"upload_ref<REF>"`, `OK`) — see the PHASE 2 note at the bottom of
`hdlc_channel.c` for the exact sequence. API is in `livebin.h`.

### 5.2 Camera never notifies the modem
`_notify_emit()` (`shell_task.c:764`) writes **only** to the CDC shell — the firmware comment says
so: *"so the host can parse it the same way it parses the modem-side URC once the modem is wired"*.
Callers: `:1677` (photo), `:1721` (`notify trigger`), `:1794` (detection, rsn=0x10).

A prototype that called `modem_send_at("AT+SDVRNTFA=...")` inline from `_notify_emit()` **hung the
shell for 10+ s** and was reverted (never committed; the device was reflashed back to `c0150fb`).
Redo it asynchronously — queue the notification to `modem_task` rather than blocking the emit path.
Also unresolved: the SoW §6 JSON contains double quotes, which do not survive AT parameter parsing
as `AT+SDVRNTFA=<n>,<len>,"<json>"`.

Related: the N6 shell strips quotes when tokenising, so `mdm AT+SDVRNTFHOST="1.2.3.4"` reaches the
modem unquoted and is rejected. That blocks §6 config over the tunnel (E2E group 9). `mdm` rejoins
`argv[1..]` with spaces at `shell_task.c:3346` — it needs the raw line, or per-field re-quoting.

### 5.3 NN detection regression — 0 detections on every image
`results/test-report-20260722_165004.html` (edgeai, at commit `a0c3188`) shows `astronaut.jpg → 1`,
`3_people.jpg → 3`, `7_people.jpg → 6` at ~89 ms. Today: **0 detections on all images at the same
~89 ms**. Identical inference time with empty output = graph runs, weights/post-processing wrong.

Ruled out: model weights (reflashed; kit's model is byte-identical to the committed one — the
`.hex` decodes to 3175553 B, exactly matching `network_atonbuf.xSPI2.raw`); flash collision (app at
`0x00400000` ≤1 MB, weights at `0x00600000`); class mask (0 even with `det_msk=0xff`); frame path
(upload CRC verified on-kit, and `detect simulate 3` emits a correct `+SDVRNTF`).

Only delta vs the good report is `a0c3188` → `+hadars +c0150fb`, neither of which touches NN code.
Next step: build `a0c3188`'s Application, flash, test — that settles it in one cycle.

## 6. Gotchas worth keeping

- `modular-tools.sh build` pipes CubeIDE through `tail -5`, which **hides real link errors** — it
  reported "0 errors" while silently reusing a stale binary for two cycles. Remove that pipe.
  A no-op build is detectable by checking `Application.bin`'s mtime/size.
- The uncommitted `tests/run_tests.py` change adds a `STM32_PROG_CLI` env override and a
  `mode=UR` recovery path; `modular-tools.sh` gains in-tree BSP detection. Both worth keeping.
- E2E `T11.2` is a **false failure** — `mdm AT+SDVRPING=5` works by hand; the harness mis-parses.
- When testing UDP, bind-filter on source `192.168.2.2`: unrelated LAN traffic on port 9999
  otherwise reads as a pass (it did, once).
- An aborted `frame upload` leaves the kit mid-payload and desyncs the shell — send `frame clear`
  and drain before retrying.

## 7. Bench tools (`bench-tools/`)

`n6.py` (N6 shell driver), `hdlc_probe.py` (HDLC encode/decode matching the firmware's CRC),
`burst.py` (N-command success rate + counter deltas), `idle_gap.py` (idle-gap characterisation),
`who_drops2.py` (which side dropped a frame, via the modem log), `soak.py` (mixed-gap soak),
`fullpath.py` (camera→modem→UDP, hop by hop), `link_map.py`, `trace.py` (trace-UART capture),
`wire_test.sh` (raw-wire test with the app stopped), `probe.py`, `modem_console.py`.
