# Whole-Product Architecture — N6Cam + V20_SDVR

**Product role:** Remote, unattended, cellular-backhauled people-counter with
on-event image capture.
**Date:** 2026-04-24
**Status:** Draft for vendor (SIANA) alignment.

---

## 1. Two-Board System

The product is a pairing of two existing boards, with new N6Cam firmware and
small additions to the V20_SDVR app:

| Board | Role | What it already does | What it needs to do |
|---|---|---|---|
| **SIANA N6Cam** | Edge-AI front end | Camera + person detection | People counting, image-on-change capture, event log on SD, GPIO event signal, SD-interlock, UART FW update target |
| **V20_SDVR** (mangOH Yellow / WP7607) | Backhaul + cloud uplink | Cellular, SD mount/unmount, HTTP(S) upload with mTLS, AT-command control, rolling logs | Drive SD hand-off GPIO to N6Cam, ack with release, optionally push N6Cam FW over UART |

One **shared SD card** is the data bus between them. One **UART** is the
control / update bus. A small set of **GPIOs** arbitrates SD ownership and
event signaling.

---

## 2. System Diagram

```
                             Cellular
                                │
                   ┌────────────▼────────────┐
                   │   V20_SDVR (WP7607)     │
                   │   Legato sdvrApp        │
                   │   - AT server           │
                   │   - upload_engine       │
                   │   - sd_manager          │
                   │   - cert_manager (mTLS) │
                   └──┬──────────┬──────┬────┘
                      │          │      │
           SD_REQ ────┘   UART   │      │ SD (SDIO/SPI) — muxed
           (out)          tx/rx  │      │ ownership switches with
           SD_ACK ◄──┐           │      │ SD_REQ / SD_ACK
           (in)      │           │      │
                     │           │      ▼
                     │     ┌─────┴────────────────────┐
                     └─────┤      SIANA N6Cam         │
                           │  - person detector       │
                           │  - event logger          │
                           │  - SD-interlock FSM      │
                           │  - UART update bootloader│
                           │  - GPIO: EVENT_OUT       │
                           │                          │
                           │         ┌──────┐         │
                           │         │Camera│         │
                           │         └──────┘         │
                           └──────────────────────────┘
                                          │
                                   ┌──────┴──────┐
                                   │  SD card    │  (shared, muxed)
                                   └─────────────┘
```

---

## 3. Signal / Interface Inventory

| Signal | Direction | Owner asserts | Purpose |
|---|---|---|---|
| **SD_REQ** | V20_SDVR → N6Cam | V20_SDVR when it wants the SD card (to upload) | Tells N6Cam to stop writing, flush, and release the SD bus |
| **SD_ACK** | N6Cam → V20_SDVR | N6Cam when it has flushed/unmounted and released | "Safe to take the card" |
| **EVENT_OUT** | N6Cam → V20_SDVR | N6Cam on every count-change event logged | Optional wake/trigger to V20_SDVR to initiate an upload cycle |
| **UART** | bidirectional | — | (a) Debug console to N6Cam, (b) FW update transport to N6Cam |
| **SD bus** (SDIO or SPI) | muxed | — | Physically switched between the two hosts by the SD-arbitration logic |

> **Electrical and pin assignments:** TBD with SIANA. The N6Cam-side pins are
> the ones listed as open questions in `SOW_people_counter.md` §5.

---

## 4. SD-Card Hand-off Protocol

State machine from the N6Cam's perspective — this is the concrete definition
of the "SD interlock GPIO" from the N6Cam SOW (FR-5).

```
          ┌──────────────────────┐
          │  OWN_SD              │  N6Cam holds SD, writes events directly.
          │  (default)           │  EVENT_OUT pulses on each event.
          └──────────┬───────────┘
                     │ SD_REQ asserted by V20_SDVR
                     ▼
          ┌──────────────────────┐
          │  FLUSHING            │  Finish in-flight write, fsync, umount.
          └──────────┬───────────┘
                     │ umount done
                     ▼
          ┌──────────────────────┐
          │  RELEASED            │  Drive SD_ACK. New events go to RAM FIFO.
          │                      │  EVENT_OUT still pulses.
          └──────────┬───────────┘
                     │ SD_REQ de-asserted
                     ▼
          ┌──────────────────────┐
          │  REMOUNT             │  Drop SD_ACK, remount SD.
          └──────────┬───────────┘
                     │ mount done
                     ▼
          ┌──────────────────────┐
          │  DRAINING            │  Write buffered events in order,
          │                      │  then fall back to OWN_SD.
          └──────────────────────┘
```

And from the V20_SDVR side, expressed as changes to the existing
`upload_engine` flow in `sdvr-app/components/sdvr/src/upload_engine.c`:

```
On AT+SDVRUPLALL (or scheduled upload):
  1. Assert SD_REQ                        ← NEW
  2. Wait for SD_ACK (timeout, log fail)  ← NEW
  3. SD_Mount()                           (existing)
  4. list/upload/rename .rdy → .upd       (existing)
  5. SD_Unmount()                         (existing)
  6. De-assert SD_REQ                     ← NEW
  7. (N6Cam drops SD_ACK, resumes)        ← N6Cam side
```

**Timeouts and failure cases** (to agree during design review):
- How long does the N6Cam have to flush before V20_SDVR assumes it's gone?
  Proposal: 5 s with log + retry.
- What if SD_ACK never comes? V20_SDVR aborts the upload cycle, logs, retries
  on next tick.
- What if V20_SDVR dies with SD_REQ asserted? N6Cam times out on SD_REQ held
  for > T seconds and reclaims the card (no-ack). Proposal: 2 min.

---

## 5. End-to-End Data Flow

```
Person enters frame
      │
      ▼  (N6Cam inference)
Count N changes    ────────────────────────────────┐
      │                                             │
      ▼                                             │
Capture JPEG + event record                         │
      │                                             │
      ▼                                             │
┌──── SD owned by N6Cam? ────┐                      │
│ yes                        │ no (V20_SDVR has it) │
▼                            ▼                      │
Write to SD as .rdy         Push to RAM FIFO        │
      │                            │                │
      │                            │ (on release)   │
      │                            └──► drain to SD │
      ▼                                             │
Pulse EVENT_OUT ────────────────────────────────────┘
      │
      ▼  (optional trigger)
V20_SDVR wakes upload
      │
      ▼
SD_REQ → wait SD_ACK → mount → upload .rdy → rename .upd → unmount → SD_REQ↓
      │
      ▼  (cellular)
HTTPS POST (mTLS) to cloud server
```

**On-SD layout proposal** (agree with V20_SDVR file conventions — it already
knows `.rdy` / `.upd`):

```
/sdvr/                          ← V20_SDVR's existing root
  events/
    2026-04-24/
      00001234_count_3to5.jpg
      00001234_count_3to5.json
      00001234_count_3to5.rdy   ← sentinel or JSON itself named .rdy
  log.jsonl                     ← append-only index (optional)
```

The N6Cam writes `.rdy`; V20_SDVR's existing `upload_engine` picks it up,
uploads, and renames to `.upd`; `AT+SDVRRMUPL` cleans up.

---

## 6. Firmware Update — Two Levels, One Pipe

The N6Cam SOW's FR-6 ("FW download over UART") becomes, at the product level,
**remote N6Cam update via the cellular link**:

```
Cloud  ──HTTPS──►  V20_SDVR  ──UART──►  N6Cam bootloader
                      │
                      │ new AT command: AT+SDVRN6FWUP=<url>
                      ▼
                  download .bin → stream over UART → verify → boot
```

New AT command on V20_SDVR side (addition to `at_handler.c`):

| Command | Description |
|---|---|
| `AT+SDVRN6FWUP=<url>[,<sha256>]` | Download N6Cam FW from URL, push to N6Cam over UART, verify, report via URC `+SDVRN6FWUP:OK|ERR,<code>` |
| `AT+SDVRN6VER` | Query currently running N6Cam FW version (queried over UART) |

The WP7607 side already has:
- libcurl + mTLS → download is free.
- Rolling log + URC plumbing → progress/reporting is free.

New code needed on WP7607 side:
- UART channel driver for the N6Cam port.
- Framing of the N6Cam update protocol (XMODEM/YMODEM/custom — TBD with SIANA).

New code needed on N6Cam side:
- Resident bootloader/update agent (FR-6 of the N6Cam SOW).
- A/B or golden-image fallback.

**V20_SDVR self-update** is unchanged — it already uses Sierra's `fwupdate`
over-the-air path and the existing `modem-flash` tool on the bench.

---

## 7. Power-on / Boot Orchestration

| Step | Actor | Action |
|---|---|---|
| 1 | V20_SDVR | Boots Legato, `sdvrApp` starts, de-asserts `SD_REQ` |
| 2 | N6Cam | Boots, sees `SD_REQ` low, takes SD, mounts, enters OWN_SD |
| 3 | N6Cam | Emits a boot URC over UART (`+N6RDY:<ver>`); V20_SDVR logs it |
| 4 | V20_SDVR | On schedule or `EVENT_OUT` or `AT+SDVRUPLALL`, runs hand-off |

If either side reboots mid-operation, the SD state machine above resolves it:
N6Cam will remount when `SD_REQ` drops, V20_SDVR will retry `SD_REQ` with a
timeout.

---

## 8. Split of Work — Deliverables by Vendor

### SIANA (N6Cam firmware) — per `SOW_people_counter.md`
- FR-1 … FR-6 in that SOW.
- Conform to the SD hand-off state machine in §4 of this document.
- Define UART update protocol; deliver host-side flasher reference (V20_SDVR
  will re-implement the host side in C/Legato, see below).

### Kamacode / ITP (V20_SDVR additions)
- New AT commands `AT+SDVRN6FWUP`, `AT+SDVRN6VER`.
- UART driver + N6Cam update protocol client (using SIANA's spec).
- Modifications to `upload_engine` to drive `SD_REQ` and wait on `SD_ACK`
  before mount and after unmount.
- Optional: `EVENT_OUT` input handler to kick scheduled uploads.
- Integration tests covering the SD hand-off and N6Cam update paths.

### Shared / to decide
- Physical SD mux / level shifter board (if needed).
- Signed-image policy for N6Cam updates.
- Cloud-side contract (upload path, event-JSON schema) — already largely
  defined by V20_SDVR; just need the event-record schema.

---

## 9. Open Questions (Rolled Up)

Superset of the N6Cam SOW §5:

1. **SD bus arbitration** — is the SD card physically muxed between the two
   hosts, or does one host expose it as USB-MSC to the other? If muxed,
   whose design is the mux?
2. **GPIO availability** on the N6Cam connector for `SD_REQ`, `SD_ACK`,
   `EVENT_OUT` — and matching free GPIOs on the mangOH Yellow side.
3. **UART routing** — is the N6Cam UART that's reachable by V20_SDVR the same
   one used for update mode, or separate? Update-mode entry mechanism.
4. **Timing budgets** — SD hand-off latency ceiling (affects event capture
   gap); maximum acceptable N6Cam RAM buffer depth during upload.
5. **Event-record schema** — agree once, used by both N6Cam (write) and cloud
   (consume).
6. **N6Cam FW update protocol** — XMODEM/YMODEM/custom; signed or CRC-only.
7. **Bench bring-up plan** — given only 2 N6Cam units on hand (one not yet
   recognized by host — see `CLAUDE.md`), sequencing of integration tests.

---

## 10. Relationship to the Two Existing Docs

- `SOW_people_counter.md` — vendor-facing SOW for **N6Cam firmware only**.
  Unchanged; this document wraps it.
- `V20_SDVR/ARCHITECTURE.md` — existing architecture of the modem app.
  The additions listed in §8 are the only changes required there.
- This document — the **system view** for stakeholders (and for Sylvain, so
  he understands where his firmware sits in the product).
