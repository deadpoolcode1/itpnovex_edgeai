# Scopus integration — status / resume point

Last updated: 2026-08-19. Everything below was measured on the bench, not inferred.

---

## 2026-08-19 (later) — motion means the BOX, not the scene

Reuven, by email the same afternoon:

> the motion detection refers to motion sensor that exists on the camera to
> identify movements of the entire board (box). With sensitivity and timeout
> for the "no motion" condition after which motion stop notification should be
> reported (if enabled). At this phase, motion tracking of people or vehicles
> within the frame is not required.

That is not what shipped this morning. Closing ScopusQA #5 gave §4.2 bits 1 and
2 a producer for the first time, and the producer was the detector's debounced
object count crossing zero — so a person walking past a bolted-down camera
reported that the camera was being carried away. The §4.5 wording ("motion
sensor, set sensitivity and timeout without motion") was read as a description
of the detector, and it is not: **the board has an inertial sensor**, an
LSM6DSO32 6-DOF IMU on the sensors I2C, listed in the SIANA datasheet and
already probed by the BSP's own board self-test at 0xD7. `motion sense` had
been storage-only since M1 (W16 "done early"), so nothing ever read the part.

### What the camera does now (build `Aug 19 2026 16:55:13`)

New `motion_sensor.c` owns the sensor and the state machine; `nn_task.c` no
longer emits 0x02/0x04 at all and reports only people (0x10) and vehicles
(0x20).

The detector has **two legs**, because one alone misses a real class of
movement:

- the LSM6DSO32's own wake-up function — slope filter, threshold in hardware,
  latched (`TAP_CFG0` LIR + INT_CLR_ON_READ) so a knock that starts and ends
  between two polls is still there when we look;
- a software comparison of the acceleration vector against its slow resting
  average, which catches a lift or a tilt that a slope filter is deliberately
  blind to. The reference follows the box while it is still (EMA, ~3 s) and is
  **frozen during motion** — tracking then would chase the signal being
  measured — and is re-learnt on the stop edge, so a unit left at a new angle
  settles instead of reporting for ever.

`SLOPE_FDS` stays 0 on purpose: routing the HPF into the wake-up path would
also high-pass the *output* registers, and `motion read` showing real gravity
is the cheapest proof the part is alive and mounted the way we think.

Sensitivity maps linearly onto the sensor's own threshold units, inverted, with
`WAKE_UP_DUR.WAKE_THS_W` set so one LSB is FS/256 = 15.6 mg rather than
FS/64 = 62.5 mg — without that the *gentlest* setting the hardware can express
is already a firm knock. Measured: 100 → 15 mg, 50 → 265 mg, 0 → 500 mg,
against a resting noise floor of **2-5 mg** (90 s at maximum sensitivity, zero
false events).

`rsd` now carries something: on start, the deviation that opened the episode in
mg; on stop, how long the episode lasted in seconds.

**`mtn` is no longer a parameter.** It was passed in by each caller and meant
"this event came from the motion path"; §6 says it is the motion state, so it
is now read from the sensor when the body is composed and describes the box on
*every* notification. `shell_notify_emit(rsn, rsd)` lost its third argument.

### The bench cannot be shoved, so the sensor shoves itself

`motion selftest` runs the part's electrostatic self-test, which deflects the
proof mass for real. The step travels the whole path — filter, threshold, state
machine, notification — so it is a genuine end-to-end stimulus with nobody near
the box. Measured shift 681-687 mg against a 265 mg threshold. There is also
`motion simulate 0|1`, which asserts the state and skips the sensor; it proves
the transport only, and the manual says so.

### Measured, end to end, on the cable

```
motion read                → x=986 y=-77 z=80 mg          (gravity, |a|=991)
motion selftest            → sensor responded (shift 681 mg)
  server: {"rsn":2,"rsd":688,"tim":"20260819170215","mtn":1,…}
  15 s later (timeout 15): {"rsn":4,"rsd":15,"mtn":0,…}
detect simulate 3          → {"rsn":16,"rsd":3,"mtn":0,…}   and nothing else
notify trigger 8 while simulated-moving → {"rsn":8,"mtn":1,…}
motion sense 100/50/0      → threshold 15 / 265 / 500 mg
reboot                     → sensor found and armed from the persisted values
```

Two traps worth recording:

- **`notify enable` had 0x30 on the unit**, so the first end-to-end attempt
  produced `start=1 stop=1` on the camera and nothing at the server. That is
  the mask working as intended (enforced since this morning), and it looks
  exactly like a dead transport. Check `notify query` before suspecting the
  link.
- The suite's group D (`10 consecutive tunnelled commands`) scored 9/10 on one
  run and 10/10 on the next two. That is the known CN805 flakiness this test
  exists to watch, not a regression from this work.

### Suite

`run_integration_tests.py` gained **group K** (7 tests): sensor present, reads
gravity, sensitivity inverts and bounds the threshold, self-test moves the
mass, movement raises `rsn=2` at the server, stillness raises `rsn=4`, and — the
guard against this whole confusion coming back — a detection raises `rsn=16`
and **neither** motion code. Clean run: **64 total, 62 PASS / 0 FAIL / 1 GAP /
1 SKIP**. The GAP is still C7 (a real NN detection notifying, refused for
injected frames by design) and the SKIP is still the absent SD card.

Tester manual: new section 20 (Steps M1-M3).

---

## 2026-08-19 — the bench came back dead after a power-cycle, and the five QA issues

### The power-cycle failure Omer reported

Omer unplugged both devices, plugged them back in, and `preflight.py` then
failed two checks while `at.py --point-here` answered every command with
`Login incorrect` under a `swi-mdm9x28-wp login:` prompt.

Root cause, proved rather than guessed: **`sdvrApp` was not installed on the
modem at all.** `/legato/systems/current/apps/` listed 24 stock Legato apps and
no `sdvrApp`; the system index was 47 with status `good`; and
`/sbin/getty ttyHSL1 115200 vt100` was running. That getty *is* the login
prompt in his log — `ttyHSL1` is shared between the kernel console and the
SDVR app's `le_port` claim, so with the app gone nothing takes the port from
getty and every `AT+SDVR…` line lands on a login prompt.

This is the same post-power-cycle rollback seen on 2026-08-16 (system 42 that
time). A Legato install that is never marked good reverts on the next reboot.
Cure, as one step: install the locally built `.update`, wait for
`app status` to list it, then `update --mark-good` **while still in
probation**.

Restored to 1.13.0, marked good (system 49), and `preflight.py` is back to
**12/12 PASS** — including the CN805 camera→modem link, which needed no
`mdm relink` this time.

### ScopusQA issues #3-#7

| # | Report | What it actually was | State |
|---|---|---|---|
| 3 | "Is there a query for camera settings?" | Each sub-command already answered with the current value when given no argument, but there was no way to see all six at once | **New `camera status`** — sensor, ISP, flip, AEC, AWB, gain, exposure, brightness in one reply |
| 4 | "Only the latest image/notification appears" | **Ours, and a one-word bug.** The camera passed the literal `"photo"` as the SENDBIN tag while the unique §7 name sat in the `filename` argument, which the UART path ignores. The tag becomes the modem's `X-Filename`, so the receiver wrote one file called `photo` every time. Notifications carried **no** `X-Filename` at all, so the receiver defaulted to `upload.bin` | Fixed both ends |
| 5 | "Not all notifications are received" | Correct, and worse than reported: the enable mask was **stored and never read by any emitter**, and 5 of the 6 §4.2 events had no producer at all | all six now emitted, mask now enforced |
| 6 | "camera brightness — Not supported on IMX335!" | Correct answer, useless phrasing. The IMX335 driver has no `set_brightness`; on an ISP part exposure/gain/AEC are the brightness controls | Message now names the alternatives |
| 7 | "camera awb — range is only 0-2, not 0-5" | The help text was wrong, not the firmware. The profile table comes from the sensor's ISP tuning file; IMX335 has three | Error names the real range; help and docs no longer promise a fixed 0..5 |

**#4 — the fix.** `snapshot_request_upload("photo", ref, fname)` →
`snapshot_request_upload(fname, ref, fname)` in both call sites
(`shell_task.c` `photo upload`, `nn_task.c` auto-upload), plus `SNAP_TAG_MAX`
31→63 so a 10-digit serial cannot truncate the name. On the modem,
`notify.c` now builds `ntf_<modem clock>_<counter>.json` and sends it as
`X-Filename`. Proved over cellular against the customer's receiver:

```
POST /upload  name=4194336_01012000_000245.rdy   bytes=152324
POST /notify  name=ntf_20260819111902_000.json   bytes=99
POST /notify  name=ntf_20260819112032_001.json   bytes=99
POST /notify  name=ntf_20260819112032_002.json   bytes=100
```

Note the photo's `01012000`: **the camera's RTC is unset**, so the date half of
the §7 name is epoch. Uniqueness still holds second-to-second, but two photos
inside one second would collide, and the name carries no useful date. `rtc set
DDMMYYYYHHMMSS` fixes it per boot; syncing the camera's RTC from the modem
(which has real time — see the `ntf_` names above) is the proper fix and is not
done.

**#5 — the mask was decorative.** `_notify_emit` never looked at
`notify_enable_mask`. Enabling a bit changed nothing; disabling one changed
nothing. It is now enforced, with two deliberate exceptions: `notify trigger`
always sends (it exists to test the transport), and the photo event `0x40` is
outside the §4.2 table. Registry default is now `0x3F` and `REGISTRY_VERSION`
is 6 — silence by default is the one failure mode a field unit cannot report.

Producers added: `0x01` from the modem's `+SDVRNET: UP` **and** its `+SDVRRDY`
banner (the "on power up / reset" half of §4.2 — the transition-only URC never
fires when the app restarts while already registered, which would have left the
event untestable on a bench that is always in coverage); `0x02`/`0x04` from the
debounced detection count crossing zero, shared with `detect simulate` so the
bench can exercise them without walking people past the lens; `0x08` from
`notify period <s>` on the shell task's turn.

Measured on hardware:

```
notify period 10   → rsn=8 at 00:01:52, 00:02:02, 00:02:13
detect simulate 3  → rsn=2 rsd=3 mtn=1, then rsn=16 rsd=3
   (3 s later)     → rsn=4 rsd=0 mtn=1, then rsn=16 rsd=0
app restart sdvrApp→ +SDVRRDY: 1.13.0, then rsn=1
```

**`0x20` vehicle detected — I got this wrong first time and it is worth
recording why.** I read `MULTICLASS_STATUS.md` (2026-05-23), saw "only class 0
(person) ever fires", and concluded the event could not be delivered without a
new model. That document describes the *relu30* experiment and is three months
stale. The kit has since moved to the **`pv` model** — `network_generate_report.txt`
records `generate --model pv_epoch0.onnx`, the output is (84,1344) = 4 box + 80
COCO classes, and `_class_passes_mask()` in `nn_task.c` has mapped COCO 1-8
(bicycle, car, motorcycle, bus, truck, airplane, train, boat) onto the vehicle
bit for as long as `detect profile <det_msk>` has existed. Vehicles were being
**detected and counted** all along.

The actual bug was one line further on: the report hardcoded
`shell_notify_emit(0x10, _nn_stable_boxes)` — every detection was announced as
"people" whatever the model saw, and nothing ever emitted `0x20`. Fixed by
splitting the filtered box buffer per class while it is being filtered
(`_nn_people_now` / `_nn_vehicles_now`) and reporting each under its own reason
code, each only when its own count changed. Two notifications rather than one
`rsn=0x30`, because a single event carries one `rsd`.

`detect simulate <N> [people|vehicle]` now takes a class, for the same reason
the motion edges are shared with it: there is no car to point the lens at.

```
detect profile 0x03 0x02
detect simulate 2 vehicle  → rsn=2 rsd=2 mtn=1, then rsn=32 rsd=2
   (3 s later)             → rsn=4 rsd=0,       then rsn=32 rsd=0
detect simulate 3          → rsn=16 rsd=3
```

**Lesson: check the model that is actually built in, not the status document.**
`Model/network_generate_report.txt` names the ONNX the firmware carries;
`Model/_backup_person_only/` exists precisely because the current one is not it.

### The camera RTC now sets itself from the modem

The camera has no battery-backed clock, so every power cycle started it at
2000-01-01 — and that clock stamps both the §7 photo name and the §6 `tim`
field, so every uploaded file was dated to the epoch. The modem has real time
from the network and is on the other end of a UART we own.

`rtc sync` sends `AT+CCLK?` and sets the RTC from
`+CCLK: "YY/MM/DD,HH:MM:SS+ZZ"`, where **ZZ is quarter-hours** (`+12` = +3 h).
The offset is applied, so the RTC ends up on local time — what a tester
compares against a wall clock and against the receiver's log lines. It runs
automatically at camera start-up **and** on the modem's `+SDVRRDY` /
`+SDVRNET: UP`, with a bounded retry.

One trap worth knowing: **`modem_send_at()` cannot return the `+CCLK:` line.**
`_looks_like_urc()` in `modem_task.c` classifies every line starting with `+`
that is not a terminator as a URC and routes it to the callback, so `reply`
holds only `OK`. That heuristic is load-bearing for the whole SDVR command set,
so the clock is caught in the URC forwarder (`_cclk_line`) instead of making
the tunnel smarter.

Measured: camera reflashed, and with no modem reboot at all the RTC came up at
`26/08/19 15:14:41` against a bench wall clock of `15:14:47`.

### Two toolchain traps

- `make` in `Application/Release` picks up a **default goal from an included
  `subdir.mk`** and answers "'Utilities/JPEG/jpeg_utils.o' is up to date"
  while building nothing. Always `make all`.
- The system `/usr/bin/arm-none-eabi-gcc` rejects `-fcyclomatic-complexity`.
  Build with ST's GCC on PATH, and sign with the **standalone** CubeProgrammer
  2.21 (`~/STMicroelectronics/...`) — the copy inside CubeIDE is 2.20 and has
  no `-align`.

---

## 2026-08-17 — both flows re-walked, and written down as a QA document

Both halves of the product were run again end to end on `t7aryz0009769z2`,
against the state the customer's server is actually in today, and the run
became `Scopus_QA_Flow.docx` (generated by `make_qa_flow.py`) — the short
document QA repeats, as distinct from the long tester manual.

| Leg | Result |
|---|---|
| `preflight.py` | 12/12 PASS |
| Cable — event | `detect simulate 3` → `rsd=3` then `rsd=0` on `192.168.2.3:9999`, valid §6 JSON |
| Cable — photo | `photo upload` → 118,853 B, `JPEG, complete`, `X-Ref: 6` |
| Cellular — config | `--point-cloud 213.8.185.180 --http-port 8991 --path /upload --notify-path /notify` → `+SDVRNET: 1,1,1,1` |
| MQTT — channel | `+SDVRMQTT: 1,1,"213.8.185.180",5912,"359779080290964",…` |
| MQTT — camera cmd | `version`, `detect simulate 3` — each answered exactly once |
| MQTT — modem cmd | `AT+CSQ` → `+CSQ: 29,99` |
| Whole product from the server | `photo upload` → `POST /upload` 118,768 B and `POST /notify` 101 B at `213.8.185.180:8991`, source `213.8.185.178` (the carrier NAT) |

**The customer's inbound is open now.** The 2026-08-13 entry below records
8991/5912/6734 timing out from the internet; today the photo and both
notifications arrived over cellular at 8991, and the broker on 5912 is
reachable from the mobile network. The UDP sink on 6734 is not listening, but
nothing uses it — notifications ride the same web port as the photos
(`AT+SDVRNTFPROTO=1,"/notify"`).

### One defect found: `AT+SDVRNTFPROTO=2` is rejected

Notifications over the MQTT channel are wired everywhere except the one place
that matters. `server_config.c:261` still validates against UDP and HTTP only:

```c
if (proto != SDVR_NOTIF_PROTO_UDP && proto != SDVR_NOTIF_PROTO_HTTP)
    return LE_BAD_PARAMETER;
```

so `AT+SDVRNTFPROTO=2` answers `+SDVRERR: 13` on 1.12.0, even though
`server_config.h` defines `SDVR_NOTIF_PROTO_MQTT 2`, the AT handler parses up
to it, and `notify.c:333` already routes it to `Mqtt_PublishNotification()`.
`scopus/<id>/ntf` is therefore dead code as shipped. One line to fix, but it
needs a 1.13.0 build and deploy, so it is listed rather than done. The QA
document does not use that path — it uses the HTTP POST that works — and says
so under *Known behaviour*.

---

## 2026-08-16 — remote commands over MQTT, end to end over cellular

ITP asked for a way to send commands *to* a deployed unit. They offered two
options: reply on the UDP notification channel, or MQTT over their port 5912
with a client certificate. **Both were measured before choosing, and the
measurement decided it.**

### Why not the UDP return path (it works, and it is still not enough)

The firmware already had the return path: `notify.c` sends notifications from
one UDP socket and `sdvrNotifRx` reads replies off that same fd, forwarding
them to the camera. It works — a server reply reached the camera console
**0.2 s** after the report.

But the carrier NAT mapping closes in **under 30 seconds**. Probes sent back
to the same address/port at t+30, +60, +120 and +240 s after a report all
vanished; only the immediate reply arrived, confirmed at both ends (three
`Notify: inbound datagram` lines in the modem log, one per immediate reply,
none for the delayed probes). Partner IL, APN `internet`.

So that path can answer a report. It cannot start a conversation. Commands
have to arrive when the operator types them, which needs a held-open
connection — hence MQTT.

### What was built

**Broker** — mosquitto 2.0.18 on the bench, TLS listener on **5912**, using
the PKI that was already in `/opt/sdvr-server/certs` (the server cert's SAN
already carries `IP:213.8.185.180`, the address the unit dials from the mobile
network). `require_certificate true` + `use_identity_as_username true`, so the
device's CN is its username and no password exists anywhere. Verified that a
client presenting no certificate is refused. Config in
`/etc/mosquitto/conf.d/scopus.conf`.

**5912 was in use** by `sdvr-https.service`, ITP's mutual-TLS upload receiver.
It is stopped, because Reuven designated 5912 for this and the photos are on
8991. `systemctl start sdvr-https` puts it back — but not while mosquitto
holds the port.

**Modem app 1.12.0** — `mqtt.c`, a minimal MQTT 3.1.1 client over OpenSSL
1.0.2 (the sysroot has no libmosquitto or paho; curl's MQTT support is
experimental and carries no TLS, so it was not an option). QoS 0 out, QoS 1
accepted and acknowledged in. LWT + retained `online`/`offline` on
`scopus/<id>/status`. Keepalive 60 s, PINGREQ at 45 s idle, reconnect if no
PINGRESP in 30 s — a carrier can reap an idle TCP session with neither end
noticing, and without that check the unit sits silently uncommandable.
Backoff 5→60 s. Client id defaults to the IMEI.

New AT commands: `AT+SDVRMQTTSRV="<host>",<port>`, `AT+SDVRMQTTID="<id>"`,
`AT+SDVRMQTT=<0|1>` / `?`, `AT+SDVRCMDR=` (the camera's response),
`AT+SDVRNTFPROTO=2` (notifications over MQTT). URCs `+SDVRMQTT: UP|DOWN|ERROR`
and `+SDVRCMD: "<text>"`.

**Camera** — `shell_task.c` runs a remote command in the *shell task's own
loop* with a capture stream swapped in, never on the modem task's thread:
lwshell is not reentrant and owns one stream at a time, so the URC only parks
the line and the shell task picks it up between iterations. Output is captured
rather than printed, so a remote `version` is byte-identical to a typed one.
Responses go back as `AT+SDVRCMDR`, chunked with the same backtick/128-byte
encoding as `AT+SDVRNTFA` (the constraint belongs to the AT line, not the
payload — and shell output contains quotes far more often than the §6 JSON).

### The routing rule, and the trap under it

A command starting with `AT` is answered by the modem; anything else goes to
the camera. The first version sent every AT command to `/dev/ttyAT` and
**every `AT+SDVR*` command came back a bare ERROR** — those handlers belong to
`le_atServer`, which serves the host UART and the camera's HDLC link, and the
module's own parser has never heard of them. Fixed by giving mqtt.c its own
PTY whose slave is handed to `le_atServer` — the same trick `hdlc_channel.c`
already uses. atServer registers handlers per command, not per device, so that
device inherits every `+SDVR*` command for free. The handler runs on the main
thread while the worker blocks reading the master; safe in one direction only,
so **no AT handler may ever wait on the MQTT worker**.

### The bug the first end-to-end run found

**Every response was published twice.** The camera's counters showed one
queued send per command, so the duplicate was the lossy-ack retry arriving and
being acted on a second time — exactly what `AT+SDVRNTFA` has dedup for.
`AT+SDVRCMDR` now has the same, keyed on numerator **and** payload hash, with
its own slots (a shared table would let a notification evict the record of a
response about to be retried). Re-tested: every command answered exactly once.

### Proven, over cellular, against 213.8.185.180

| What | Result |
|---|---|
| TLS + client cert | `ECDHE-RSA-AES256-GCM-SHA384`, no-cert client refused |
| Camera command | `version` → the camera's own banner |
| Modem command | `AT+CSQ` → `+CSQ: 28,99 OK` |
| Our AT namespace | `AT+SDVRNET?` → the full `+SDVRNET: 1,1,1,1,"Partner IL",…` |
| Whole product | `photo upload` sent from the server → 121,593-byte JPEG on the server |
| Notification | `POST /notify` 100 B, HTTP 200, §6 JSON intact |
| Photo | `POST /upload`, complete JPEG (FFD8…FFD9) |
| Regression | `run_integration_tests.py` **56 PASS / 0 FAIL / 0 GAP / 1 SKIP** — unchanged |

Route confirmed cellular, not the cable: `ip route get 213.8.185.180` on the
modem goes via `rmnet_data0`, and their receiver logs the source as
`213.8.185.178` (their NAT's outside address), i.e. arriving from the internet.

Tester procedure: **section 19** of `Scopus_Tester_Manual.docx` (Steps R1-R5).

### Also fixed on the way

The bench came back from a power-cycle with **no `sdvrApp` installed at all**
(Legato system 42, app absent; the FTDI showed a login prompt where the AT
channel should be). Reinstalled and marked good. Two follow-ons: the FTDI AT
channel stayed silent until the app was stopped and started once, and the
CN805 camera→modem direction needed a single `mdm relink`. Note that an
app-stopped `cat /dev/ttyHSL1` reading 0 bytes proves nothing while atServer
owns the port — that measurement nearly produced a wrong "the RX pin is dead"
diagnosis.

---

## 2026-08-13 — ITP's two QA bugs, both root-caused and fixed

`ITPNOVEX/ScopusQA` issues #1 and #2, reported by Reuven. Both reproduced on
`t7aryz0009769z2` before anything was changed, and both turned out to be
firmware faults rather than test-procedure mistakes.

### #1 "No response for query commands" — a USBX bug, not a shell bug

The tester's description was precise and worth re-reading: *"Instructions
commands are working but there is no OK response."* That is not a hung shell.
Proved it on the bench: with the console silent, `detect stop` sent over CDC
**stopped the NN task** (the per-frame inference trace ceased at that instant)
while nothing came back. The shell receives, parses and executes; only the
transmit direction is dead. It never recovers, and it takes a reboot to clear
— which is why the kit also looked bricked: `n6cam-update.py` drives the same
console, so a wedged unit cannot even be reflashed.

Root cause is in ST's USBX device controller,
`ux_dcd_stm32_transfer_request.c`:

```c
HAL_PCD_EP_Transmit(...);                         /* buffer handed to the peripheral */
status = _ux_utility_semaphore_get(&...semaphore, timeout);
if (status != UX_SUCCESS)
    return(status);                               /* returns, transfer still armed */
```

On a timeout it returns **without taking the transfer back**. The endpoint
stays busy for ever: every later `HAL_PCD_EP_Transmit` on that address is
refused, so nothing is sent, so the semaphore is never posted, so the next
caller times out too. **One expired write kills the direction permanently.**

That is reached constantly on this product because the device writes to its
own console — a `+SDVRNTF` line every time the scene changes — and a QA
terminal is not always attached to read it. Notifications are written with a
100 ms timeout, so the first detection with nobody listening killed the
console. This is also the other half of the 2026-08-06 §5.2 fix: honouring the
timeout stopped the *hang* and left the *poisoning*.

Fixed by aborting and flushing the endpoint on the timeout path, then draining
any post the ISR slipped in, so the next caller starts clean. Deliberately
**not** applied to the receive direction: RX shows no sign of the fault and
flushing there would discard a byte that arrived during the abort.

Two supporting fixes found along the way:

- **`_notify_emit()` overflowed the NN task's stack.** It put ~1.2 KB of
  buffers (`json`/`buf`/`enc`/`at`) on its caller's stack. That was survivable
  while the shell task was the only caller and fatal once `nn_task` became the
  second: every task here has a 2 KB stack. Measured after the fix, with the
  buffers moved to static storage under a mutex, `tx.task.nn` peaks at
  **1420 bytes** — plus the 1.2 KB it used to carry is ~2.6 KB into a 2 KB
  stack. The NN stack is now 4 KB, matching modem/jpeg.
- **`stacks`** — new shell command printing per-task high-water usage from
  ThreadX's 0xEF fill. A stack overflow here corrupts something else minutes
  later and somewhere else entirely; this makes it readable instead.
- **`version` now also prints `Build: <date> <time>`.** `version_bsp.h` is
  vendor-generated and our build never regenerates it, so every image reported
  `01.08.2593089169` and a flash that silently did not take was
  indistinguishable from one that did. That cost a debugging detour today.

**Verified**: 8 minutes of continuous detection with the CDC port *closed* for
60 s between probes — the exact condition that used to kill it. The console
answered every round (uptime 111 s → 481 s). Before the fix it died
permanently ~95 s after `detect start`, taking the whole USB device off the
bus with it. Residual: 2 individual commands out of 18 returned nothing and
the next one worked — the known notification-vs-response interleaving, not a
wedge.

### #2 "Unstable detection with multiple objects" — the debounce sat silent

The 2026-08-12 debounce required a new count to hold **continuously** for
1000 ms. With four people in frame and one flickering — exactly what a
192×192 quantised detector does to a partly-occluded or distant object — the
count alternates 3,4,3,4 at frame rate, the window restarts every ~90 ms, and
the believed count never moves. **No notification and no photo are sent at
all** while people are plainly in view. Measured on the old build: a static
scene took **144 s** to report once, and reported nothing in between. That is
the tester's report exactly, and it is a worse failure than the storm it
replaced, because silence looks like a dead device.

Replaced with asymmetric hysteresis, because a detector this size misses
objects far more often than it invents them, so 3↔4 almost always means four
seen imperfectly:

- **Rising** is believed after `NN_RISE_CONFIRM_FRAMES` (2) frames above the
  believed count, carrying the **highest** value seen. Evidence expires on the
  clock, not on a disagreeing frame — clearing it when the count momentarily
  agrees is what starved the rise in the first version.
- **Falling** must hold below the believed value for the whole window without
  once touching it, and then falls to the **highest** count seen during that
  window. A person who blinks out for three frames does not empty the room.

So 3→4 reports in ~200 ms, 4→3→4→3 reports nothing, everybody leaving reports
0 one window later, and the scheme always converges. `detect debounce 0` still
disables both halves. Verified on the bench: counts now report within seconds
of the scene changing (5, 4, 0, 1, 6, 5 …) instead of after minutes.

### The customer's server: the device is fine, their inbound is closed

Reuven asked for the data to go straight from the device to `213.8.185.180`
(photos TCP 8991 or 5912, notifications UDP 6734). **`213.8.185.180` is the
office public IP of the bench itself**, and their receiver
(`/opt/sdvr-server/server.py`, `SDVRReceiver/2.0`, plus a UDP sink on 6734) is
already installed and running on the bench PC — it has been up 39 days. So
nothing needs installing; it is a reachability problem:

| From | 8991/tcp | 5912/tcp | 6734/udp |
|---|---|---|---|
| inside their LAN | HTTP 200 | connects | listening |
| the modem, over cellular | timeout | timeout | sent, never arrives |
| an unrelated internet host | timeout | timeout | — |

The device end is proven, not assumed. The modem decoded and sent the §6 JSON
(`AT: NTFA ENC=1 rejoined 1 part(s) -> 102 bytes`, `Notify_Send: 102 bytes
sent over UDP`) and `tcpdump` on the receiving machine itself saw **nothing**.
The control run settles it: the same modem, same cellular link, same
notification path, pointed at a reachable public server —
`Notify HTTP: 102 bytes POSTed to http://165.22.181.245:80/scopus/notify
(200)`. Cellular data is healthy (Partner IL LTE, route up, `ping 8.8.8.8`
0% loss).

**So the inbound port-forward at 213.8.185.180 is not passing traffic from the
internet.** That is theirs to fix. Endpoints are left configured at their
server so it starts working the moment it opens.

### Still blocked: the CN805 camera↔modem link on this bench

The full camera→modem→cellular→server chain could **not** be run today. The
link is latched in the camera→modem direction: `tx frames=51 retries=50`,
`rx frames=3`, `ntf queued=9 sent=0`, `-> nothing reaching PF6`. The camera
composes correct notifications (`rsd` 3/4/5/6, RTC set, right timestamps) and
they never cross the wire.

This is the known FXMA108 auto-direction translator fault (§3, §4.1), not
anything changed today. Everything documented was tried: the ordered reset
(modem first, wait for `sdvrApp`, then camera, camera drives first) ×2,
`mdm relink` ×15+, and back-to-back traffic to beat the ~5 s idle latch. It
came back for exactly one command (`mdm AT` → `OK`, `+SDVRRDY` framed through)
and latched again. **It needs someone at the bench** — re-seat CN805 — and
ultimately the hardware fix.

### Also found: `sdvrApp` disappears across modem reboots

It was absent at the start of the session and vanished again after a reboot
despite `update --mark-good` reporting success — the system index climbed
38 → 40 with the app gone from `app list` on a system marked `[good]`.
AirVantage/`avcService` is running and is the obvious suspect. Reinstall,
`--mark-good`, and **verify `HdlcChannel: init on /dev/ttyHS0` appears in
`logread`** — without the app the camera's frames reach the bare AT parser,
which answers a plain 9-byte `\r\nERROR\r\n` and never frames, which reads
exactly like a dead link.

---

## 2026-08-12 — a live demo, and the three faults it found

The whole chain ran over real cellular to the public relay during a customer
demo: `detect simulate 3` and a 94,943-byte JPEG both reached
`165.22.181.245` **from 2.54.57.163**, a Partner mobile address, and live
detections followed all afternoon. Modem app 1.10.0, preflight 12/12, SIM
`+SDVRSIM:1,1` on the Partner card. What broke is worth more than what worked.

### 1. The data route died and the keeper could not recover it — FIXED (1.11.0)

`+SDVRNET: 1,1,1,**0**` — cellular on, registered, session "connected", **no
route**. On the modem, `rmnet_data0` was **DOWN with no address**, while
`le_mdc` reported CONNECTED with an IP and a gateway. Every notification and
photo failed with `Notify HTTP: no route — requesting the data session`.

The keeper noticed (`Network_IsDataUp()` judges on the route, correctly) and
retried at 10 s, 20 s, 40 s — and could never succeed: `RegisterAndConnect()`
calls `le_mdc_StartSession()`, gets `LE_DUPLICATE`, finds the duplicate **is**
tracked as CONNECTED, and returns `LE_OK` without touching it. Hence
`bring-up failed (err 0)` forever. **`AT+SDVRNET=0/1` and a full
`app restart sdvrApp` both changed nothing** — neither stops a session Legato
believes is up.

Unblocked by hand (this is the field workaround, ~5 s):

```bash
ssh root@192.168.2.2 "/sbin/ip link set rmnet_data0 up; \
  /sbin/ip addr add <ip>/32 dev rmnet_data0; \
  /sbin/ip route add <gw> dev rmnet_data0 scope link; \
  /sbin/ip route add default via <gw> dev rmnet_data0"
```
(`<ip>`/`<gw>` are the last two fields of `AT+SDVRNET?` — today
10.67.51.120 / .121.) `curl` from the modem then answered 200 and the queued
backlog flushed at once.

Fixed properly in `network.c` with `RecycleDataSession()`: when the sequence
reports success but `Network_IsDataUp()` is still false, **stop the session and
start it again**, then install the route on the way up. "Connected" and
"usable" are different claims; only the second is worth anything to a caller.

### 2. Only arrivals were reported, so a scene that never empties goes silent

The camera fired solely on the 0→N edge, so **3 → 4 raised nothing**. The lens
was pointed at a monitor showing a stock photo of three people, which kept the
scene permanently occupied — the customer could stand in front of it all day
and never produce an event. Nothing was broken; the trigger could not arm.

Changed in `nn_task.c`: the detection **count** is what is reported, and
**every change of it** raises an event, both directions, including the change
to 0 (rsn stays 0x10, rsd carries the new count; the SD snapshot is skipped on
the 0, there being nobody to photograph).

### 3. …and the same edge, flickering, produced an event every few seconds

The other half of the same fault. The edge re-armed the instant the count
touched zero, so one frame with the boxes dropped — glare, motion blur, a
confidence on the threshold — read as the room emptying and re-entering. Six
events stamped over 12 s arrived over 26 s: each leg is one `AT+SDVRNTFA` plus
an HTTP POST plus an ack, ~4–6 s, so the events outpaced the drain and the lag
compounded. They were **distinct events, not duplicates** (`num` 47…52) — the
modem's dedup correctly left them alone.

Fixed with a debounce: a new count must hold **continuously for 1000 ms**
before it is believed. `detect debounce <ms>` / `detect debounce query`,
persisted (registry V5, `detect_debounce_ms`). 0 restores the old
report-every-frame behaviour. Time, not frames, because frame rate varies with
load and it is steadiness in seconds the customer's server cares about.

**Not yet flashed / installed** — both builds are clean but the demo was live.
Camera: `Application_signed.bin` over CDC. Modem:
`_build_sdvr_v1110/…/sdvrApp.wp76xx.update`, then `--mark-good` inside
probation.

### 4. The SoW's automatic photo upload was never implemented — now it is

The customer asked why no image appeared while events kept arriving. The
answer was that nothing uploads a photo on its own: `action_msk` had bit0
(save to SD) and bit1 (report), and a picture only ever left the device when
somebody typed `photo upload`.

That is a real gap against the SoW, not a demo mistake. **§3.1 lists five
enable/disable bullets and the last two are the automatic ones** — "taking
photo and saving to SD card **on detection of new objects**" and "taking photo
and **sending to remote server** on detection of new objects" — while the §4.2
mask table only ever defined two bits. Half of §3.1 had no way to be switched
on.

Added as **bit2**: `detect profile 0x01 0x06` = report + upload
(`0x07` with the SD snapshot as well). It calls the same
`snapshot_request_upload()` the shell command uses, so the capture, encode and
SENDBIN transfer happen on snapshot_task and inference is never held behind
the UART. Rate-limited to one photo per 20 s — a ~95 KB JPEG needs 10–15 s on
the internal 115200 link and a room changes faster than that, so without a
floor the queue would never drain and every later picture would describe a
moment long past. Drops are counted, not hidden: `detect stats` prints
`auto-upload skipped=<rate> busy=<pipeline>`.

Proven on hardware, nothing typed:

```
72  14:13:38Z  notification  rsd 4
73  14:13:54Z  UPLOAD  102135 bytes  JPEG, complete
```

and the JPEG shows four people, which is what `rsd` said.

### Also seen, unfixed

- **The CDC shell wedges under a heavy detection load.** `cam.py` first
  answers "busy sending a notification", then stops answering entirely, while
  the camera keeps detecting and notifying perfectly. `uhubctl -l 3-7 -p 1 -a
  cycle` cleared it once and it wedged again within the minute. Photos are only
  reachable from that shell, so an on-demand photo is what is lost.
- **`mdm stats` counts `ntf: unconfirmed`** for notifications that did arrive —
  the ack path is lossy, the delivery is not.

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

### Ask SIM/radio questions with `at.py --raw`, not the SDVR channel

`AT+CPIN?`, `AT!UIMS?`, `AT+COPS?` sent over the FTDI port come back as
**binary rubbish** — the SDVR app bridges unrecognised commands to the modem
and the reply returns at the wrong line settings. It reads like a dead modem
and is not one. `at.py --raw` sends them to `/dev/ttyAT` on the modem over
SSH instead. That path needs the USB Ethernet link even when the thing under
test is cellular.

### The bench modem dropped off USB at the end of 2026-08-09

`lsusb` shows no Sierra device, there are no `usb-Sierra_Wireless_*` by-id
nodes, and `cdc_ether … unregister` is in dmesg — with the FTDI adapter
re-attaching on a *different* USB port (3-6 → 3-5), which is a re-plug
signature rather than anything software did. The N6Cam and the ST-Link are
still there. **The modem needs re-seating by someone at the bench**; until
then the SDVR channel answers garbage and `192.168.2.2` does not ping. It was
healthy for the whole session up to that point, including the 1.8.0 install
and every verification above.

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
