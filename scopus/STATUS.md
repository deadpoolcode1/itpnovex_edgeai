# Scopus integration — status / resume point

Last updated: 2026-08-09. Everything below was measured on the bench, not inferred.

---

## 2026-08-09 — cellular backhaul

The product can now be tested the way it is deployed: the modem sends over its
own cellular connection to a server on the internet, and the tester's PC — which
has no public address and is behind whatever router it is behind — watches what
arrives by *pulling* from that server. Neither end accepts an inbound
connection, so the procedure works from any network with nothing to configure
on a router.

| Piece | Where | What |
|---|---|---|
| `scopus/cloud_relay.py` | droplet `165.22.181.245` (`sds8200.duckdns.org`) | UDP notification sink + HTTP photo sink + pull API. systemd unit `scopus-relay`, ports 39999/udp and 38080/tcp, data under `~/scopus-relay/data` |
| `scopus/relay_pull.py` | the tester's PC | long-polls the relay, writes `scopus-received/` and prints the same lines `test_server.py` does |
| `scopus/at.py --point-cloud <ip>` | the bench | sets the five endpoints at the relay, sets the APN, `AT+SDVRNET=1`, waits for a **route** |
| `AT+SDVRNET` (app 1.8.0) | the modem | the cellular backhaul, as a thing you can switch on and look at |

**Reach the relay on port 80, not 38080.** A DigitalOcean cloud firewall in
front of that droplet passes inbound TCP 22/80/443 and nothing else — verified
with `tcpdump` on the droplet seeing *zero* packets on 38080 while the host's
own iptables is `ACCEPT`. Caddy therefore proxies `http://165.22.181.245/scopus/*`
to the relay (an IP-keyed site block, deliberately separate from the
`sds8200.duckdns.org` block so the hostname's automatic-HTTPS and ACME handling
are untouched). The upload endpoint is `http://165.22.181.245:80/scopus/upload`
and the pull base is `http://165.22.181.245/scopus`.

### Two things are blocked and both need someone else

1. **The SIM's data service carries nothing.** The modem registers on LTE
   (`HOTMOBILE` roaming, signal 5), gets an IP and a gateway on every APN
   tried — `novx`, `internet`, `sierra.iot`, `jtm2m`, `m2m.jtglobal.com` — and
   then `TX packets: 268, RX packets: 0`. No DNS, no TCP to 8.8.8.8:53, no
   ping to its own gateway. The eSIM is a JT Global profile (IMSI
   `234500040066501`, ICCID `89332500000003665094`). This is a
   subscription/provisioning matter with the SIM's operator, not a device
   fault: everything up to and including "packets leave the radio" works.
2. **UDP cannot reach the relay** until the droplet's cloud firewall gains an
   inbound rule for **UDP 39999**. Photos ride TCP 80 and are unaffected; this
   blocks only the notification leg. The relay's UDP listener itself is
   verified (a datagram sent from the droplet to its own public address is
   received and parsed).

### The SIM was in slot 2 all along

`AT+CPIN?` answered `+CME ERROR: SIM failure` and `!GSTATUS` said `NO IMSI`,
which reads exactly like an empty holder. The modem was selecting slot 1
(`AT!UIMS: 0`). `AT!ENTERCND="A710"` then `AT!UIMS=1` and a `AT+CFUN=0/1`
cycle → `+CPIN: READY`, ICCID, LTE registration. **Check `AT!UIMS?` before
concluding a SIM is missing.**

### What was actually proven end to end

A real 127,281-byte 800×600 JPEG went camera → modem → public internet →
relay → this PC, complete (`FFD8`…`FFD9`), pulled down by `relay_pull.py`.
Because the SIM carries nothing, that run used a host route through the bench
PC as a stand-in for the radio (`route add -host 165.22.181.245 gw 192.168.2.3
dev ecm0` on the modem, plus `ip_forward` + a MASQUERADE rule on the bench —
the route was removed afterwards so a "cellular" test cannot silently pass over
the cable; the bench NAT rules are still in place and are inert without it).
Everything except the radio hop is therefore proven against the real public
server. The notification leg was proven as far as the wire: `Notify_Send: 102
bytes sent over UDP` in the modem log, and `tcpdump` on the bench showing the
102-byte datagram leaving for `165.22.181.245:39999`.

### Firmware: 1.7.1 → 1.8.0

The Scopus paths never brought the network up. `Notify_Send()` and
`UploadFile_FromMemory()` — the two the use case actually runs on — went
straight to `sendto`/curl, while only the SD-upload paths called
`Network_Register()`, which is also the only thing that installs the default
route. On a unit with no cable that is `ENETUNREACH` and curl error 7 on a
perfectly capable modem.

- **`network.c`**: a keeper thread (`sdvrNetKeep`) owns the data session —
  brings it up on request, re-establishes it in auto mode every 30 s if the
  network drops it, with 10→300 s backoff. `Network_IsDataUp()` judges on the
  session **and** `/proc/net/route`, because "connected" without a route is the
  exact failure being fixed. `RegisterAndConnect()` is the old sequence minus
  the "server already reachable" short-circuit (the keeper must not skip
  cellular because the cable happens to work — the cable is what is about to be
  unplugged); `Network_Register()` keeps it. A `RegMtx` serialises the two
  callers.
- **The registration wait was dead code.** It registered an `le_mrc`
  state-change handler and waited on a semaphore, on threads that run no event
  loop — the callback could never fire, and only the already-registered fast
  path ever worked. Replaced with polling, which behaves the same on any
  thread.
- **`notify.c`**: on `ENETUNREACH`/`EHOSTUNREACH`/`ENETDOWN`, ask the keeper
  and return as before — no blocking. `AT+SDVRNTFA` is idempotent and the
  camera retries, so the retry lands once the link is up.
- **`upload_file.c`**: `UploadFile_FromMemory` waits up to 25 s for a link
  (free when it is already up).
- **`AT+SDVRNET=0|1` / `AT+SDVRNET?`**, `+SDVRNET: UP|ERROR <code>`, and
  `NETAUTO=1|0` in `tconf.ini`.

Verified on HW: `+SDVRNET: 1,1,1,1,"Sierra Wireless","LTE","m2m.jtglobal.com",
"rmnet_data0","10.123.5.12","10.123.5.13",0`, keeper log
`Network keeper: data session up`, and a `detect simulate 2` whose 102-byte
JSON reached `Notify_Send` and was sent — the send that used to fail.

`Scopus_Tester_Manual.docx` section 18 is the tester-facing procedure
(purely additive: 32 blocks inserted, 0 deleted). The relay key is **not** in
the repo — generate the tester's copy with
`SCOPUS_RELAY_KEY=... python3 scopus/make_tracked_manual.py`.

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
- Modem: **1.7.1**, 33 AT commands.
- `run_integration_tests.py` → **56 PASS / 0 FAIL / 0 GAP / 1 SKIP**, identical
  across three consecutive runs. The one SKIP is the absent SD card.
- `run_scopus_tests.py` → **44 PASS / 0 FAIL / 5 SKIP**.
- Host suites (`V20_SDVR/sdvr-app/tests/host`): **219/219** safety,
  **93/93** HDLC E2E (was 63 — 30 new assertions cover the SENDBIN sink).

Point the camera at people and the modem sends the event, and the photo
reaches a server. Verified hop by hop, by hand and by suite:
NN detection → `+SDVRNTF` on the shell → queued → `AT+SDVRNTFA` over the CN805
link → modem decodes → UDP datagram at `192.168.2.3` carrying valid §6 JSON;
and `photo upload` → SENDBIN → LiveBin → HTTP POST → a complete JPEG on the PC.

`Scopus_Tester_Manual.docx` (generated by `make_tester_manual.py`) is the
by-hand version of that, with `test_server.py` as the receiving end.

### The tester manual, and the three things walking it by hand found

The manual was rewritten on 2026-08-06 for a QA tester with no Linux: every
line is a single copy-paste command against fixed paths on the bench
(`cd ~/work/itpnovex/edgeai && …`), and it was walked end to end on
`t7aryz0009769z2` before being written down. Four helpers back it, all in
`scopus/`:

| Script | What it is for |
|---|---|
| `preflight.py` | 12 bench checks before the test, each with the fix under it |
| `cam.py "<cmd>"` | one camera shell command; resolves the by-id port, retries idempotent commands |
| `at.py --point-here` | sets the five endpoints from this PC's address on the modem subnet, then reads both back |
| `inference_test.py` | inject a known picture → `frame run`, with retries; asserts the count |

Three product problems surfaced that no automated run had caught, because the
suite drives the shell far faster than a person does:

1. **The camera watchdog-reset when the automatic link recovery fired.**
   FIXED — see §5. `mdm test wedge 9600` + three notifications reproduced it
   every time; the trace ended mid-way through `link wedged: … relinking` and
   the next boot said `Watchdog boot` / `Boot 1/3`.
2. **A command's console reply was dropped while a notification was in
   flight.** FIXED — see §5. Not reordered but absent: no echo, no output,
   nothing for 25 s, while the command itself ran. It made `frame run` fail
   ~50% of the time by hand.
3. **An injected frame only notifies if the live scene was empty.**
   `nn_task.c` fires on a 0→N box-count edge, and the live loop keeps
   `_nn_prev_boxes` at whatever the lens sees, so `frame run` on a 3-person
   picture raises nothing when the room already had people in it. This is
   correct by design and fatal to a manual test that assumes otherwise, so the
   manual proves inference (Step 7, injected picture) and delivery (Step 8,
   `detect simulate 3`) separately, and keeps the live walk-in-front as Step 9.

### Next session — what is actually left

0. **The two cellular blockers, both external** (see the 2026-08-09 section):
   a data subscription on the SIM that actually carries traffic, and an
   inbound **UDP 39999** rule on the droplet's DigitalOcean cloud firewall.
   With those two, section 18 of the tester manual runs as written — every
   other part of it is already proven against the real public relay.
1. **Put an SD card in the N6 slot.** It is the only SKIP left, and it unlocks
   the §7 photo→SD pipeline (G4).
2. **HTTPS.** The upload leg is proven over plain HTTP to a PC on the modem
   subnet. `AT+SDVRUPL*="S"` needs the client-cert set imported; T13.1 still
   skips on that. (The watchdog reset that stood at the top of this list was
   fixed on 2026-08-06 — see §5.)
3. **Watch `relinks` in the field.** The CN805 wedge is now recovered in
   software (§4.1) rather than needing a reboot, but the recovery has only ever
   been exercised against an injected fault — the real latch still cannot be
   reproduced on demand. A climbing `relinks` count on a deployed unit is the
   signal that it is happening for real, and that the hardware fix (a
   direction-controlled translator) is worth doing.
4. `mod`/`bat`/`vol` in the §6 body are still placeholders (`""`, `0.0`,
   `0.0`). The transport now carries them at full size (see §1), but nothing
   populates them — `bat`/`vol` need a real sensor source before they mean
   anything.

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

### 4.1 CN805 wedge — RECOVERED IN SOFTWARE (was the biggest open item)
The root cause is unchanged and is hardware: the FXMA108 auto-direction
translator latches, and the firmware cannot stop it happening. What has changed
is that it no longer needs a camera reboot to clear.

`modem_send_at` counts consecutive command timeouts — the only symptom a wedge
produces, since tx_frames keeps climbing with tx_errors at zero — and after
`MODEM_RELINK_AFTER` (3) re-initialises USART2. Re-running the GPIO init
briefly returns TX to its reset state, which is what lets the translator
re-sense direction; it is the software half of what the reboot did. `mdm
relink` drives it by hand, and `mdm stats` reports `link: relinks= consec=`.

**The recovery is proven, which it never could be before.** The real latch has
never been reproducible on demand, so the firmware carries a fault injector:
`mdm test wedge [baud]` re-configures USART2 to the wrong line rate. Frames
genuinely stop crossing the wire, and the thing that fixes it — a UART re-init
— is exactly what fixes the real one. Suite group I asserts the whole cycle.

That injector paid for itself immediately. The first version of the recovery
called `bsp_uart_init`, which **returns OK without doing anything** when the
UART is already up: it counted a relink, reported success, and changed nothing.
Only the injected fault exposed it. The BSP now has `bsp_uart_reinit`, which
redoes GPIO + peripheral and leaves the RTOS objects alone.

Still true: a hardware translator with a direction GPIO would remove the
failure rather than recover from it. This makes the failure survivable.

### 4.2 The photo is assembled but the upload has nowhere to go
`UploadFile_FromMemory` runs and fails — no provisioned certs, no data session.
That is the same prerequisite T13.1 skips on, not a defect in the transfer.

### 4.3 `mdm` quote loss — FIXED
Worse than "quotes stripped": lwshell's tokeniser replaces a quote found
mid-token with a NUL, so `mdm AT+SDVRNTFHOST="1.2.3.4"` reached the modem as
`AT+SDVRNTFHOST=` — the value gone, and the modem right to reject it. Since the
modem's own parser also rejects a bare dotted IP, every AT+SDVR* string setter
was unreachable over the tunnel.

`lwshell_raw_line()` now returns the untokenised line, and `mdm` forwards it
verbatim instead of rejoining argv. Suite group J asserts the round-trip.

### 4.4 Lossy acknowledgement path — FIXED by making NTFA idempotent
A lost ack is indistinguishable from a lost command, which forced a choice
between dropping events (never retry) and duplicating them at the server
(always retry). Neither is acceptable.

`AT+SDVRNTFA` now suppresses a repeat within 30 s, so the host can retry
freely. The key is the numerator **and** a hash of the payload, not the
numerator alone: N is 16-bit, wraps, and the camera restarts it at 0 every
boot, so the same N legitimately belongs to different events — keying on N
alone silently swallowed distinct notifications, which the suite caught within
one run. The camera's notifier uses `modem_send_at` (with retry) again.

`ntf_unconfirmed` remains as a measure of how lossy the ack path is; it no
longer implies a lost event.

## 5. Fixed on 2026-08-06 — the watchdog reset, and the lost console reply

Both were found by walking the tester manual by hand, and both were invisible
to the suite because it drives the shell faster than a person does and always
has a reader attached to the CDC port.

### 5.1 The camera reset whenever the link recovery fired

**Reproduction** (deterministic, 3/3 before the fix):
`mdm test wedge 9600`, then three `detect simulate 3`. The third notification
takes `consec_timeouts` to `MODEM_RELINK_AFTER`, and the board resets: uptime
went 2747984 → 11509 ms, the trace stopped part-way through printing the
`link wedged` line, and the next boot reported `SYSTEM : Watchdog boot`.

**Why it only happened automatically.** `mdm relink` does the same work and
never reset the board (3/3). The difference is which thread runs it:

- `_relink_locked()` was called from inside `_send_at`, i.e. on whichever
  thread happened to time out. For a notification that is `modem.notify` —
  a **2 KB** stack that was already holding `_send_at`'s 1 KB `payload` and
  the notifier's 512-byte `line` when HAL's UART de-init/re-init frames and a
  trace call went on top of it. The truncated log line is what that looks
  like from outside.
- It also re-initialised USART2 while `_modem_task_run` was blocked inside
  `bsp_uart_read` on that same peripheral.

**Fix.** The thread that notices a wedge now only *asks* for recovery
(`_m.relink_pending`); `_modem_task_run` performs it at the top of its loop,
which is the one context with no read in flight and a stack that can take it.
It uses `tx_mutex_get(TX_NO_WAIT)` — blocking for `tx_mtx` there would stop
the loop that receives the response the current holder is waiting for, and
would turn one wedge into a permanent one. `payload` and `line` are static
now as well (both are covered for their whole lifetime by `tx_mtx` and by
single-thread ownership respectively), and the notifier stack is 4 KB.

Recovery is therefore **asynchronous**: it happens on the modem task's next
pass, within its 1 s read timeout — measured at 555 ms. Group I3 polls for it
rather than reading the counter once, which is also a stronger assertion than
before ("it relinks promptly", not "it relinked by the time we looked").

Verified after the fix: uptime climbs straight through the same reproduction,
`relinks` increments, USART2 returns to 115200, and `mdm AT` answers again.

### 5.2 A command's reply vanished while a notification was in flight

`_cdc_write()` took a `timeout` argument and ignored it (`UNUSED(timeout)`),
so a write blocked until the host drained the endpoint — **forever** if
nothing had the port open. The camera emits notifications on its own, so a
detection with no terminal attached parked a writer inside USBX holding
`_cdc_mtx_tx`, and every later write queued behind it: the echo, the command's
own output, everything. It looked like the camera ignoring a command when it
had run it and had nowhere to say so, and it cleared the instant anything read
the port — which is what made it look intermittent rather than structural.

The fix honours the caller's timeout via
`UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_WRITE_TIMEOUT`, exactly as `_cdc_read()`
already did for reads, and treats the expiry (`TX_NO_INSTANCE`) as "wrote
nothing" instead of falling through to `Error_Handler()`. Callers already pass
sensible values: 1000 ms for shell output and echo (`SLIB32_STREAM_PRINT_TIMEOUT`),
100 ms for notifications. A console must not depend on somebody listening;
dropping a line nobody is there to read beats stalling the shell that produced
it.

Measured: `detect stop` → inject → `detect start` → `frame run` returned the
full result **4/4** first time, against roughly one in three before.

### 5.3 `mdm stats` contradicted itself after a relink

`_relink_locked()` cleared `_m.consec_timeouts` but not its copy in `stats`,
so a link that had just been recovered still reported the count that triggered
the recovery. The tester manual asks for these counters, so it now clears both.
