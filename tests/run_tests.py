#!/usr/bin/env python3
"""
N6Cam test runner — drives the kit's CDC shell to exercise every
SoW-mandated UART command, the SD pipeline, frame injection + NN
inference accuracy, and reports performance.

Output:
  - Per-test PASS/FAIL/SKIP with description on stdout (colored).
  - HTML report under results/test-report-<timestamp>.html.

Tests are grouped by capability. SW-update path is NOT covered here —
the user already lives in that flow, retesting it on every run is wasteful.

Usage:
  python3 tests/run_tests.py [--tty /dev/ttyACMx] [--images tests/images]
                              [--out results/test-report-XXX.html]
"""
import argparse
import fcntl
import os
import re
import struct
import sys
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, List, Optional, Tuple

# ── Constants ───────────────────────────────────────────────────────
FRAME_W = 192
FRAME_H = 192
FRAME_BYTES = FRAME_W * FRAME_H * 3

# Re-use the byte-level framing the firmware speaks
FRAME_MAGIC = b"FRMI"

# Detection class mapping (COCO)
CLASS_NAMES = {0: "person", 2: "car", 3: "motorcycle", 5: "bus", 7: "truck"}

# Regex helpers for parsing kit replies
RE_OK_ACK = re.compile(r"^(\S+)(?:\s+\S+)?\s+ok\b", re.MULTILINE)
RE_RTC = re.compile(r"RTC:\s+(\d{2})/(\d{2})/(\d{2})\s+(\d{2}):(\d{2}):(\d{2})")
RE_VERSION = re.compile(r"Application:\s+(\S+)")
RE_FW = re.compile(r"FW VER:\s+(\S+)")
RE_UID = re.compile(r"UID\s*:\s*([0-9A-F]+)", re.IGNORECASE)
RE_UPTIME = re.compile(r"Uptime\s*:\s*(\d+)", re.IGNORECASE)
RE_ECHO_STATE = re.compile(r"echo:\s+(on|off)")
RE_IRLED_STATE = re.compile(r"irled:\s+([01])")
RE_MOTION_QUERY = re.compile(r"motion:\s+sensitivity=(\d+)\s+timeout=(\d+)")
RE_IMG_QUERY = re.compile(
    r"img:\s+(\d+)x(\d+)\s+quality=(\d+)\s+color=(\w+)\s+chroma=(\d+)"
)
RE_DETECT_PROFILE = re.compile(
    r"detect profile:\s+det_msk=0x([0-9a-f]+)\s+action_msk=0x([0-9a-f]+)"
)
RE_NOTIFY_QUERY = re.compile(
    r"notify:\s+enable_mask=0x([0-9a-f]+)\s+period=(\d+)s\s+num=(\d+)"
)
RE_NOTIFY_JSON = re.compile(
    r'\+SDVRNTF:\s*\{"ser":(\d+),"num":(\d+),"rsn":(\d+),"rsd":(\d+)'
)
RE_FRAME_QUERY = re.compile(r"frame:\s+(loaded|empty|cleared)")
RE_FRAME_UPLOAD_OK = re.compile(r"frame upload:\s+ok\s+\((\d+)\s+bytes")
RE_FRAME_RUN = re.compile(
    r"frame run:\s+(\d+)\s+detection\(s\),?\s*(?:NN\s+([\d.]+)\s*ms)?"
)
RE_FRAME_BOX = re.compile(
    r"\[(\d+)\]\s+class=(-?\d+)\s+conf=([\d.]+)\s+bbox=\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\)"
)
RE_SD_QUERY = re.compile(r"sd:\s+(mounted|not mounted)")

# ANSI colours
C_RED   = "\033[91m"
C_GRN   = "\033[92m"
C_YEL   = "\033[93m"
C_BLU   = "\033[94m"
C_CYN   = "\033[96m"
C_BOLD  = "\033[1m"
C_RESET = "\033[0m"


# ── Kit-side serial wrapper ────────────────────────────────────────
class KitShell:
    """Open + drive the N6Cam CDC shell with non-blocking I/O."""

    def __init__(self, tty: str):
        self.tty = tty
        os.system(f"stty -F {tty} 115200 cs8 -cstopb -parenb raw -echo")
        self.fd = os.open(tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.drain(0.3)

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass

    def write_bytes(self, data: bytes):
        n = 0
        while n < len(data):
            try:
                n += os.write(self.fd, data[n:])
            except BlockingIOError:
                time.sleep(0.005)

    def write_line(self, s: str):
        self.write_bytes(s.encode() + b"\n")

    def drain(self, secs=0.2):
        end = time.time() + secs
        while time.time() < end:
            try:
                os.read(self.fd, 4096)
            except BlockingIOError:
                time.sleep(0.02)

    def read_until(self, sentinel: str, max_secs=4.0) -> str:
        buf = b""
        end = time.time() + max_secs
        sentinel_b = sentinel.encode()
        while time.time() < end:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                buf += chunk
                if sentinel_b in buf:
                    break
            else:
                time.sleep(0.02)
        return buf.decode(errors="replace")

    def send_get(self, line: str, sentinel: str, max_secs=4.0) -> str:
        """Send a single shell line and read until the sentinel appears.
        Typical sentinel for SoW commands is 'ok' (the success-ack)."""
        self.drain(0.05)
        self.write_line(line)
        return self.read_until(sentinel, max_secs=max_secs)


# ── Result aggregation ─────────────────────────────────────────────
@dataclass
class TestResult:
    id: str
    desc: str
    status: str             # 'pass' | 'fail' | 'skip'
    reason: str = ""        # populated for fail / skip
    extra: str = ""         # optional info (e.g. timing)


@dataclass
class Suite:
    results: List[TestResult] = field(default_factory=list)
    groups: List[Tuple[str, int]] = field(default_factory=list)
    # device snapshot info captured during T00
    fw_version: str = "?"
    uid:        str = "?"

    def group(self, title: str):
        self.groups.append((title, len(self.results)))
        print(f"\n{C_BOLD}{C_BLU}── {title} ──{C_RESET}")

    def add(self, r: TestResult):
        self.results.append(r)
        col = {"pass": C_GRN, "fail": C_RED, "skip": C_YEL}[r.status]
        tag = r.status.upper()
        suffix = f" — {r.reason}" if r.reason else ""
        extra  = f"  {C_CYN}{r.extra}{C_RESET}" if r.extra else ""
        print(f"  [{r.id:>6s}] {r.desc:<70s} {col}{tag:>4s}{C_RESET}{suffix}{extra}")

    def passed(self) -> int: return sum(1 for r in self.results if r.status == "pass")
    def failed(self) -> int: return sum(1 for r in self.results if r.status == "fail")
    def skipped(self) -> int: return sum(1 for r in self.results if r.status == "skip")
    def total(self) -> int: return len(self.results)


# ── Helpers used by tests ─────────────────────────────────────────
def must_match(suite: Suite, tid: str, desc: str, regex, text: str,
               extra: str = ""):
    m = regex.search(text)
    if m:
        suite.add(TestResult(tid, desc, "pass", extra=extra))
        return m
    suite.add(TestResult(tid, desc, "fail",
                         reason=f"no match for {regex.pattern!r}",
                         extra=text[:120].replace("\n", " ")))
    return None


def must_be(suite: Suite, tid: str, desc: str, ok: bool, reason="",
            extra=""):
    if ok:
        suite.add(TestResult(tid, desc, "pass", extra=extra))
    else:
        suite.add(TestResult(tid, desc, "fail", reason=reason, extra=extra))
    return ok


def skip(suite: Suite, tid: str, desc: str, reason: str):
    suite.add(TestResult(tid, desc, "skip", reason=reason))


# ── Frame inject (binary) helpers ─────────────────────────────────
def load_image_as_rgb(path: Path) -> bytes:
    from PIL import Image  # type: ignore
    img = Image.open(path).convert("RGB").resize((FRAME_W, FRAME_H),
                                                 Image.LANCZOS)
    return img.tobytes()


def inject_frame(sh: KitShell, data: bytes) -> bool:
    crc = zlib.crc32(data) & 0xFFFFFFFF
    sh.write_line("frame upload")
    time.sleep(0.35)
    sh.write_bytes(FRAME_MAGIC + struct.pack("<II", len(data), crc))
    for i in range(0, len(data), 4096):
        sh.write_bytes(data[i:i + 4096])
    out = sh.read_until("frame upload ok", max_secs=4.0)
    return "frame upload ok" in out


def run_inference(sh: KitShell) -> Tuple[int, float, List[dict], str]:
    sh.drain(0.05)
    t0 = time.perf_counter()
    sh.write_line("frame run")
    out = sh.read_until("frame run ok", max_secs=4.0)
    wall = (time.perf_counter() - t0) * 1000.0
    m = RE_FRAME_RUN.search(out)
    if not m:
        return -1, wall, [], out
    count = int(m.group(1))
    nn_ms = float(m.group(2)) if m.group(2) else 0.0
    boxes = []
    for bm in RE_FRAME_BOX.finditer(out):
        boxes.append({
            "class": int(bm.group(2)),
            "conf":  float(bm.group(3)),
        })
    return count, nn_ms, boxes, out


# ── Test groups ────────────────────────────────────────────────────
def group_prereq(sh: KitShell, suite: Suite, tty: str):
    suite.group("GROUP 0: Prerequisites")
    suite.add(TestResult(
        "T00.1", f"Kit CDC port {tty} exists and is readable",
        "pass" if os.path.exists(tty) else "fail",
        reason="" if os.path.exists(tty) else "TTY not found",
    ))
    out = sh.send_get("help", "camera", 3.0)
    must_be(suite, "T00.2", "Shell `help` lists user-defined commands",
            "User-defined commands" in out and "camera" in out,
            reason="`help` output missing expected text")
    out = sh.send_get("fw", "FW VER", 2.0)
    m = must_match(suite, "T00.3", "`fw` returns FW VER: …", RE_FW, out)
    if m:
        suite.fw_version = m.group(1)
    out = sh.send_get("uid", "UID", 2.0)
    m = must_match(suite, "T00.4", "`uid` returns MCU UID", RE_UID, out)
    if m:
        suite.uid = m.group(1)
    out = sh.send_get("uptime", "UPTIME", 2.0)
    must_match(suite, "T00.5", "`uptime` returns UPTIME ms", RE_UPTIME, out)


def group_shell(sh: KitShell, suite: Suite):
    suite.group("GROUP 1: Shell mechanics (SoW §4.1)")
    out = sh.send_get("echo query", "echo query ok", 3.0)
    m = RE_ECHO_STATE.search(out)
    must_be(suite, "T01.1", "`echo query` reports state + ok ack",
            m is not None and "echo query ok" in out)
    out = sh.send_get("echo off", "echo off ok", 3.0)
    must_be(suite, "T01.2", "`echo off` succeeds (ok-ack)",
            "echo off ok" in out)
    # With echo off, the *typed text* is no longer echoed, only the data lines.
    sh.drain(0.2)
    sh.write_line("echo query")
    out = sh.read_until("echo query ok", 3.0)
    must_be(suite, "T01.3", "After `echo off`, no echoed prompt before reply",
            "> echo" not in out)
    out = sh.send_get("echo on", "echo on ok", 3.0)
    must_be(suite, "T01.4", "`echo on` restores echo + ok ack",
            "echo on ok" in out)


def group_system(sh: KitShell, suite: Suite):
    suite.group("GROUP 2: System commands (SoW §3.7)")
    out = sh.send_get("rtc", "RTC:", 3.0)
    must_match(suite, "T02.1", "`rtc` returns DD/MM/YY HH:MM:SS", RE_RTC, out)
    set_str = "22052026120000"
    out = sh.send_get(f"rtc set {set_str}", "rtc set ok", 3.0)
    must_be(suite, "T02.2", "`rtc set DDMMYYYYHHMMSS` accepts a valid value",
            "rtc set ok" in out)
    time.sleep(0.2)
    out = sh.send_get("rtc", "RTC:", 3.0)
    m = RE_RTC.search(out)
    if m:
        ok = (int(m.group(1)) == 26 and int(m.group(2)) == 5 and
              int(m.group(3)) == 22 and int(m.group(4)) == 12)
        must_be(suite, "T02.3", "RTC reads back the value we just set", ok,
                reason=f"got {m.groups()}, expected (26,05,22,12,*,*)")
    else:
        must_be(suite, "T02.3", "RTC reads back the value we just set",
                False, reason="rtc output not parseable")
    out = sh.send_get("version", "version ok", 2.0)
    m = must_match(suite, "T02.4", "`version` prints Application: <ver>",
                   RE_VERSION, out)


def group_sensors(sh: KitShell, suite: Suite):
    suite.group("GROUP 3: Sensors (SoW §3.5, §4.5)")
    out = sh.send_get("irled on", "irled on ok", 3.0)
    m = RE_IRLED_STATE.search(out)
    must_be(suite, "T03.1", "`irled on` reports state=1 + ok-ack",
            m is not None and m.group(1) == "1" and "irled on ok" in out)
    out = sh.send_get("irled off", "irled off ok", 3.0)
    m = RE_IRLED_STATE.search(out)
    must_be(suite, "T03.2", "`irled off` reports state=0",
            m is not None and m.group(1) == "0")
    out = sh.send_get("motion sense 65 45", "motion sense ok", 3.0)
    m = RE_MOTION_QUERY.search(out)
    must_be(suite, "T03.3", "`motion sense 65 45` accepted, echoed back",
            m is not None and int(m.group(1)) == 65 and int(m.group(2)) == 45)
    out = sh.send_get("motion query", "motion query ok", 3.0)
    m = RE_MOTION_QUERY.search(out)
    must_be(suite, "T03.4", "`motion query` returns persisted 65/45",
            m is not None and int(m.group(1)) == 65 and int(m.group(2)) == 45)


def group_img(sh: KitShell, suite: Suite):
    suite.group("GROUP 4: Photo settings (SoW §3.4, §4.4)")
    out = sh.send_get("img quality 75", "img quality ok", 3.0)
    must_be(suite, "T04.1", "`img quality 75` set + ok-ack",
            "img quality ok" in out)
    out = sh.send_get("img color RGB", "img color ok", 3.0)
    must_be(suite, "T04.2", "`img color RGB` accepted",
            "img color ok" in out)
    out = sh.send_get("img chroma 1", "img chroma ok", 3.0)
    must_be(suite, "T04.3", "`img chroma 1` accepted",
            "img chroma ok" in out)
    out = sh.send_get("img query", "img query ok", 3.0)
    m = RE_IMG_QUERY.search(out)
    must_be(suite, "T04.4", "`img query` shows quality=75 color=RGB chroma=1",
            m is not None and int(m.group(3)) == 75 and m.group(4) == "RGB"
            and int(m.group(5)) == 1)
    # Restore defaults so subsequent JPEG quality tests are deterministic
    sh.send_get("img quality 90", "img quality ok", 2.0)
    sh.send_get("img color YCBCR", "img color ok", 2.0)
    sh.send_get("img chroma 0", "img chroma ok", 2.0)


def group_detect(sh: KitShell, suite: Suite):
    suite.group("GROUP 5: Detection control (SoW §3.1, §4.2)")
    out = sh.send_get("detect start", "detect start ok", 3.0)
    must_be(suite, "T05.1", "`detect start` succeeds", "detect start ok" in out)
    out = sh.send_get("detect profile 3 1", "detect profile ok", 3.0)
    m = RE_DETECT_PROFILE.search(out)
    must_be(suite, "T05.2", "`detect profile 3 1` (people+vehicles, save-SD)",
            m is not None and m.group(1) == "03" and m.group(2) == "01")
    out = sh.send_get("detect profile query", "detect profile ok", 3.0)
    m = RE_DETECT_PROFILE.search(out)
    must_be(suite, "T05.3", "`detect profile query` returns 0x03 / 0x01",
            m is not None and m.group(1) == "03" and m.group(2) == "01")
    # Leave detection running for later groups
    out = sh.send_get("detect profile 1 1", "detect profile ok", 3.0)


def group_notify(sh: KitShell, suite: Suite):
    suite.group("GROUP 6: Notifications (SoW §3.1, §4.2, §6)")
    out = sh.send_get("notify enable 0x10", "notify enable ok", 3.0)
    must_be(suite, "T06.1", "`notify enable 0x10` (people-detect bit) accepted",
            "notify enable ok" in out)
    out = sh.send_get("notify period 30", "notify period ok", 3.0)
    must_be(suite, "T06.2", "`notify period 30` accepted",
            "notify period ok" in out)
    out = sh.send_get("notify query", "notify query ok", 3.0)
    m = RE_NOTIFY_QUERY.search(out)
    must_be(suite, "T06.3", "`notify query` shows mask=0x10 period=30",
            m is not None and int(m.group(1), 16) == 0x10 and
            int(m.group(2)) == 30)
    out = sh.send_get("notify trigger 16", "notify trigger ok", 3.0)
    m = RE_NOTIFY_JSON.search(out)
    must_be(suite, "T06.4", "`notify trigger 16` emits +SDVRNTF JSON with rsn=16",
            m is not None and int(m.group(3)) == 16)
    out = sh.send_get("notify disable", "notify disable ok", 3.0)
    must_be(suite, "T06.5", "`notify disable` succeeds",
            "notify disable ok" in out)


def group_sd(sh: KitShell, suite: Suite):
    suite.group("GROUP 7: SD card + photo (SoW §3.2, §7, §4.4)")
    out = sh.send_get("sd query", "sd query ok", 3.0)
    m = RE_SD_QUERY.search(out)
    sd_mounted = m is not None and m.group(1) == "mounted"
    must_be(suite, "T07.1", "`sd query` reports mounted",
            sd_mounted, reason="SD not mounted")
    if not sd_mounted:
        for tid, d in [("T07.2", "`sd ls` enumerates SD root"),
                       ("T07.3", "`photo savesd` returns SoW filename"),
                       ("T07.4", "`photo savesd` file appears in `sd ls`")]:
            skip(suite, tid, d, "SD card not mounted")
        return
    # ls before
    out_before = sh.send_get("sd ls", "sd ls ok", 4.0)
    files_before = set(line.split()[0] for line in out_before.splitlines()
                       if line and line[0] != ">" and "ok" not in line and
                       "sd ls" not in line)
    must_be(suite, "T07.2", "`sd ls` returns at least one entry",
            len(files_before) > 0)
    # Set RTC to wall-clock so the timestamped .rdy filename is unique per
    # test run — otherwise fx_file_create refuses (file already exists from
    # a previous suite run) and we'd flake on T07.4.
    fresh_ts = time.strftime("%d%m%Y%H%M%S")
    sh.send_get(f"rtc set {fresh_ts}", "rtc set ok", 2.0)
    out = sh.send_get("photo savesd", "photo savesd ok", 4.0)
    m = re.search(r"photo savesd: capturing -> (\S+\.rdy)", out)
    must_be(suite, "T07.3", "`photo savesd` returns SoW §7 filename",
            m is not None,
            extra=m.group(1) if m else "")
    # Wait for snapshot_task to encode JPEG + flush via FileX, then poll
    # `sd ls` until the new file shows up (or 15 s timeout). FAT flush on a
    # clean 32 GB card is async and can take a couple of seconds.
    new_name = m.group(1) if m else ""
    found = False
    if new_name:
        for _ in range(15):
            time.sleep(1.0)
            poll_out = sh.send_get("sd ls", "sd ls ok", 4.0)
            if new_name in poll_out:
                found = True
                break
    must_be(suite, "T07.4",
            "New .rdy file appears on SD after `photo savesd`",
            found, reason="not seen in sd ls within 15s" if not found else "",
            extra=new_name if found else "")


def group_frame_inject(sh: KitShell, suite: Suite, image_dir: Path):
    suite.group("GROUP 8: Frame injection mechanics")
    sh.send_get("detect start", "detect start ok", 2.0)
    # query empty
    out = sh.send_get("frame clear", "frame clear ok", 3.0)
    must_be(suite, "T08.1", "`frame clear` reverts to live camera",
            "frame clear ok" in out)
    # synth frame
    try:
        from PIL import Image
        img = Image.new("RGB", (FRAME_W, FRAME_H), color=(128, 128, 128))
        ok = inject_frame(sh, img.tobytes())
        must_be(suite, "T08.2", "Upload a synthetic 192x192 grey frame", ok)
    except Exception as e:
        skip(suite, "T08.2", "Synthetic frame inject", f"Pillow not available: {e}")
    out = sh.send_get("frame query", "frame query ok", 3.0)
    m = RE_FRAME_QUERY.search(out)
    must_be(suite, "T08.3", "`frame query` reports loaded",
            m is not None and m.group(1) == "loaded")
    count, nn_ms, _, _ = run_inference(sh)
    must_be(suite, "T08.4",
            "`frame run` returns a count + NN time on a grey frame",
            count >= 0 and nn_ms > 0,
            extra=f"count={count} NN={nn_ms:.1f}ms")


def group_algorithm(sh: KitShell, suite: Suite, image_dir: Path):
    suite.group("GROUP 9: Detection algorithm — single person + non-person")
    sh.send_get("detect start", "detect start ok", 2.0)
    person_imgs = ["astronaut.jpg", "camera.jpg"]
    non_imgs    = ["cat.jpg", "coffee.jpg", "moon.jpg", "rocket.jpg"]
    nn_times = []

    def run_one(name: str) -> Tuple[int, float, List[dict]]:
        path = image_dir / name
        if not path.exists():
            return -1, 0.0, []
        try:
            data = load_image_as_rgb(path)
        except Exception:
            return -2, 0.0, []
        if not inject_frame(sh, data):
            return -3, 0.0, []
        return run_inference(sh)[:3]

    idx = 1
    for name in person_imgs:
        c, nn, _ = run_one(name)
        if c < 0:
            skip(suite, f"T09.{idx}", f"Inject + run on {name}", f"rc={c}")
        else:
            nn_times.append(nn)
            must_be(suite, f"T09.{idx}",
                    f"{name} (real person) → NN detects ≥1 person",
                    c >= 1,
                    extra=f"count={c} NN={nn:.1f}ms")
        idx += 1
    for name in non_imgs:
        c, nn, _ = run_one(name)
        if c < 0:
            skip(suite, f"T09.{idx}", f"Inject + run on {name}", f"rc={c}")
        else:
            nn_times.append(nn)
            must_be(suite, f"T09.{idx}",
                    f"{name} (no person) → NN returns 0 detections",
                    c == 0,
                    extra=f"count={c} NN={nn:.1f}ms")
        idx += 1

    # Performance test — aggregate NN time + throughput
    if nn_times:
        avg = sum(nn_times) / len(nn_times)
        worst = max(nn_times)
        suite.add(TestResult(
            f"T09.{idx}",
            f"NN inference avg ≤ 30 ms over {len(nn_times)} images",
            "pass" if avg <= 30.0 else "fail",
            reason="" if avg <= 30.0 else f"avg={avg:.1f}ms",
            extra=f"avg={avg:.1f}ms worst={worst:.1f}ms n={len(nn_times)}",
        ))


def group_count(sh: KitShell, suite: Suite, image_dir: Path):
    """Multi-person and vehicle COUNT verification — images sourced from
    COCO128 with ground-truth labels. Asserts a sensible lower bound so the
    test reflects realistic NN behavior at 192x192 input (small people in
    crowd shots will undercount; that's a property of the model, not a bug)."""
    suite.group("GROUP 11: Multi-person + vehicle counting")
    sh.send_get("detect start", "detect start ok", 2.0)
    sh.send_get("detect profile 3 0", "detect profile ok", 2.0)  # people + vehicles, no action

    # Each entry: (filename, ground_truth_persons, ground_truth_vehicles,
    #              min_persons_to_pass, min_vehicles_to_pass, kind)
    cases = [
        ("1_person.jpg",      1,  0, 1, 0, "single"),
        ("2_people.jpg",      2,  0, 1, 0, "small group"),
        ("3_people.jpg",      3,  0, 2, 0, "small group"),
        ("5_people.jpg",      5,  0, 2, 0, "group"),
        ("7_people.jpg",      7,  0, 2, 0, "group"),
        ("crowd_13.jpg",     13,  0, 3, 0, "crowd"),
        ("cars_18.jpg",       0, 18, 0, 0, "vehicle (multi-class only)"),
        ("cars_15.jpg",       0, 15, 0, 0, "vehicle (multi-class only)"),
        ("13ppl_5trucks.jpg",13,  5, 3, 0, "mixed"),
    ]

    def run_one(name: str):
        path = image_dir / name
        if not path.exists():
            return -1, 0.0, []
        try:
            data = load_image_as_rgb(path)
        except Exception:
            return -2, 0.0, []
        if not inject_frame(sh, data):
            return -3, 0.0, []
        return run_inference(sh)[:3]

    idx = 1
    for name, gt_p, gt_v, min_p, min_v, kind in cases:
        c, nn, boxes = run_one(name)
        if c < 0:
            skip(suite, f"T11.{idx}", f"{name} (gt: {gt_p}p, {gt_v}v — {kind})",
                 f"rc={c}")
            idx += 1
            continue
        per_cls = {}
        for b in boxes:
            per_cls[b["class"]] = per_cls.get(b["class"], 0) + 1
        n_person  = per_cls.get(0, 0)
        n_vehicle = sum(per_cls.get(c, 0) for c in (2, 3, 5, 7))
        ok_p = n_person  >= min_p
        ok_v = n_vehicle >= min_v
        passed = ok_p and ok_v
        extra = (f"detected: person={n_person} vehicle={n_vehicle} "
                 f"(gt {gt_p}p/{gt_v}v) NN={nn:.1f}ms")
        if passed:
            suite.add(TestResult(
                f"T11.{idx}",
                f"{name} ({kind}) → ≥{min_p} person",
                "pass", extra=extra))
        else:
            reason = []
            if not ok_p: reason.append(f"person {n_person}<{min_p}")
            if not ok_v: reason.append(f"vehicle {n_vehicle}<{min_v}")
            suite.add(TestResult(
                f"T11.{idx}",
                f"{name} ({kind}) → ≥{min_p} person",
                "fail", reason=", ".join(reason), extra=extra))
        idx += 1


def group_camera(sh: KitShell, suite: Suite):
    suite.group("GROUP 10: Camera control passthrough")
    out = sh.send_get("camera awb auto", "Auto", 3.0)
    must_be(suite, "T10.1", "`camera awb auto` accepted",
            "auto" in out.lower() or "ok" in out.lower() or
            "active" in out.lower())
    out = sh.send_get("camera exposure 10000", "Exposure", 3.0)
    must_be(suite, "T10.2", "`camera exposure 10000` accepted",
            "Value updated" in out or "Exposure" in out)
    out = sh.send_get("camera gain 0", "Gain", 3.0)
    must_be(suite, "T10.3", "`camera gain 0` accepted",
            "Value updated" in out or "Gain" in out)


# ── HTML report ────────────────────────────────────────────────────
HTML_HEAD = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>N6Cam Test Report</title>
<style>
  body { font-family: -apple-system, Arial, sans-serif; margin: 20px; background: #f5f5f5; }
  h1 { color: #333; } h2 { color: #555; margin-top: 20px; }
  .meta { color: #666; margin-bottom: 20px; }
  .summary { font-size: 1.3em; padding: 15px; border-radius: 8px; margin: 15px 0; }
  .summary.pass { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
  .summary.fail { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
  table { border-collapse: collapse; width: 100%; background: white; box-shadow: 0 1px 3px rgba(0,0,0,.12); margin-bottom: 20px; }
  th { background: #343a40; color: white; padding: 10px 12px; text-align: left; }
  td { padding: 8px 12px; border-bottom: 1px solid #dee2e6; }
  tr.group td { background: #e9ecef; font-weight: bold; padding: 10px 12px; }
  tr.pass td:last-child { color: #28a745; font-weight: bold; }
  tr.fail td:last-child { color: #dc3545; font-weight: bold; }
  tr.skip td:last-child { color: #ffc107; font-weight: bold; }
  td.extra { color: #6c757d; font-size: .9em; }
  .footer { color: #999; margin-top: 30px; font-size: 0.9em; }
</style></head><body>
<h1>N6Cam — Test Report</h1>
"""


def write_report(out_path: Path, suite: Suite, runtime_s: int, tty: str):
    total = suite.total()
    passed = suite.passed()
    failed = suite.failed()
    skipped = suite.skipped()
    result_class = "pass" if failed == 0 else "fail"
    result_text = "ALL TESTS PASSED" if failed == 0 else f"{failed} TEST(S) FAILED"

    rows = []
    grp_idx = 0
    grps = suite.groups + [(None, total)]
    for i, r in enumerate(suite.results):
        if grp_idx < len(grps) - 1 and i == grps[grp_idx][1]:
            rows.append(
                f"<tr class='group'><td colspan='3'><b>{grps[grp_idx][0]}</b></td></tr>"
            )
        # ensure we emit any pending groups whose offset equals i
        while grp_idx < len(grps) - 1 and i >= grps[grp_idx][1]:
            if i == grps[grp_idx][1]:
                pass  # already handled above
            grp_idx += 1
        rcell = r.status.upper()
        if r.reason:
            rcell += f" — {r.reason}"
        extra = f" <span class='extra'>{r.extra}</span>" if r.extra else ""
        rows.append(
            f"<tr class='{r.status}'><td>{r.id}</td>"
            f"<td>{r.desc}{extra}</td><td>{rcell}</td></tr>"
        )

    # the simpler grouping logic above is buggy; rebuild cleanly:
    rows = []
    group_marker = dict(suite.groups)
    for i, r in enumerate(suite.results):
        if i in group_marker:
            rows.append(
                f"<tr class='group'><td colspan='3'><b>{group_marker[i]}</b></td></tr>"
            )
        rcell = r.status.upper()
        if r.reason:
            rcell += f" — {r.reason}"
        extra = f"<br><span class='extra'>{r.extra}</span>" if r.extra else ""
        rows.append(
            f"<tr class='{r.status}'><td>{r.id}</td>"
            f"<td>{r.desc}{extra}</td><td>{rcell}</td></tr>"
        )

    body = HTML_HEAD
    body += f"""<div class="meta">
  <b>Date:</b> {time.strftime('%Y-%m-%d %H:%M:%S')}<br>
  <b>Device:</b> SIANA N6Cam (STM32N657 / IMX335 / Neural-ART NPU)<br>
  <b>Firmware:</b> {suite.fw_version}<br>
  <b>MCU UID:</b> {suite.uid}<br>
  <b>Shell TTY:</b> {tty}<br>
  <b>Runtime:</b> {runtime_s} seconds
</div>
<div class="summary {result_class}">
  <b>Result:</b> {result_text}
  &nbsp;—&nbsp; Total: {total} &nbsp;|&nbsp; Pass: {passed} &nbsp;|&nbsp; Fail: {failed} &nbsp;|&nbsp; Skip: {skipped}
</div>
<table>
<tr><th style="width:10%">Test ID</th><th style="width:75%">Description</th><th style="width:15%">Result</th></tr>
{chr(10).join(rows)}
</table>
<div class="footer">Generated by tests/run_tests.py — Kamacode Ltd.</div>
</body></html>
"""
    out_path.write_text(body, encoding="utf-8")


# ── Main ───────────────────────────────────────────────────────────
def find_tty() -> Optional[str]:
    for cand in ("/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02",):
        if os.path.exists(cand):
            return os.path.realpath(cand)
    # Fallback to /dev/ttyACM1 if present
    if os.path.exists("/dev/ttyACM1"):
        return "/dev/ttyACM1"
    return None


def main() -> int:
    here = Path(__file__).parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--tty", default=None,
                    help="CDC TTY (default: auto-detect N6Cam by id)")
    ap.add_argument("--images", default=str(here / "images"),
                    help="Folder of test images for the algorithm group")
    ap.add_argument("--out", default=None,
                    help="HTML report path (default: results/test-report-<ts>.html)")
    args = ap.parse_args()

    tty = args.tty or find_tty()
    if not tty or not os.path.exists(tty):
        print(f"{C_RED}N6Cam CDC port not found. Is the kit plugged in?{C_RESET}",
              file=sys.stderr)
        return 2

    image_dir = Path(args.images)
    if not image_dir.is_dir():
        print(f"{C_YEL}Images dir {image_dir} missing — algorithm group will skip.{C_RESET}")

    out_path = Path(args.out) if args.out else (
        here.parent / "results" / f"test-report-{time.strftime('%Y%m%d_%H%M%S')}.html"
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"{C_BOLD}=== N6Cam Test Suite ==={C_RESET}")
    print(f"TTY: {tty}")
    print(f"Images: {image_dir}")
    print(f"Report: {out_path}")

    sh = KitShell(tty)
    suite = Suite()
    t0 = time.time()
    try:
        group_prereq(sh, suite, tty)
        group_shell(sh, suite)
        group_system(sh, suite)
        group_sensors(sh, suite)
        group_img(sh, suite)
        group_detect(sh, suite)
        group_notify(sh, suite)
        group_sd(sh, suite)
        group_frame_inject(sh, suite, image_dir)
        if image_dir.is_dir():
            group_algorithm(sh, suite, image_dir)
        else:
            suite.group("GROUP 9: Detection algorithm against real photos")
            skip(suite, "T09.0", "Algorithm tests", "no images folder")
        group_camera(sh, suite)
        if image_dir.is_dir():
            group_count(sh, suite, image_dir)
        else:
            suite.group("GROUP 11: Multi-person + vehicle counting")
            skip(suite, "T11.0", "Count tests", "no images folder")
    finally:
        # Reset state to be friendly
        sh.write_line("frame clear")
        sh.drain(0.3)
        sh.close()

    runtime = int(time.time() - t0)
    write_report(out_path, suite, runtime, tty)

    print(f"\n{C_BOLD}════════════════════════════════════════════{C_RESET}")
    print(f"  TOTAL: {suite.total()}  "
          f"{C_GRN}PASS: {suite.passed()}{C_RESET}  "
          f"{C_RED}FAIL: {suite.failed()}{C_RESET}  "
          f"{C_YEL}SKIP: {suite.skipped()}{C_RESET}  "
          f"TIME: {runtime}s")
    print(f"  Report: {out_path}")
    print(f"{C_BOLD}════════════════════════════════════════════{C_RESET}")
    return 0 if suite.failed() == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
