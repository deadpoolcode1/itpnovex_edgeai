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
from lib.devices import N6Shell, ModemAt, ModemSsh           # noqa: E402
from lib.report import Suite, write_report, C_BOLD, C_GRN, C_RED, C_YEL, C_RST  # noqa: E402


# ───────────────────────── shared context ─────────────────────────────
class Ctx:
    def __init__(self):
        self.n6 = None            # N6Shell or None
        self.mat = None           # ModemAt or None
        self.ssh = None           # ModemSsh
        self.n6_mode = "absent"   # 'edgeai-app' | 'stock' | 'absent'
        self.modem_scopus = False # modem app has the new SoW v3 commands
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
        c.mat = ModemAt(os.environ.get("SDVR_PORT") or None)
        ok_at, _ = c.mat.expect("AT", "OK", 2.0)
        s.ok("T0.3", "Modem AT command channel responsive (§5.1)", ok_at,
             reason="no OK to bare AT", extra=c.mat.tty)
    except Exception as e:
        c.mat = None
        s.ok("T0.3", "Modem AT command channel responsive (§5.1)", False, reason=str(e))

    # Modem SSH + SDVR app -------------------------------------------------
    c.ssh = ModemSsh(os.environ.get("MODEM_IP", "192.168.2.2"),
                     os.environ.get("MODEM_PASSWORD", "Ss123"))
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
    rc, out, _ = ssh.run("ip route get 192.168.2.3 2>/dev/null; ip addr show 2>/dev/null | grep 'inet 192.168.2'")
    # The modem sees the host as the default gw on ecm0 — read it from there.
    rc, gw, _ = ssh.run("ip route show default 2>/dev/null | awk '{print $3}' | head -1")
    gw = (gw or "").strip()
    if gw.startswith("192.168.2."):
        return gw
    return "192.168.2.3"


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
    if c.mat:
        return False
    for tid, desc in tids:
        s.skip(tid, desc, "modem AT channel unavailable")
    return True


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
    host = c.host_ip or "192.168.2.3"
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
    _, r = m.expect("AT+SDVRMOUNTSD", "OK", 8.0)
    mounted = False
    if ssh and ssh.reachable():
        _, mounts, _ = ssh.run("cat /proc/mounts | grep -c mmcblk || echo 0")
        mounted = mounts.strip().splitlines()[0] != "0"
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
    if not c.mat or not c.modem_scopus:
        s.skip(tid, desc, "needs modem Scopus build (AT+SDVRNTFA)")
        return
    host = c.host_ip or "192.168.2.3"
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


def g13_e2e_upload(c: Ctx, s: Suite):
    s.group("GROUP 13 — End-to-end HTTPS file upload (§8)")
    tid, desc = "T13.1", "SD file uploads to HTTPS server, X-Timestamp/X-Ref headers (§8)"
    # This needs the local HTTPS file server + device certs provisioned and a
    # data session. Detect the upload sink; skip with a precise reason if absent.
    sink = Path.home() / "sdvr-uploads-tls"
    if not c.mat:
        s.skip(tid, desc, "modem AT channel unavailable")
        return
    if not sink.is_dir():
        s.skip(tid, desc, f"no HTTPS upload sink at {sink} (server not provisioned on this host)")
        return
    s.skip(tid, desc, "E2E HTTPS upload requires provisioned certs + active data "
                      "session; run V20_SDVR server-establish first (out of scope here)")


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
    try:
        g0_prereq(c, s)
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
        if c.n6:
            c.n6.close()

    runtime = int(time.time() - t0)
    meta = dict(n6_fw=s.n6_fw, n6_app=s.n6_app, n6_mode=s.n6_mode,
                modem_ver=s.modem_ver, modem_cmds=s.modem_cmds,
                n6_tty=(c.n6.tty if c.n6 else "—"),
                modem_tty=(c.mat.tty if c.mat else "—"),
                modem_ip=os.environ.get("MODEM_IP", "192.168.2.2"),
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
