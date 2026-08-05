# Scopus integration — status / resume point

Last updated: 2026-08-05. Everything below was measured on the bench, not inferred.

---

## RESUME HERE (next session)

### Where everything lives

There is **one** Scopus directory: `edgeai/scopus/`, tracked in the **edgeai**
repo on `master`. A second copy used to sit at `itpnovex/scopus/` (untracked,
byte-identical); it was removed on 2026-08-05, locally and on the bench. Do not
recreate it — the suite drives both boxes but has to be versioned somewhere,
and edgeai is where it is committed.

| Piece | Repo | Branch | Path |
|---|---|---|---|
| Camera firmware (N6Cam, Main CPU) | `edgeai` | `master` | `vendor/n6cam.core.bsp/Firmware/Application/` |
| Modem app (WP76 / MangOH) | `V20_SDVR` | `scopus` | `sdvr-app/components/sdvr/src/` |
| Integration + whole-system suites | `edgeai` | `master` | `scopus/` |

Remotes: edgeai → `origin` (`deadpoolcode1/itpnovex_edgeai`).
V20_SDVR → `origin` (`deadpoolcode1/V20_SDVR`) and `origin2`
(`ITPNOVEX/SDVR-WP`); the Scopus work goes to `origin2/scopus`.

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
| N6 CDC shell | `/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02` (ttyACM1 **or** ttyACM2 — it moves across reflashes, always resolve the by-id link) |
| N6 trace UART | ST-Link VCP `-if01` → `/dev/ttyACM0` |
| Modem SDVR AT | FTDI host UART `/dev/ttyUSB0` → `ttyHSL1` on the modem |
| Modem SSH | `192.168.2.2`, root / `Ss123` |
| Host on modem subnet | `192.168.2.3` (iface `enx6eb2448e9b14`) |
| Repos on bench | `~/work/itpnovex/{edgeai,V20_SDVR}` — **edgeai there is an rsync copy, not a git repo.** Develop locally, ship artifacts. |
| CubeProgrammer on bench | `~/stm32prog/bin/STM32_Programmer_CLI` (copied there 2026-08-05; needs `LD_LIBRARY_PATH=~/stm32prog/lib` and sudo for USB) |

### Build + deploy

**Camera** — build locally (CubeIDE is at `/opt/st/stm32cubeide_1.19.0`), then
flash **over CDC**, which needs no SWD and no boot switch:

```bash
./modular-tools.sh build            # honest now: fails on link errors, says
                                    # whether each artifact was actually rebuilt
scp .../Application_signed.bin user@bench:/tmp/          # see the trap below
ssh bench 'cd /tmp && python3 n6cam-update.py --target app Application_signed.bin \
    $(readlink -f /dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02)'
```

Three traps, each of which cost a cycle:

- **`Application_signed.bin` is mode `r--r--r--`.** A second `scp` onto an
  existing copy fails with permission denied. `rm -f` the target first, and
  compare CRC32 on both ends — `n6cam-update.py` prints the CRC it is about to
  send, so a stale flash is visible if you look.
- **SWD is not available**: the ST-Link is healthy (reports 3.30 V) but every
  connect mode answers `Unable to get core ID`, because the kit's boot switch
  is in operation mode. Flashing over SWD would need someone at the bench to
  move it. The CDC path above avoids the question entirely.
- **The CDC port re-enumerates after a flash** and can come back as a different
  `ttyACM<n>`. Wait ~30 s and resolve the by-id symlink again, or the harness
  dies with `OSError(5)`.

**Modem** — the bench has no `mksys` (no `~/.leaf`), so build locally and ship
only the ~220 KB bundle:

```bash
export LEGATO_ROOT="$HOME/.leaf/wp76-legato_20.04.0-202004151904"
export PATH="$LEGATO_ROOT/bin:$PATH"
TCROOT="$PWD/leaf-data/wp76-toolchain_SWI9X07Y_02.28.03.05-linux64"
export WP76XX_TOOLCHAIN_DIR="$TCROOT/sysroots/x86_64-pokysdk-linux/usr/bin/arm-poky-linux-gnueabi"
export WP76XX_TOOLCHAIN_PREFIX="arm-poky-linux-gnueabi-"
export WP76XX_SYSROOT="$TCROOT/sysroots/armv7a-neon-poky-linux-gnueabi"
mksys -t wp76xx sdvr.sdef -o _build_sdvr_v150 -w _build_sdvr_v150/wp76xx
cat _build_sdvr_v150/wp76xx/app/sdvrApp/sdvrApp.wp76xx.update \
  | ssh root@192.168.2.2 /legato/systems/current/bin/update
```

**Never trust an on-target file's name/mtime for its version** — read the
embedded string:
`strings /legato/systems/current/apps/sdvrApp/read-only/lib/libComponent_sdvr.so | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$'`.
A build whose log looked like 1.3.0 was actually 1.0.11 with the Scopus
commands missing.

### Current state — the headline use case works

- Camera: local `master` build with the notification path wired (see §1).
- Modem: **1.5.0**, 33 AT commands.
- `run_integration_tests.py` → **46 PASS / 0 FAIL / 0 GAP / 1 SKIP**, identical
  across three consecutive runs. The one SKIP is the absent SD card.
- `run_scopus_tests.py` → **44 PASS / 0 FAIL / 5 SKIP**.
- Host suites (`V20_SDVR/sdvr-app/tests/host`): **219/219** safety,
  **93/93** HDLC E2E (was 63 — 30 new assertions cover the SENDBIN sink).

Point the camera at people and the modem sends the event. Verified hop by hop:
NN detection → `+SDVRNTF` on the shell → queued → `AT+SDVRNTFA` over the CN805
link → modem decodes → UDP datagram at `192.168.2.3` carrying valid §6 JSON.

### Next session — what is actually left

1. **Put an SD card in the N6 slot.** It is the only SKIP left, and it unlocks
   the §7 photo→SD pipeline (G4).
2. **§5.4 CN805 one-way wedge** — still open, still not reproducible on demand.
   Nothing this session made it worse; the link now carries two producers and
   still measures `badcrc=0 stray=0`.
3. **Commit and push.** All the work below is on disk and **uncommitted** in
   both repos.
4. Consider a real HTTPS session so `UploadFile_FromMemory` completes rather
   than failing after the photo is assembled (T13.1 / F-group tail).

---

## 1. What changed on 2026-08-05 (the four GAPs, closed)

The suite reported 31 PASS / 4 GAP. All four GAPs were the same missing idea:
the camera and the modem each worked, and nothing joined them.

### C7 — a real detection now notifies (`nn_task.c`)

The live inference loop fired only an SD snapshot on the 0→N box edge and never
raised a notification; only `notify trigger` and `detect simulate` did. It now
calls `shell_notify_emit(0x10, count, false)` on that edge when the profile's
action mask has bit1 ("report") set — the same rsn/rsd pair `detect simulate`
reports, so a simulated and a real detection are indistinguishable downstream.

### E2 — the notification reaches the modem, asynchronously (`modem_task.c`)

`_notify_emit()` wrote only to `_shell.stream`. It now also composes an
`AT+SDVRNTFA` and hands it to **`modem_notify_async()`**, a new queue drained by
a dedicated `modem.notify` thread.

The thread is the point. `modem_send_at` blocks up to `MODEM_AT_TIMEOUT_MS` per
attempt; the previous prototype called it inline and froze the shell 10+ s. It
also cannot live in `modem_task`'s own loop — that loop is what *receives* the
response `modem_send_at` waits for, so a call made from it could never complete.

New counters on `mdm stats`: `ntf: queued= sent= unconfirmed= dropped=`.

### The JSON-through-AT problem — why substitution, and why chunked

The §6 body is JSON; JSON is full of the two characters an AT line uses as
structure. Measured against the real modem:

| Encoding | Result |
|---|---|
| `"{"ser":1}"` naive | **ERROR**, nothing sent |
| `\"` backslash-escaped | **ERROR** |
| unquoted | **ERROR** |
| `""` doubled quotes | OK — but arrives as `{ser:1}`, silently **not JSON** |
| single-quote JSON | intact, but still not JSON |
| hex | intact |

Only hex and a 1:1 substitution survive. The second constraint decides between
them: **atServer caps one parameter at 128 bytes** (measured: 128 accepted,
129 a clean ERROR).

That cap is not comfortable — it is the whole reason the payload is chunked.
Measured sizes of the body as the camera composes it:

| Body | Size |
|---|---|
| today (`mod` empty, `bat`/`vol` = placeholder `0.0`) | 100 B |
| all numerics maxed, `mod` still empty | 124 B |
| real `bat`/`vol`, `mod` empty | **128 B** — exactly at the cap |
| real `bat`/`vol`, `mod` 8 chars | 136 B — **over** |

So a single-parameter payload works only while three §6 fields are
placeholders, and breaks silently the moment a product fills them. Hex is worse
still: at 2:1 it needs two parameters for even today's 100-byte body.

Hence `AT+SDVRNTFA=<n>,<size>,"<part>"[,"<part2>"…][,<ENC>]`, with `ENC` always
last: `ENC=1` means "rejoin parameters 2..ENC-1 and restore backtick → `"`".
`ENC` absent or `0` is the legacy verbatim single-parameter form, so every
pre-existing caller is untouched (T9.3 still passes). The camera splits at 128
bytes; the modem's `NTFA_PAYLOAD_MAX` is 384, bounded by the 512-byte AT line
cap. Handled in `at_handler.c: Handle_NtfA` and `shell_task.c: _notify_emit`.

Pinned by **group H** of `run_integration_tests.py` (11 assertions: legacy
forms, single chunk, multi-chunk, the exactly-128 boundary, malformed ENC), all
judged on the received datagram rather than the AT reply — every failure mode
here answers `OK` on the AT channel.

Protocol reference for integrators: `V20_SDVR/README.md` →
*Notifications & Control channel*.

### F3 — the modem keeps the photo (`hdlc_channel.c`)

`ArmBinarySwallow` counted the SENDBIN tail and threw it away. It now parses all
five fields (quote-aware, so a comma inside `"TAG"`/`"TIME"` does not split a
field), calls `LiveBin_Begin`, feeds each frame to `LiveBin_Feed`, and frames
the ack back — `+SDVRSRVR: OK`, `+SDVRUPL: END,"upload_ref<REF>"`, `OK` —
straight onto the UART, since no atServer command sits behind those lines.

Measured on HW: `LiveBin_Begin: armed for 96956 bytes` → `LiveBin_Feed: 96956
bytes received, uploading` → `UploadFile_FromMemory`.

If arming fails the bytes are still counted off the wire and discarded, because
otherwise the decoder resumes mid-JPEG and reads image data as AT text. Both
paths are covered by two new host tests.

## 2. Three things this session got wrong first — worth not repeating

- **`ntf_failed` was a lie.** The camera counted several notifications as
  failed; the modem log showed **64 of 64** `AT+SDVRNTFA` decoded and 63 sent
  over UDP. The loss is on the *return* path — the ack, not the event. Renamed
  `ntf_unconfirmed`, and the notifier deliberately does **not** retry: the
  command almost certainly arrived, so a resend would duplicate the event at
  the server. Treat a non-zero count as "the ack path is lossy".
- **Async notifications desynced the shell.** With detection left running, the
  NN task emits `+SDVRNTF` between a command and its response. Harmless for
  text, fatal during a bulk binary transfer: the host streams a fixed byte
  count and reads for a banner, sees the notification instead, and abandons the
  upload mid-payload — which desyncs every later command. A second suite run
  scored 27/36 against the first run's 35/36 on identical firmware.
  Fixed twice over: `_shell_binary_rx` suppresses the *CDC copy* (never the
  modem leg) for the length of a transfer, and the suite now calls
  `quiesce_detector()` before the first group and after the last.
- **Group D measured a link it no longer owned.** D6/D7 assume the shell is the
  only producer on the CN805 link; the notifier is now a second one, and its
  frames and retries were being attributed to D's own commands. The suite now
  calls `wait_for_notify_drain()` first — the ntf counters make "quiet"
  observable rather than assumed.

## 3. SOLVED earlier — the dropped-UART-messages bug

Root cause: the CN805 **FXMA108 auto-direction level translator** latches
direction. After the modem answers, the camera's next frame is consumed flipping
it back, so the modem never sees the command; no reply then unlatches it, so the
following command works. Hence a perfectly alternating DROP/OK pattern (10/10
trials) after any idle >= ~5 s. Back-to-back commands 0.3 s apart were fine.

Fix (`modem_task.c` `_tx_framed`): send a short run of bare HDLC flags as its
**own write**, then wait `MODEM_TX_WAKE_MS` before the real frame. The
separation is the part that matters — a contiguous preamble did NOT help. Plus
one retry on timeout in `modem_send_at` as a backstop.

## 4. OPEN

### 4.1 CN805 link can wedge one-way and only a camera reboot clears it
The firmware mitigation from §3 recovers the *alternating* drop but not a hard
latch. There is no software recovery path today — the modem task keeps counting
`tx_frames` with `err=0` while nothing reaches the wire, and no watchdog
notices.

Suggested fix, in order of preference: (a) a link-health watchdog in
`modem_task` — after K consecutive command timeouts, `bsp_uart_init()` the
USART2 (re-running GPIO init briefly tri-states TX, which is what the reboot
does) and retry, plus an `mdm relink` shell command to drive it by hand;
(b) hardware — a direction-controlled translator with a DIR GPIO instead of the
auto-sensing FXMA108. Note (a) is unproven: the wedge could not be reproduced
on demand, so the recovery cannot be tested against the real failure.

### 4.2 The photo is assembled but the upload has nowhere to go
`UploadFile_FromMemory` runs and fails — no provisioned certs, no data session.
That is the same prerequisite T13.1 skips on, not a defect in the transfer.

### 4.3 `mdm AT+SDVRNTFHOST="1.2.3.4"` still loses its quotes
The N6 shell strips quotes when tokenising, so the command reaches the modem
unquoted and is rejected. `mdm` rejoins `argv[1..]` with spaces in
`shell_task.c` — it needs the raw line, or per-field re-quoting. Does not affect
the notification path, which composes its own AT line.

## 5. Gotchas worth keeping

- `modular-tools.sh build` **used to** pipe CubeIDE through `tail -5`, which hid
  real link errors — it reported "0 errors" while silently reusing a stale
  binary for two cycles. Now fixed: output goes to
  `/tmp/cubeide_ws_n6cam/build-<project>.log`, errors are grepped and fail the
  build, and each artifact is reported as `rebuilt` or `UNCHANGED`. An
  UNCHANGED FSBL is normal when only Application sources were touched.
- E2E `T11.2` is no longer a false failure — `N6Shell.send()` matches past the
  shell's echo.
- When testing UDP, bind-filter on source `192.168.2.2`: unrelated LAN traffic
  on port 9999 otherwise reads as a pass (it did, once).
- An aborted `frame upload` leaves the kit mid-payload and desyncs the shell —
  send `frame clear` and drain before retrying. `quiesce_detector()` does this.
- The integration suite writes JSON to `scopus/results/` (gitignored).
  `NTF_PORT` defaults to 5005; the bench runs used `NTF_PORT=9999`.

## 6. Bench tools (`bench-tools/`)

`n6.py` (N6 shell driver), `hdlc_probe.py` (HDLC encode/decode matching the
firmware's CRC), `burst.py` (N-command success rate + counter deltas),
`idle_gap.py` (idle-gap characterisation), `who_drops2.py` (which side dropped a
frame, via the modem log), `soak.py` (mixed-gap soak), `fullpath.py`
(camera→modem→UDP, hop by hop), `link_map.py`, `trace.py` (trace-UART capture),
`wire_test.sh` (raw-wire test with the app stopped), `probe.py`,
`modem_console.py`.
