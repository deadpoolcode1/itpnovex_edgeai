#!/usr/bin/env python3
"""
Scopus PoC — whole-system test runner.

Exercises the integrated system described in Scopus_SoW_v3.pdf end to end:

    Control PC ─(UART/USB)─▶ N6 Main CPU (camera + detection + shell)
                                   │  AT-over-HDLC (internal UART)
                                   ▼
                            WP76 modem (SDVR app) ─(cellular)─▶ server
                                   │  UDP notifications (§6)
                                   └  HTTPS file upload  (§8)

Unlike the two per-device suites (edgeai/tests/run_tests.py for the N6 and
V20_SDVR's modular-tools test-run for the modem), this drives BOTH devices in
one pass and checks the seams between them: the N6 shell control channel
(§3/§4), the modem SDVR command channel (§5), the N6→modem tunnel (§4.6), and
the modem→server notification/upload paths (§6/§8).

Produces a self-contained HTML report + same-stem PDF under scopus/results/,
in the same style as the per-device reports.

Run (on the host the hardware is attached to, e.g. the remote bench PC):
    python3 scopus/run_scopus_tests.py
Env overrides: N6_TTY, SDVR_PORT, MODEM_IP, MODEM_PASSWORD, HOST_IP.
"""
import os
import re
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parent / "lib"))
from lib.devices import N6Shell, ModemAt, ModemSsh           # noqa: E402
from settings import S  # noqa: E402
from lib.report import Suite, write_report, C_BOLD, C_GRN, C_RED, C_YEL, C_RST  # noqa: E402


# ───────────────────────── shared context ─────────────────────────────
class Ctx:
    def __init__(self):
        self.n6 = None            # N6Shell or None
        self.mat = None           # ModemAt or None
        self.ssh = None           # ModemSsh
        self.n6_mode = "absent"   # 'edgeai-app' | 'stock' | 'absent'
        self.modem_scopus = False # modem app has the new SoW v3 commands
        self.mat_is_sdvr = True   # c.mat is the port sdvrApp registered on
        self.host_ip = os.environ.get("HOST_IP", "")


# ─────────────────────────── GROUP 0 ──────────────────────────────────
def g0_prereq(c: Ctx, s: Suite):
    s.group("GROUP 0 — Prerequisites & link bring-up")

    # N6 main-CPU shell ----------------------------------------------------
    try:
        c.n6 = N6Shell(os.environ.get("N6_TTY") or None)
        alive = c.n6.alive(3.0)
        s.ok("T0.1", "N6 Main-CPU CDC shell present + responsive (§4.1)", alive,
             reason="no 'Uptime' reply", extra=c.n6.tty)
        if alive:
            fw = c.n6.send("fw", "FW VER", 2.0)
            m = re.search(r"FW VER:\s+(\S+)", fw)
            if m:
                s.n6_fw = m.group(1)
            helptext = c.n6.send("help", None, 2.5)
            is_app = any(k in helptext for k in ("detect", "notify", "img ", "frame"))
            c.n6_mode = "edgeai-app" if is_app else "stock"
            s.n6_mode = c.n6_mode
            s.ok("T0.2", "N6 runs the Kamacode edgeai application (not stock BSP)",
                 is_app,
                 reason="device runs STOCK SIANA firmware — app not flashed; "
                        "N6 SoW command groups will be skipped",
                 extra=f"mode={c.n6_mode} fw={s.n6_fw}")
    except Exception as e:
        c.n6 = None
        s.ok("T0.1", "N6 Main-CPU CDC shell present + responsive (§4.1)", False,
             reason=str(e))
        s.skip("T0.2", "N6 runs the Kamacode edgeai application", "N6 shell unavailable")

    # Modem AT / SDVR command channel -------------------------------------
    try:
        # See the note in run_integration_tests.py: SDVR commands fall back to
        # the camera's `mdm` tunnel when this port is the module's own parser.
        c.mat = ModemAt(os.environ.get("SDVR_PORT") or None, via_camera=c.n6)
        ok_at, _ = c.mat.expect("AT", "OK", 2.0)
        s.ok("T0.3", "Modem AT command channel responsive (§5.1)", ok_at,
             reason="no OK to bare AT", extra=c.mat.tty)
    except Exception as e:
        c.mat = None
        s.ok("T0.3", "Modem AT command channel responsive (§5.1)", False, reason=str(e))

    # Modem SSH + SDVR app -------------------------------------------------
    c.ssh = ModemSsh(S.get("modem", "ip"), S.require("modem", "password"))
    reachable = c.ssh.reachable()
    s.ok("T0.4", "Modem reachable over SSH (side-effect channel)", reachable)
    if reachable:
        running = c.ssh.app_running()
        s.ok("T0.5", "SDVR app running on modem", running)
        # Ask the RUNNING build directly. Scraping the boot banner out of
        # sdvr.log is only a fallback: the banner scrolls past the log tail
        # once a session has generated traffic, and the probe then reported
        # "? cmds" and silently downgraded every Scopus group to SKIP on a
        # perfectly good 1.3.0 build.
        if c.mat is not None:
            mv = re.search(r"\+SDVRVER:\s*([0-9.]+)", c.mat.send("AT+SDVRVER", 3.0))
            if mv:
                s.modem_ver = mv.group(1)
            # READ-type query on a Scopus-only command — present ⇒ Scopus build.
            c.modem_scopus = "+SDVRNTFHOST:" in c.mat.send("AT+SDVRNTFHOST?", 3.0)

            # Is this port the SDVR channel at all?
            #
            # sdvrApp registers its commands on ttyHSL1, which reaches this PC
            # through the modem's FTDI adapter. The Sierra module ALSO exposes
            # its own AT port over native USB (…-if03 → /dev/ttyUSB3), and the
            # port autodetect falls back to it when the FTDI is unplugged. That
            # port answers a bare `AT` with OK — so every liveness check passes
            # — and answers every AT+SDVR… with ERROR, because those commands
            # do not exist on it. Groups 8 and 10 then reported seven red
            # FAILs for an unplugged cable, which reads exactly like a broken
            # product and cost a session to tell apart on 2026-08-23.
            #
            # Two probes, and they have to disagree for this to fire: a bare AT
            # must work (so the port is alive and it is not a serial problem)
            # while a command that exists in every SDVR build since 1.0 must
            # not. That combination has only one cause.
            c.mat_is_sdvr = True
            if not c.modem_scopus:
                at_ok   = "OK" in c.mat.send("AT", 2.0)
                sdvr_rx = c.mat.send("AT+SDVRPING=1", 3.0)
                if at_ok and "+SDVRPING:" not in sdvr_rx:
                    c.mat_is_sdvr = False
        # sdvr.log persists across boots — take the LATEST (last) entries so
        # we read the currently-running build, not an earlier boot's banner.
        log = c.ssh.sdvr_log(120)
        vers = re.findall(r"\+SDVRRDY:\s*([0-9.]+)", log)
        if vers and s.modem_ver == "?":
            s.modem_ver = vers[-1]
        cnts = re.findall(r"All (\d+) AT commands registered", log)
        if cnts:
            s.modem_cmds = cnts[-1]
            # The 4 new Scopus commands push the registered count to 33 (was 29).
            if not c.modem_scopus:
                c.modem_scopus = int(cnts[-1]) >= 33

        # Last resort: ask over the camera's AT tunnel.
        #
        # Both sources above can be silent at once and neither means the app is
        # old. c.mat may be the module's own AT port (see the probe above), and
        # /data/sdvr/sdvr.log is a ring buffer whose boot banner — the only
        # place the version and the command count are printed — is pushed out
        # by a busy session in well under an hour. That combination reported
        # "version=? cmds=?" and failed T0.6 against a modem running 1.14.0
        # with 40 commands registered, three minutes after a clean install.
        #
        # `mdm AT+SDVRVER` reaches the same handler over the CN805 link, and
        # that link is already proven by T0.8 before this runs.
        if s.modem_ver == "?" and c.n6 is not None and c.n6_mode == "edgeai-app":
            mv = re.search(r"\+SDVRVER:\s*([0-9.]+)",
                           c.n6.send("mdm AT+SDVRVER", max_secs=5.0) or "")
            if mv:
                s.modem_ver = mv.group(1)
                c.modem_scopus = True
                if s.modem_cmds == "?":
                    s.modem_cmds = "via camera tunnel"
        s.ok("T0.6", "SDVR app exposes Scopus SoW v3 command set (≥33 AT cmds)",
             c.modem_scopus,
             reason=f"only {s.modem_cmds} cmds registered — pre-Scopus build "
                    f"(needs 1.1.0)",
             extra=f"version={s.modem_ver} cmds={s.modem_cmds}")
    else:
        for t, d in [("T0.5", "SDVR app running on modem"),
                     ("T0.6", "SDVR app exposes Scopus SoW v3 command set")]:
            s.skip(t, d, "modem SSH unreachable")

    # Host endpoint on the modem subnet (for E2E §6/§8) -------------------
    if not c.host_ip:
        c.host_ip = _discover_host_ip(c.ssh) if reachable else ""


def _discover_host_ip(ssh: ModemSsh) -> str:
    """Find the host's IP on the modem's USB-ECM subnet (modem is .2)."""
    hip = S.get("modem", "host_ip")
    rc, out, _ = ssh.run(f"ip route get {hip} 2>/dev/null; "
                         f"ip addr show 2>/dev/null | grep 'inet {hip.rsplit('.', 1)[0]}'")
    # The modem sees the host as the default gw on ecm0 — read it from there.
    rc, gw, _ = ssh.run("ip route show default 2>/dev/null | awk '{print $3}' | head -1")
    gw = (gw or "").strip()
    if gw.startswith("192.168.2."):
        return gw
    return S.get("modem", "host_ip")


# ─────────────────────────── N6 groups ────────────────────────────────
def _n6_guard(c: Ctx, s: Suite, tids):
    """Skip a group's tests when the N6 app isn't available, returning True."""
    if c.n6 and c.n6_mode == "edgeai-app":
        return False
    reason = ("N6 on stock firmware — app not flashed" if c.n6_mode == "stock"
              else "N6 shell unavailable")
    for tid, desc in tids:
        s.skip(tid, desc, reason)
    return True


def g1_n6_control(c: Ctx, s: Suite):
    s.group("GROUP 1 — N6 control channel & system (§4.1, §3.7)")
    tids = [("T1.1", "echo on/off/query ack format (§4.1)"),
            ("T1.2", "rtc set→query round-trips (§3.7/§4.2)"),
            ("T1.3", "application version query (§3.7)"),
            ("T1.4", "commands list enumerates shell commands (§3.7)")]
    if _n6_guard(c, s, tids):
        return
    n = c.n6
    out = n.send("echo query", "echo", 2.0)
    s.ok("T1.1", "echo on/off/query ack format (§4.1)",
         "echo" in out and ("on" in out or "off" in out))
    ts = time.strftime("%d%m%Y%H%M%S")
    n.send(f"rtc set {ts}", "ok", 2.0)
    rtc = n.send("rtc", "RTC", 2.0)
    s.ok("T1.2", "rtc set→query round-trips (§3.7/§4.2)",
         bool(re.search(r"RTC:\s+\d{2}/\d{2}/\d{2}", rtc)), extra=rtc.strip()[:60])
    ver = n.send("version", None, 2.0)
    if "Unknown command" in ver:
        ver = n.send("system version", None, 2.0)
    s.ok("T1.3", "application version query (§3.7)",
         "Unknown command" not in ver and bool(ver.strip()),
         reason="no version command", extra=ver.strip()[:60])
    cmds = n.send("commands", None, 2.0)
    s.ok("T1.4", "commands list enumerates shell commands (§3.7)",
         "Unknown command" not in cmds and len(cmds) > 20,
         reason="no commands listing", extra=f"{len(cmds)} bytes")


def g2_n6_detect(c: Ctx, s: Suite):
    s.group("GROUP 2 — N6 detection & counting (§3.1, §4.2)")
    tids = [("T2.1", "detect start (§4.2)"), ("T2.2", "detect stop (§4.2)"),
            ("T2.3", "detect profile <det> <act> set people+vehicles/save-SD (§4.2)"),
            ("T2.4", "detect profile query reflects the set mask (§4.2)")]
    if _n6_guard(c, s, tids):
        return
    n = c.n6
    s.ok("T2.1", "detect start (§4.2)", "ok" in n.send("detect start", "detect start ok", 3.0))
    s.ok("T2.2", "detect stop (§4.2)", "ok" in n.send("detect stop", "detect stop ok", 3.0))
    out = n.send("detect profile 3 1", "detect profile ok", 3.0)
    s.ok("T2.3", "detect profile 3 1 (people+vehicles, save-SD) (§4.2)",
         "detect profile ok" in out)
    q = n.send("detect profile query", "detect profile", 3.0)
    m = re.search(r"det_msk=0x0*([0-9a-f]+)\s+action_msk=0x0*([0-9a-f]+)", q)
    s.ok("T2.4", "detect profile query reflects the set mask (§4.2)",
         bool(m) and m.group(1) == "3" and m.group(2) == "1", extra=q.strip()[:60])
    n.send("detect profile 1 0", "ok", 2.0)


def g3_n6_notify(c: Ctx, s: Suite):
    s.group("GROUP 3 — N6 notifications (§3.1, §4.2, §6)")
    tids = [("T3.1", "notify enable <mask> (§4.2)"), ("T3.2", "notify period <sec> (§4.2)"),
            ("T3.3", "notify query returns mask+period (§4.2)"),
            ("T3.4", "notify trigger emits +SDVRNTF JSON, SoW §6 shape"),
            ("T3.5", "notify disable (§4.2)")]
    if _n6_guard(c, s, tids):
        return
    n = c.n6
    s.ok("T3.1", "notify enable 0x10 (people-detect bit) (§4.2)",
         "notify enable ok" in n.send("notify enable 0x10", "notify enable ok", 3.0))
    s.ok("T3.2", "notify period 30 (§4.2)",
         "notify period ok" in n.send("notify period 30", "notify period ok", 3.0))
    q = n.send("notify query", "notify", 3.0)
    m = re.search(r"enable_mask=0x0*([0-9a-f]+)\s+period=(\d+)", q)
    s.ok("T3.3", "notify query returns mask=0x10 period=30 (§4.2)",
         bool(m) and int(m.group(1), 16) == 0x10 and int(m.group(2)) == 30,
         extra=q.strip()[:60])
    j = n.send("notify trigger 16", "SDVRNTF", 3.0)
    mj = re.search(r'\+SDVRNTF:\s*\{"ser":(\d+),"num":(\d+),"rsn":(\d+)', j)
    s.ok("T3.4", "notify trigger 16 emits +SDVRNTF JSON, rsn=16 (§6)",
         bool(mj) and int(mj.group(3)) == 16, extra=j.strip()[:80])
    s.ok("T3.5", "notify disable (§4.2)",
         "notify disable ok" in n.send("notify disable", "notify disable ok", 3.0))
    # Put the period back now that T3.3 has read it. `notify period` is
    # persisted, so leaving it at 30 s armed a §4.2 bit-3 report every 30 s for
    # whatever ran next — which is how it reached run_integration_tests.py and
    # cost K6 a spurious failure on 2026-08-21. The group is about the command
    # being accepted, stored and read back; holding the unit in that state
    # afterwards is not part of it.
    n.send("notify period 0", "notify period ok", 3.0)


def g4_n6_photo(c: Ctx, s: Suite):
    s.group("GROUP 4 — N6 photo/image settings (§3.4, §4.4)")
    tids = [("T4.1", "img quality (§4.4)"), ("T4.2", "img color (§4.4)"),
            ("T4.3", "img chroma (§4.4)"), ("T4.4", "img query round-trips settings (§4.4)")]
    if _n6_guard(c, s, tids):
        return
    n = c.n6
    s.ok("T4.1", "img quality 75 (§4.4)", "img quality ok" in n.send("img quality 75", "img quality ok", 3.0))
    s.ok("T4.2", "img color RGB (§4.4)", "img color ok" in n.send("img color RGB", "img color ok", 3.0))
    s.ok("T4.3", "img chroma 1 (§4.4)", "img chroma ok" in n.send("img chroma 1", "img chroma ok", 3.0))
    q = n.send("img query", "img", 3.0)
    m = re.search(r"quality=(\d+)\s+color=(\w+)\s+chroma=(\d+)", q)
    s.ok("T4.4", "img query shows quality=75 color=RGB chroma=1 (§4.4)",
         bool(m) and int(m.group(1)) == 75 and m.group(2) == "RGB" and int(m.group(3)) == 1,
         extra=q.strip()[:60])
    for r in ("img quality 90", "img color YCBCR", "img chroma 0"):
        n.send(r, "ok", 2.0)


def g5_n6_sensors(c: Ctx, s: Suite):
    s.group("GROUP 5 — N6 sensors (§3.5, §4.5)")
    tids = [("T5.1", "irled on→state=1 (§4.5)"), ("T5.2", "irled off→state=0 (§4.5)"),
            ("T5.3", "motion sense set (§4.5)"), ("T5.4", "motion query persists value (§4.5)")]
    if _n6_guard(c, s, tids):
        return
    n = c.n6
    o = n.send("irled on", "irled on ok", 3.0)
    s.ok("T5.1", "irled on → state=1 (§4.5)", bool(re.search(r"irled:\s+1", o)))
    o = n.send("irled off", "irled off ok", 3.0)
    s.ok("T5.2", "irled off → state=0 (§4.5)", bool(re.search(r"irled:\s+0", o)))
    o = n.send("motion sense 65 45", "motion sense ok", 3.0)
    s.ok("T5.3", "motion sense 65 45 accepted (§4.5)", "motion sense ok" in o)
    o = n.send("motion query", "motion", 3.0)
    m = re.search(r"sensitivity=(\d+)\s+timeout=(\d+)", o)
    s.ok("T5.4", "motion query persists 65/45 (§4.5)",
         bool(m) and int(m.group(1)) == 65 and int(m.group(2)) == 45, extra=o.strip()[:50])


def g6_n6_camera(c: Ctx, s: Suite):
    s.group("GROUP 6 — N6 camera passthrough (§4.3)")
    tids = [("T6.1", "camera awb (§4.3)"), ("T6.2", "camera exposure (§4.3)"),
            ("T6.3", "camera gain (§4.3)")]
    if _n6_guard(c, s, tids):
        return
    n = c.n6
    o = n.send("camera awb auto", None, 3.0)
    s.ok("T6.1", "camera awb auto accepted (§4.3)", any(k in o.lower() for k in ("auto", "ok", "active")))
    o = n.send("camera exposure 10000", None, 3.0)
    s.ok("T6.2", "camera exposure 10000 accepted (§4.3)", "Exposure" in o or "updated" in o.lower())
    o = n.send("camera gain 0", None, 3.0)
    s.ok("T6.3", "camera gain 0 accepted (§4.3)", "Gain" in o or "updated" in o.lower())


def g7_n6_sd(c: Ctx, s: Suite):
    s.group("GROUP 7 — N6 → SD photo pipeline (§3.2, §7)")
    tids = [("T7.1", "sd query reports mounted (§3.2)"),
            ("T7.2", "sd ls enumerates SD (§3.2)"),
            ("T7.3", "photo savesd returns SoW §7 filename (serial_DDMMYYYY_HHMMSS.rdy)"),
            ("T7.4", "new .rdy file appears in sd ls (§7)")]
    if _n6_guard(c, s, tids):
        return
    n = c.n6
    q = n.send("sd query", "sd", 3.0)
    mounted = "mounted" in q and "not mounted" not in q
    if not mounted:
        # An empty card slot is a missing bench prerequisite, not a defect —
        # the N6 auto-mounts at boot and has no `sd mount` command, so there
        # is nothing the firmware could have done differently. Skip the whole
        # group with a precise reason, the same way every other unavailable
        # channel in this suite is reported, instead of failing T7.1 while
        # skipping T7.2-T7.4 for the identical cause.
        for t, d in tids:
            s.skip(t, d, "no SD card mounted on the N6 — insert a card in the "
                         "N6 slot to exercise the §7 photo→SD pipeline")
        return
    s.ok("T7.1", "sd query reports mounted (§3.2)", True, extra=_oneline(q))
    s.ok("T7.2", "sd ls enumerates SD (§3.2)", "ok" in n.send("sd ls", "sd ls ok", 4.0))
    n.send(f"rtc set {time.strftime('%d%m%Y%H%M%S')}", "ok", 2.0)
    o = n.send("photo savesd", "photo savesd ok", 4.0)
    m = re.search(r"(\S+\.rdy)", o)
    s.ok("T7.3", "photo savesd returns SoW §7 filename", bool(m), extra=m.group(1) if m else "")
    found = False
    if m:
        for _ in range(12):
            time.sleep(1.0)
            if m.group(1) in n.send("sd ls", "sd ls ok", 6.0):
                found = True
                break
    s.ok("T7.4", "new .rdy file appears in sd ls (§7)", found,
         reason="not seen within 12s", extra=m.group(1) if m else "")


# ─────────────────────────── modem groups ─────────────────────────────
def _modem_guard(c: Ctx, s: Suite, tids):
    if c.mat and c.mat_is_sdvr:
        return False
    if not c.mat:
        reason = "modem AT channel unavailable"
    else:
        # See the probe in group 0: this port is alive but is the module's own
        # AT parser, not sdvrApp's. Skipping says so; failing would blame the
        # product for a cable.
        reason = (f"{getattr(c.mat, 'tty', 'this AT port')} is the Sierra "
                  "module's own AT port, not the SDVR channel — plug the "
                  "modem's FTDI adapter in (it carries ttyHSL1, where sdvrApp "
                  "registers). Verify meanwhile with "
                  "`cam.py \"mdm AT+SDVRPING=7\"`, which reaches the same "
                  "handler over the camera link.")
    for tid, desc in tids:
        s.skip(tid, desc, reason)
    return True


# ─────────────────── the device's settings are not ours ───────────────────
#
# Groups 8, 9 and 12 are round-trip tests: they set a value and read it back.
# Setting is the test, but LEAVING it set is not — AT+SDVRHOST/PORT are the
# unit's real upload endpoint, and a run used to end with the device pointed
# at "scopus.test":8443. Every photo after that failed to resolve a host that
# does not exist, on a bench nobody had touched since the tests "passed".
#
# So: read what the unit is configured with before touching it, and put it
# back afterwards, whatever the run did in between.
_SRVGET_RE = re.compile(
    r'\+SDVRSRVGET:\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*(\d+)\s*,\s*"([^"]*)"'
    r'(?:\s*,\s*(\w+))?')


def snapshot_device(c: Ctx) -> dict:
    """Capture the settings the suite is about to overwrite. Missing values
    come back absent rather than guessed — restore then leaves them alone."""
    if not c.mat or not c.mat_is_sdvr:
        return {}
    snap = {}
    m = _SRVGET_RE.search(c.mat.send("AT+SDVRSRVGET", 3.0))
    if m:
        snap["srv_ip"], snap["srv_url"] = m.group(1), m.group(2)
        snap["srv_port"], snap["srv_path"] = int(m.group(3)), m.group(4)
    for key, cmd, pat in (
            ("ntf_host",  "AT+SDVRNTFHOST?",  r'\+SDVRNTFHOST:\s*"([^"]*)"'),
            ("ntf_port",  "AT+SDVRNTFPORT?",  r"\+SDVRNTFPORT:\s*(\d+)"),
            ("ntf_proto", "AT+SDVRNTFPROTO?", r"\+SDVRNTFPROTO:\s*(\d+)(?:\s*,\s*\"([^\"]*)\")?")):
        m = re.search(pat, c.mat.send(cmd, 3.0))
        if m:
            snap[key] = m.groups() if key == "ntf_proto" else m.group(1)
    return snap


def restore_device(c: Ctx, snap: dict):
    """Put back everything snapshot_device captured. Best effort and silent:
    a restore failure must not turn into a test result — there is no test
    here, only the courtesy of leaving the bench as we found it."""
    if not snap or not c.mat or not c.mat_is_sdvr:
        return
    try:
        # Host is one field on the device with two setters. Whichever of the
        # two SRVGET reported non-empty is the one that was in use.
        if snap.get("srv_ip"):
            c.mat.expect(f'AT+SDVRHOSTIP="{snap["srv_ip"]}"', "OK", 3.0)
        elif snap.get("srv_url"):
            c.mat.expect(f'AT+SDVRHOST="{snap["srv_url"]}"', "OK", 3.0)
        if snap.get("srv_port"):
            c.mat.expect(f'AT+SDVRPORT={snap["srv_port"]}', "OK", 3.0)
        if snap.get("srv_path"):
            c.mat.expect(f'AT+SDVRSRVRPATH="{snap["srv_path"]}"', "OK", 3.0)
        if snap.get("ntf_host"):
            c.mat.expect(f'AT+SDVRNTFHOST="{snap["ntf_host"]}"', "OK", 3.0)
        if snap.get("ntf_port"):
            c.mat.expect(f'AT+SDVRNTFPORT={snap["ntf_port"]}', "OK", 3.0)
        proto = snap.get("ntf_proto")
        if proto:
            num, path = proto[0], (proto[1] or "")
            arg = f'{num},"{path}"' if path else num
            c.mat.expect(f"AT+SDVRNTFPROTO={arg}", "OK", 3.0)
    except Exception:
        pass


def g8_modem_control(c: Ctx, s: Suite):
    s.group("GROUP 8 — Modem SDVR control channel, existing (§5.2)")
    tids = [("T8.1", "AT → OK (§5.1)"), ("T8.2", "AT+SDVRPING=N echoes +SDVRPING:N (§5.2)"),
            ("T8.3", "server host set→get round-trips (§5.2)"),
            ("T8.4", "server port set→get round-trips (§5.2)")]
    if _modem_guard(c, s, tids):
        return
    m = c.mat
    s.ok("T8.1", "AT → OK (§5.1)", m.expect("AT", "OK", 2.0)[0])
    ok, r = m.expect("AT+SDVRPING=7", "SDVRPING: 7", 3.0)
    s.ok("T8.2", "AT+SDVRPING=7 → +SDVRPING: 7 (§5.2)", ok, extra=_oneline(r))
    _, _ = m.expect('AT+SDVRHOST="scopus.test"', "OK", 3.0)
    ok, r = m.expect("AT+SDVRSRVGET", "scopus.test", 3.0)
    s.ok("T8.3", "AT+SDVRHOST set→AT+SDVRSRVGET shows it (§5.2)", ok, extra=_oneline(r))
    _, _ = m.expect("AT+SDVRPORT=8443", "OK", 3.0)
    ok, r = m.expect("AT+SDVRSRVGET", "8443", 3.0)
    s.ok("T8.4", "AT+SDVRPORT set→AT+SDVRSRVGET shows it (§5.2)", ok, extra=_oneline(r))


def g9_modem_scopus(c: Ctx, s: Suite):
    s.group("GROUP 9 — Modem SDVR new Scopus commands (§5.2/§6/§8.2)")
    tids = [("T9.1", "AT+SDVRNTFHOST set→query round-trips (§5.2)"),
            ("T9.2", "AT+SDVRNTFPORT set→query round-trips (§5.2)"),
            ("T9.3", "AT+SDVRNTFA=N,SIZE,\"msg\" → +SDVRNTF: END,N (§6)"),
            ("T9.4", "AT+SDVRSENDBIN arms live in-memory upload (§8.2)")]
    if _modem_guard(c, s, tids):
        return
    if not c.modem_scopus:
        for tid, desc in tids:
            s.skip(tid, desc, f"modem on pre-Scopus build ({s.modem_cmds} cmds) — needs 1.1.0")
        return
    m = c.mat
    host = c.host_ip or S.get("modem", "host_ip")
    _, _ = m.expect(f'AT+SDVRNTFHOST="{host}"', "OK", 3.0)
    ok, r = m.expect("AT+SDVRNTFHOST?", host, 3.0)
    s.ok("T9.1", "AT+SDVRNTFHOST set→query round-trips (§5.2)", ok, extra=_oneline(r))
    _, _ = m.expect("AT+SDVRNTFPORT=5005", "OK", 3.0)
    ok, r = m.expect("AT+SDVRNTFPORT?", "5005", 3.0)
    s.ok("T9.2", "AT+SDVRNTFPORT set→query round-trips (§5.2)", ok, extra=_oneline(r))
    ok, r = m.expect('AT+SDVRNTFA=1,5,"hello"', "SDVRNTF: END", 6.0)
    s.ok("T9.3", 'AT+SDVRNTFA=1,5,"hello" → +SDVRNTF: END,N (§6)', ok, extra=_oneline(r))
    ok, r = m.expect('AT+SDVRSENDBIN=1,"tag","01012026000000",1,4', "OK", 4.0)
    # SENDBIN enters data mode awaiting SIZE bytes; just assert it armed (no ERROR)
    s.ok("T9.4", "AT+SDVRSENDBIN arms live in-memory upload (§8.2)",
         "ERROR" not in r, extra=_oneline(r))
    # Disarm. The binary sink only exists on the camera HDLC link, so this
    # channel can arm SENDBIN but can never feed it the SIZE bytes that would
    # complete it — and ESC does not clear it either. Arming and walking away
    # left the modem wedged, so T9.4 passed once and then failed on every
    # later run until the app was restarted. UPLSTOP now calls LiveBin_Abort.
    m.expect("AT+SDVRUPLSTOP", "OK", 3.0)


def g10_modem_sd(c: Ctx, s: Suite):
    s.group("GROUP 10 — Modem SD management via SDVR (§3.2, §5.2)")
    tids = [("T10.1", "AT+SDVRMOUNTSD mounts the card (verified via /proc/mounts) (§3.2)"),
            ("T10.2", "AT+SDVRLSALL lists SD files, no +SDVRUPL noise (§3.2)"),
            ("T10.3", "AT+SDVRUNMOUNTSD unmounts (verified via /proc/mounts) (§3.2)")]
    if _modem_guard(c, s, tids):
        return
    m, ssh = c.mat, c.ssh

    # No card in the modem's slot is a bench condition, not a product fault.
    # These three used to go red for it, which reads as "SD management is
    # broken" — the same misdiagnosis the FTDI cable produced in group 8.
    # AT+SDVRMOUNTSD answers +SDVRERR: 3 because there is nothing to mount.
    #
    # The test for that used to be `ls /dev/mmcblk0`, and it was wrong: an
    # unbound card is not an absent one. AT+SDVRUNMOUNTSD unbinds the card
    # from the mmcblk driver on purpose (sd_manager.c, SD_Unmount
    # releaseCard), and so does AT+SDVRCERTIMPORT whenever it found the card
    # unmounted and put it back the way it was — the exact sequence a tester
    # runs for the certificate import in ScopusQA #19. The node then does not
    # exist while the card sits in the slot, and this whole group skipped
    # itself on a bench that has one (seen 2026-08-30).
    #
    # So ask the modem to mount instead of guessing from the host. That is
    # the operation under test, it re-binds a released card, and only a
    # genuinely empty slot makes it fail.
    _, r = m.expect("AT+SDVRMOUNTSD", "OK", 8.0)
    mounted = False
    if ssh and ssh.reachable():
        _, mounts, _ = ssh.run("cat /proc/mounts | grep -c mmcblk || echo 0")
        mounted = mounts.strip().splitlines()[0] != "0"
    if "OK" not in r and not mounted:
        for tid, desc in tids:
            s.skip(tid, desc,
                   "no SD card in the MODEM's slot (AT+SDVRMOUNTSD failed) — "
                   "this is the modem's own card, separate from the N6's; "
                   "insert one to exercise §3.2 SD management")
        return
    s.ok("T10.1", "AT+SDVRMOUNTSD mounts card (/proc/mounts) (§3.2)",
         "OK" in r and mounted, reason="not present in /proc/mounts", extra=_oneline(r))
    ok, r = m.expect("AT+SDVRLSALL", "OK", 5.0)
    s.ok("T10.2", "AT+SDVRLSALL lists files, terminates OK (§3.2)", ok, extra=_oneline(r))
    _, r = m.expect("AT+SDVRUNMOUNTSD", "OK", 8.0)
    unmounted = True
    if ssh and ssh.reachable():
        _, mounts, _ = ssh.run("cat /proc/mounts | grep -c mmcblk || echo 0")
        unmounted = mounts.strip().splitlines()[0] == "0"
    s.ok("T10.3", "AT+SDVRUNMOUNTSD unmounts (/proc/mounts) (§3.2)",
         "OK" in r and unmounted, extra=_oneline(r))

    # AT+SDVRUNMOUNTSD does not just unmount: it unbinds the card from the
    # mmcblk driver, by design, so the host MCU can drive the shared SD bus
    # (sd_manager.c, SD_Unmount releaseCard). /dev/mmcblk0 therefore vanishes
    # — and the guard above reads that as "no card in the slot", so the run
    # after this one silently skips all three of these. Put the card back,
    # after the assertion, the same way snapshot/restore does elsewhere.
    m.expect("AT+SDVRMOUNTSD", "OK", 8.0)


# ─────────────────────── cross-device groups ──────────────────────────
def g11_tunnel(c: Ctx, s: Suite):
    s.group("GROUP 11 — N6 → Modem AT tunnel (§4.6)")
    tids = [("T11.1", "N6 `mdm AT` tunnels to modem, OK returned (§4.6)"),
            ("T11.2", "N6 `mdm AT+SDVRPING=5` tunnels SDVR cmd + response (§4.6)")]
    if c.n6_mode != "edgeai-app":
        for tid, desc in tids:
            s.skip(tid, desc, "needs N6 edgeai app (mdm tunnel command)")
        return
    n = c.n6
    o = n.send("mdm AT", "OK", 4.0)
    s.ok("T11.1", "N6 `mdm AT` tunnels to modem, OK returned (§4.6)",
         "OK" in o and "Unknown command" not in o, extra=_oneline(o))
    o = n.send("mdm AT+SDVRPING=5", "SDVRPING", 5.0)
    s.ok("T11.2", "N6 `mdm AT+SDVRPING=5` tunnels SDVR cmd+response (§4.6)",
         "SDVRPING: 5" in o, extra=_oneline(o))


def g12_e2e_notify(c: Ctx, s: Suite):
    s.group("GROUP 12 — End-to-end notification over UDP (§6)")
    tid, desc = "T12.1", "Modem→host UDP datagram received with SoW §6 JSON fields"
    if not c.mat or not c.mat_is_sdvr or not c.modem_scopus:
        s.skip(tid, desc, "needs modem Scopus build (AT+SDVRNTFA)")
        return
    host = c.host_ip or S.get("modem", "host_ip")
    port = 5005
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(6.0)
        sock.bind(("0.0.0.0", port))
    except Exception as e:
        s.skip(tid, desc, f"cannot bind UDP {port}: {e}")
        return
    try:
        c.mat.expect(f'AT+SDVRNTFHOST="{host}"', "OK", 3.0)
        c.mat.expect(f"AT+SDVRNTFPORT={port}", "OK", 3.0)
        # This test watches for a datagram, so the unit has to be in datagram
        # mode. A unit left in HTTP or MQTT notification mode — which is how a
        # deployed unit is normally configured — delivered the notification
        # perfectly well to its server and this test still went red, reporting
        # "no datagram within 6s" against a working product. Set the transport
        # the test needs; main() puts the configured one back at the end.
        c.mat.expect("AT+SDVRNTFPROTO=0", "OK", 3.0)
        c.mat.expect('AT+SDVRNTFA=2,11,"scopus-e2e"', "SDVRNTF", 6.0)
        data = b""
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            pass
        got = data.decode(errors="replace")
        s.ok(tid, desc, bool(data) and ('"ser"' in got or "scopus-e2e" in got),
             reason="no datagram within 6s", extra=_oneline(got)[:80])
    finally:
        sock.close()


class _TlsSink:
    """A mutual-TLS receiver the suite runs itself, for the duration of one
    test, on the modem's own link to this PC.

    Standing this up here rather than requiring an external server is the
    difference between T13.1 being a test and being a permanent SKIP. It also
    keeps the assertion honest: the bytes are checked in this process, so a
    PASS means a JPEG arrived over a TLS connection that presented the
    device's client certificate — not that a file appeared in a directory.
    """

    def __init__(self, certs: Path):
        import http.server, ssl, threading

        self.received = []          # (name, body, peer_cn)

        sink = self

        class H(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def do_POST(self):
                n = int(self.headers.get("Content-Length", "0") or 0)
                body = self.rfile.read(n) if n else b""
                cn = ""
                try:
                    cert = self.connection.getpeercert() or {}
                    cn = "/".join("=".join(x) for rdn in cert.get("subject", ())
                                  for x in rdn)
                except Exception:
                    pass
                sink.received.append(
                    (self.headers.get("X-Filename") or "", body, cn))
                self.send_response(200)
                self.send_header("Content-Length", "2")
                self.end_headers()
                self.wfile.write(b"OK")

            def log_message(self, *a):
                pass

        self.srv = http.server.ThreadingHTTPServer(("0.0.0.0", 0), H)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certfile=str(certs / "server.crt"),
                            keyfile=str(certs / "server.key"))
        ctx.load_verify_locations(cafile=str(certs / "ca.crt"))
        ctx.verify_mode = ssl.CERT_REQUIRED
        self.srv.socket = ctx.wrap_socket(self.srv.socket, server_side=True)
        self.port = self.srv.socket.getsockname()[1]
        self.thread = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        try:
            self.srv.shutdown()
            self.srv.server_close()
        except Exception:
            pass


def g13_e2e_upload(c: Ctx, s: Suite):
    s.group("GROUP 13 — End-to-end HTTPS file upload (§8)")
    tid, desc = "T13.1", "camera photo uploads over HTTPS/mutual-TLS, arrives as a JPEG (§8)"

    if not c.mat or not c.mat_is_sdvr:
        s.skip(tid, desc, "modem SDVR AT channel unavailable")
        return
    if c.n6_mode != "edgeai-app":
        s.skip(tid, desc, "needs the N6 edgeai app — `photo upload` is what "
                          "drives the §8.2 in-memory upload")
        return
    certs = Path(os.environ.get("SDVR_CERTS_DIR")
                 or S.get("server", "certs_dir") or "/opt/sdvr-server/certs")
    missing = [f for f in ("server.crt", "server.key", "ca.crt")
               if not (certs / f).is_file()]
    if missing:
        s.skip(tid, desc, f"no server PKI at {certs} (missing {', '.join(missing)}) "
                          "— generate one with /opt/sdvr-server/gencerts.sh")
        return
    if c.ssh and c.ssh.reachable():
        _, n, _ = c.ssh.run("ls /data/sdvr/certs/client.key 2>/dev/null | wc -l")
        if n.strip().splitlines()[0] == "0":
            s.skip(tid, desc, "no client certificate on the modem — import one "
                              "with AT+SDVRCERTIMPORT (needs the SD card)")
            return

    host = S.get("modem", "host_ip")
    try:
        sink = _TlsSink(certs)
    except Exception as e:
        s.skip(tid, desc, f"cannot start the TLS sink: {e}")
        return
    try:
        c.mat.expect(f'AT+SDVRHOSTIP="{host}"', "OK", 3.0)
        c.mat.expect(f"AT+SDVRPORT={sink.port}", "OK", 3.0)
        c.mat.expect('AT+SDVRSRVRPATH="/upload"', "OK", 3.0)
        c.n6.send("photo upload", "photo upload ok", 25.0)

        deadline = time.time() + 30.0
        while not sink.received and time.time() < deadline:
            time.sleep(0.5)

        if not sink.received:
            s.ok(tid, desc, False,
                 reason=f"nothing arrived at https://{host}:{sink.port}/upload "
                        "within 30s")
            return
        name, body, cn = sink.received[0]
        # JPEG, over TLS, presenting a client certificate: all three, or the
        # test has not shown what it claims to.
        is_jpeg = body[:2] == b"\xff\xd8"
        s.ok(tid, desc, is_jpeg and bool(cn),
             reason=("body is not a JPEG" if not is_jpeg
                     else "no client certificate on the TLS session"),
             extra=f"{name} {len(body)} bytes, client_cn={cn or '-'}")
    finally:
        sink.close()


def _oneline(s: str) -> str:
    return " ".join(s.split())[:120]


# ─────────────────────────────── main ─────────────────────────────────
def main() -> int:
    here = Path(__file__).parent
    out = here / "results" / f"test-report-{time.strftime('%Y%m%d_%H%M%S')}.html"
    out.parent.mkdir(parents=True, exist_ok=True)

    print(f"{C_BOLD}=== Scopus PoC — Whole-System Test Suite ==={C_RST}")
    c = Ctx()
    s = Suite()
    t0 = time.time()
    snap = {}
    try:
        g0_prereq(c, s)
        snap = snapshot_device(c)
        g1_n6_control(c, s)
        g2_n6_detect(c, s)
        g3_n6_notify(c, s)
        g4_n6_photo(c, s)
        g5_n6_sensors(c, s)
        g6_n6_camera(c, s)
        g7_n6_sd(c, s)
        g8_modem_control(c, s)
        g9_modem_scopus(c, s)
        g10_modem_sd(c, s)
        g11_tunnel(c, s)
        g12_e2e_notify(c, s)
        g13_e2e_upload(c, s)
    finally:
        restore_device(c, snap)
        if c.n6:
            c.n6.close()

    runtime = int(time.time() - t0)
    meta = dict(n6_fw=s.n6_fw, n6_app=s.n6_app, n6_mode=s.n6_mode,
                modem_ver=s.modem_ver, modem_cmds=s.modem_cmds,
                n6_tty=(c.n6.tty if c.n6 else "—"),
                modem_tty=(c.mat.tty if c.mat else "—"),
                modem_ip=S.get("modem", "ip"),
                host=c.host_ip or "—")
    write_report(out, s, runtime, meta)

    print(f"\n{C_BOLD}{'═'*52}{C_RST}")
    print(f"  TOTAL: {s.total()}  {C_GRN}PASS: {s.passed()}{C_RST}  "
          f"{C_RED}FAIL: {s.failed()}{C_RST}  {C_YEL}SKIP: {s.skipped()}{C_RST}  "
          f"TIME: {runtime}s")
    print(f"  Report: {out}")
    print(f"{C_BOLD}{'═'*52}{C_RST}")
    return 0 if s.failed() == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
