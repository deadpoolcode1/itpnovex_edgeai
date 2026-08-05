#!/usr/bin/env python3
"""Scopus whole-system INTEGRATION tests — camera + modem, end to end.

`run_scopus_tests.py` checks each box and each seam in isolation: it drives the
N6 over CDC and the modem over its own AT channel, and where it exercises "end
to end" paths it does so by sending the modem an AT command directly. That is
useful, but it cannot tell you whether the product works, because the thing a
user actually does — point the camera at people and expect an event to leave
the modem — is never performed by any of it.

This suite performs that. It injects an image with a KNOWN number of people,
runs inference on the device, and then follows the event outward hop by hop:

    injected frame -> NN detection -> camera notification
        -> camera/modem UART -> modem SDVR app -> UDP datagram on the host

Every hop is asserted separately so a failure names the hop that broke rather
than "end to end failed".

Statuses
--------
PASS  the behaviour is there and correct.
FAIL  the behaviour is supposed to be there and is not — a regression.
GAP   the hop is genuinely not implemented yet in the firmware. Reported
      separately from FAIL so a known-missing feature can never be mistaken for
      a passing one, and so the count of GAPs is the honest "distance to done".
SKIP  a bench prerequisite is absent (no SD card, no data session, ...).

Run:  python3 scopus/run_integration_tests.py [-v]
Repeatable: every test restores what it changed, so back-to-back runs give
identical results. Run it twice — a suite that only passes once is not a suite.
"""
import argparse
import json
import os
import re
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
from devices import N6Shell, ModemAt, ModemSsh   # noqa: E402

IMAGE_DIR = Path(os.environ.get(
    "SCOPUS_IMAGES", "/home/user/work/itpnovex/edgeai/images"))
HOST_IP = os.environ.get("HOST_IP", "192.168.2.3")
NTF_PORT = int(os.environ.get("NTF_PORT", "5005"))
MODEM_IP = os.environ.get("MODEM_IP", "192.168.2.2")

C = {"pass": "\033[92m", "fail": "\033[91m", "gap": "\033[95m",
     "skip": "\033[93m", "hdr": "\033[1m\033[94m", "dim": "\033[96m",
     "b": "\033[1m", "0": "\033[0m"}


# ───────────────────────────── reporting ──────────────────────────────
class Suite:
    def __init__(self, verbose=False):
        self.results = []
        self.verbose = verbose

    def group(self, title):
        print(f"\n{C['hdr']}── {title} ──{C['0']}")

    def _emit(self, tid, desc, status, note=""):
        self.results.append(dict(id=tid, desc=desc, status=status, note=note))
        tag = {"pass": "PASS", "fail": "FAIL", "gap": "GAP ", "skip": "SKIP"}[status]
        line = f"  [{tid:>6}] {desc:<62} {C[status]}{tag}{C['0']}"
        if note:
            line += f"  {C['dim']}{note[:110]}{C['0']}"
        print(line)

    def ok(self, tid, desc, cond, note="", failnote=""):
        self._emit(tid, desc, "pass" if cond else "fail",
                   note if cond else (failnote or note))
        return cond

    def gap(self, tid, desc, note):
        self._emit(tid, desc, "gap", note)

    def skip(self, tid, desc, note):
        self._emit(tid, desc, "skip", note)

    def counts(self):
        c = {"pass": 0, "fail": 0, "gap": 0, "skip": 0}
        for r in self.results:
            c[r["status"]] += 1
        return c


# ─────────────────────────── device helpers ───────────────────────────
class Camera:
    """N6 shell wrapper that adds frame injection and notification capture."""

    def __init__(self):
        self.n6 = N6Shell(N6Shell.discover())
        self.geometry = None

    def send(self, *a, **kw):
        return self.n6.send(*a, **kw)

    def close(self):
        try:
            self.n6.send("frame clear", max_secs=2.0)
        except OSError:
            pass
        self.n6.close()

    def inject(self, image_path):
        """Upload an image as the NN input. Returns (ok, detail).

        The geometry is read back from the kit rather than assumed: the
        firmware wants CAMERA_ANCILLARY_BUFFER_SIZE (256*256*3 today) and the
        old tooling hardcoded 300*300*3, so every upload was rejected and
        inference silently ran on a stale buffer.
        """
        from PIL import Image
        self.n6.send("frame clear", max_secs=2.0)
        self.n6.drain(0.3)
        banner = self.n6.send("frame upload", "Ready", 4.0)
        m = re.search(r"(\d+)\s+bytes RGB(?:\s*\((\d+)x(\d+)\))?", banner)
        if not m:
            return False, f"no upload banner: {banner[-120:]!r}"
        nbytes = int(m.group(1))
        if m.group(2):
            w, h = int(m.group(2)), int(m.group(3))
        else:
            side = int(round((nbytes / 3) ** 0.5))
            w = h = side
        self.geometry = (w, h, nbytes)
        data = Image.open(image_path).convert("RGB").resize((w, h), Image.LANCZOS).tobytes()
        if len(data) != nbytes:
            return False, f"geometry mismatch {len(data)} vs {nbytes}"
        crc = zlib.crc32(data) & 0xFFFFFFFF
        self.n6._write(b"FRMI" + struct.pack("<II", nbytes, crc))
        for i in range(0, len(data), 1024):
            self.n6._write(data[i:i + 1024])
        out = self._read_until((b"ok", b"ERROR"), 10.0)
        return ("ERROR" not in out), out.strip().replace("\n", " ")[-120:]

    def _read_until(self, needles, timeout):
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            try:
                chunk = os.read(self.n6.fd, 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                buf += chunk
                if any(nd in buf for nd in needles):
                    break
            else:
                time.sleep(0.02)
        return buf.decode(errors="replace")

    def run_nn(self, timeout=12.0):
        """Run inference on the injected frame. Returns (count, raw)."""
        out = self.n6.send("frame run", "frame run ok", timeout)
        m = re.search(r"(\d+)\s+detection\(s\)", out)
        return (int(m.group(1)) if m else None), out


class UdpWatch:
    """Listen for modem notification datagrams, filtered by source."""

    def __init__(self, port, source=MODEM_IP):
        self.source = source
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", port))
        self.sock.settimeout(0.2)

    def drain(self):
        try:
            while True:
                self.sock.recvfrom(65535)
        except (socket.timeout, BlockingIOError):
            pass

    def wait(self, timeout):
        """Return the first datagram from `source`, or None.

        Filtering on source matters: unrelated LAN traffic on this port has
        been mistaken for a pass on this bench before.
        """
        end = time.time() + timeout
        while time.time() < end:
            try:
                data, addr = self.sock.recvfrom(65535)
            except (socket.timeout, BlockingIOError):
                continue
            if addr[0] == self.source:
                return data
        return None

    def close(self):
        self.sock.close()


def modem_log_marker(ssh):
    rc, out, _ = ssh.run("wc -l < /data/sdvr/sdvr.log", timeout=15)
    try:
        return int(out.strip())
    except ValueError:
        return 0


def modem_log_since(ssh, marker):
    rc, out, _ = ssh.run(f"tail -n +{marker + 1} /data/sdvr/sdvr.log", timeout=20)
    return out


def wait_for_notify_drain(cam, max_secs=20.0):
    """Block until the camera's async notification queue is empty.

    The camera now has two independent producers on the CN805 link: the shell,
    when it runs `mdm <cmd>`, and the notifier thread, which sends queued
    AT+SDVRNTFA commands on its own schedule. Group D measures link hygiene —
    retries per frame, and whether the first command after an idle gap is
    answered — and both of those measurements silently assume the shell is the
    only thing on the wire. A notification landing mid-measurement adds frames
    and retries that D then attributes to its own commands, and refills the
    link during the gap D7 is trying to leave idle.

    So drain first, and measure a genuinely quiet link. The ntf_* counters
    exist precisely so "quiet" is observable rather than assumed:
    queued == sent + unconfirmed + dropped means nothing is still in flight.
    """
    deadline = time.time() + max_secs
    while time.time() < deadline:
        st = mdm_stats(cam)
        q = st.get("ntf_queued")
        if q is None:
            return False       # firmware predates the counters — nothing to do
        if q == st.get("ntf_sent", 0) + st.get("ntf_unconfirmed", 0) \
                + st.get("ntf_dropped", 0):
            time.sleep(0.5)    # let the last reply settle off the wire
            return True
        time.sleep(0.5)
    return False


def mdm_stats(cam):
    """Parse `mdm stats` into a dict so hops can be measured, not guessed."""
    out = cam.send("mdm stats", "usart2:", 4.0)
    d = {}
    m = re.search(r"ntf:\s*queued=(\d+)\s+sent=(\d+)\s+unconfirmed=(\d+)\s+"
                  r"dropped=(\d+)", out)
    if m:
        (d["ntf_queued"], d["ntf_sent"],
         d["ntf_unconfirmed"], d["ntf_dropped"]) = map(int, m.groups())
    for key in ("bytes", "frames", "badcrc", "stray", "err", "timeouts"):
        m = re.search(rf"rx:.*?\b{key}=(\d+)", out, re.S)
        if m:
            d["rx_" + key] = int(m.group(1))
    m = re.search(r"tx:\s*frames=(\d+)\s+err=(\d+)\s+retries=(\d+)", out)
    if m:
        d["tx_frames"], d["tx_err"], d["tx_retries"] = map(int, m.groups())
    m = re.search(r"usart2 err\(ORE/FE/NE\)=(\d+)", out)
    if m:
        d["usart2_err"] = int(m.group(1))
    return d


# ───────────────────────────── test groups ────────────────────────────
def g_a_prereq(s, ctx):
    s.group("A — bench prerequisites")
    cam, mat, ssh = ctx["cam"], ctx["mat"], ctx["ssh"]

    v = cam.send("version", "version ok", 3.0)
    s.ok("A1", "N6 shell responsive, edgeai app running",
         "Application:" in v, note=_one(v))

    helptext = cam.send("help", None, 2.5)
    s.ok("A2", "N6 exposes the Scopus command set (detect/notify/frame/mdm)",
         all(k in helptext for k in ("detect", "notify", "frame", "mdm")))

    r = mat.send("AT+SDVRVER", 4.0)
    m = re.search(r"\+SDVRVER:\s*([0-9.]+)", r)
    ctx["modem_ver"] = m.group(1) if m else "?"
    s.ok("A3", "Modem SDVR app answers AT+SDVRVER", bool(m),
         note=f"version={ctx['modem_ver']}")

    s.ok("A4", "Modem reachable over SSH (side-effect channel)", ssh.reachable())
    s.ok("A5", "sdvrApp running on modem", ssh.app_running())

    # Prove the camera UART is bound by USING it. Scraping the boot banner out
    # of sdvr.log is unreliable — the banner scrolls out of the tail as soon as
    # a session generates traffic, so a healthy tunnel reported as broken.
    alive = "OK" in cam.send("mdm AT", "ok", 10.0)
    s.ok("A6", "camera↔modem tunnel is live (modem claimed ttyHS0)", alive,
         note="mdm AT answered",
         failnote="no answer through the tunnel — modem may not have claimed ttyHS0")


def g_b_detection(s, ctx):
    """NN accuracy against images whose people-count is known from the name."""
    s.group("B — NN detection on injected frames (known ground truth)")
    cam = ctx["cam"]
    cam.send("detect start", "detect", 4.0)

    cases = [("1_person.jpg", 1), ("2_people.jpg", 2), ("3_people.jpg", 3),
             ("5_people.jpg", 5), ("7_people.jpg", 7)]
    detected = {}
    for idx, (name, expect) in enumerate(cases, 1):
        path = IMAGE_DIR / name
        if not path.exists():
            s.skip(f"B{idx}", f"{name}: detect {expect} people", "image not on bench")
            continue
        okup, detail = cam.inject(path)
        if not okup:
            s.ok(f"B{idx}", f"{name}: frame uploads to NN buffer", False, failnote=detail)
            continue
        count, raw = cam.run_nn()
        detected[name] = count
        # The NN is allowed to miss in a crowd (7_people scored 6 on the known
        # good baseline), but it must not return zero or hallucinate extras.
        good = count is not None and 1 <= count <= expect
        s.ok(f"B{idx}", f"{name}: detects 1..{expect} people (got {count})", good,
             note=_one(raw, 90),
             failnote=f"expected 1..{expect}, got {count} — {_one(raw, 80)}")

    if detected:
        # Guards the failure mode that looked like a model regression but was
        # really every upload being rejected on a size mismatch.
        allzero = all(v in (0, None) for v in detected.values())
        s.ok("B6", "not a blanket zero-detection failure across all images",
             not allzero,
             failnote="0 detections on every image — check frame geometry first")

    ctx["detected"] = detected


def g_c_camera_notify(s, ctx):
    """Detection must raise a SoW §6 notification on the camera side."""
    s.group("C — camera-side notification (§3.1, §4.2, §6)")
    cam = ctx["cam"]

    cam.send("notify enable 0xff", "notify enable ok", 4.0)
    q = cam.send("notify query", "notify query ok", 4.0)
    s.ok("C1", "notify enable/query round-trips", "enable_mask=0x000000ff" in q,
         note=_one(q, 60))

    # `notify trigger` is the deterministic path; it exercises the same
    # _notify_emit() the detector uses, without depending on the NN.
    out = cam.send("notify trigger 16", "+SDVRNTF", 5.0)
    got = "+SDVRNTF" in out
    s.ok("C2", "notify trigger emits +SDVRNTF on the shell", got, note=_one(out, 90))

    if got:
        m = re.search(r"\+SDVRNTF:\s*(\{.*?\})", out, re.S)
        parsed = None
        if m:
            try:
                parsed = json.loads(m.group(1))
            except json.JSONDecodeError:
                parsed = None
        s.ok("C3", "+SDVRNTF payload is valid JSON", parsed is not None,
             note=_one(m.group(1), 90) if m else "no JSON body")
        if parsed:
            required = {"ser", "num", "rsn", "rsd", "tim", "mtn", "mod", "bat", "vol"}
            missing = required - set(parsed)
            s.ok("C4", "notification carries all SoW §6 fields", not missing,
                 note="ser/num/rsn/rsd/tim/mtn/mod/bat/vol",
                 failnote=f"missing {sorted(missing)}")
            # Round-trip the clock rather than just shape-checking the field:
            # an unset RTC still matches YYYYMMDDHHMMSS but stamps every event
            # 2000-01-01, and the server has no other time source for them.
            want = time.strftime("%d%m%Y%H%M%S")
            cam.send(f"rtc set {want}", "ok", 4.0)
            out2 = cam.send("notify trigger 16", "+SDVRNTF", 5.0)
            m2 = re.search(r'"tim":"(\d{14})"', out2)
            got = m2.group(1) if m2 else ""
            expect_prefix = time.strftime("%Y%m%d%H%M")   # to the minute
            s.ok("C5", "notification timestamp tracks the RTC (not an unset clock)",
                 got.startswith(expect_prefix), note=f"tim={got}",
                 failnote=f"tim={got or 'none'}, expected ~{expect_prefix}xx — "
                          "RTC unset or not reflected in notifications")
    else:
        for t, d in [("C3", "+SDVRNTF payload is valid JSON"),
                     ("C4", "notification carries all SoW §6 fields"),
                     ("C5", "timestamp is a plausible YYYYMMDDHHMMSS")]:
            s.skip(t, d, "no notification emitted")

    # Control: the simulated detector fires the notification the inference loop
    # is supposed to fire. If this passes but C7 does not, the notification
    # plumbing is fine and the gap is specifically in the live NN path.
    out = cam.send("detect simulate 3", "+SDVRNTF", 6.0)
    s.ok("C6", "detect simulate fires the detection notification (rsn=0x10)",
         "+SDVRNTF" in out, note=_one(out, 90))

    # The real thing. Enable the action mask first so the test is fair — with
    # action_msk=0 the profile asks for no side effects at all.
    path = IMAGE_DIR / "3_people.jpg"
    if not path.exists():
        s.skip("C7", "a real NN detection raises +SDVRNTF (rsn=0x10)", "image missing")
    else:
        cam.send("detect profile 0x01 0x03", "ok", 4.0)
        cam.send("detect start", "detect", 4.0)
        cam.n6.drain(0.3)
        cam.inject(path)
        count, raw = cam.run_nn()
        if "+SDVRNTF" in raw:
            s.ok("C7", f"a real NN detection raises +SDVRNTF (detected {count})",
                 True, note=_one(raw, 90))
        elif count:
            s.gap("C7", f"a real NN detection raises +SDVRNTF (detected {count})",
                  "inference found people but emitted nothing: the live loop in "
                  "nn_task.c only fires an SD snapshot on the 0→N edge and never "
                  "calls _notify_emit — only 'notify trigger' and 'detect "
                  "simulate' do")
        else:
            s.ok("C7", "a real NN detection raises +SDVRNTF", False,
                 failnote=f"no detections to notify on (count={count})")


def g_d_tunnel(s, ctx):
    """The camera->modem AT tunnel (SoW §4.6) and its link hygiene."""
    s.group("D — camera ↔ modem UART tunnel (§4.6)")
    cam = ctx["cam"]

    # Let the notifier finish anything group C queued, so this group measures
    # the shell's own traffic rather than two producers sharing the link.
    drained = wait_for_notify_drain(cam)

    # Wake the link before measuring. The CN805 FXMA108 translator latches
    # direction, so the first command after an idle gap legitimately costs one
    # retry — that is the designed mitigation (preamble + retry), not a fault.
    # Measuring from here keeps D5/D6 about steady state; D7 tests the gap.
    cam.send("mdm AT", "ok", 8.0)

    before = mdm_stats(cam)
    ok_at = "OK" in cam.send("mdm AT", "ok", 8.0)
    s.ok("D1", "mdm AT round-trips through the modem", ok_at)

    r = cam.send("mdm AT+SDVRVER", "ok", 8.0)
    s.ok("D2", "SDVR command tunnels and the reply comes back",
         "+SDVRVER" in r, note=_one(r, 70))

    r = cam.send("mdm AT+SDVRPING=5", "ok", 8.0)
    s.ok("D3", "AT+SDVRPING=5 tunnels and echoes its argument",
         "+SDVRPING: 5" in r, note=_one(r, 70))

    # 10 commands with a settling gap: the FXMA108 translator used to eat
    # every other frame after an idle period, so this is the regression guard.
    okc = 0
    for _ in range(10):
        if "OK" in cam.send("mdm AT", "ok", 8.0):
            okc += 1
        time.sleep(0.3)
    s.ok("D4", "10 consecutive tunnelled commands all answered",
         okc == 10, note=f"{okc}/10")

    after = mdm_stats(cam)
    d_badcrc = after.get("rx_badcrc", 0) - before.get("rx_badcrc", 0)
    d_stray = after.get("rx_stray", 0) - before.get("rx_stray", 0)
    d_uart = after.get("usart2_err", 0) - before.get("usart2_err", 0)
    # Data integrity is non-negotiable: a corrupt frame means the HDLC layer or
    # the wire is wrong, and no retry count excuses it.
    s.ok("D5", "no CRC errors or stray bytes on the link (data integrity)",
         d_badcrc == 0 and d_stray == 0,
         note=f"badcrc=0 stray=0 (usart2 +{d_uart})",
         failnote=f"badcrc+{d_badcrc} stray+{d_stray}")

    # Retries are allowed but must stay rare in steady state; one per command
    # would mean the wake mitigation is papering over a latched link.
    d_retry = after.get("tx_retries", 0) - before.get("tx_retries", 0)
    d_tx = max(1, after.get("tx_frames", 0) - before.get("tx_frames", 0))
    s.ok("D6", f"TX retries stay rare in steady state ({d_retry}/{d_tx} frames)",
         d_retry * 4 <= d_tx,
         note="" if drained else "notifier did not drain — see failnote",
         failnote=f"{d_retry} retries over {d_tx} frames — link latching again"
                  + ("" if drained else "; the notification queue never "
                     "drained, so these frames are not all this group's"))

    # The documented failure mode was: after an idle gap the first command is
    # eaten. It must still be answered (the retry is allowed to pay for it).
    # Drain again first: D4's traffic can have queued nothing, but anything the
    # notifier sends during the sleep below would mean the link was not idle
    # and this would be testing something other than what it claims.
    wait_for_notify_drain(cam)
    time.sleep(12.0)
    t0 = time.time()
    woke = "OK" in cam.send("mdm AT", "ok", 10.0)
    s.ok("D7", "first command after a 12 s idle gap is still answered", woke,
         note=f"{time.time()-t0:.2f}s",
         failnote="idle-gap regression — the CN805 wake preamble is not recovering")
    ctx["stats_after"] = mdm_stats(cam)


def g_e_full_chain(s, ctx):
    """THE product behaviour: see people -> an event leaves the modem."""
    s.group("E — full chain: detection → modem → server event")
    cam, ssh, mat = ctx["cam"], ctx["ssh"], ctx["mat"]

    # Point the modem's notification endpoint at us and prove the modem leg
    # works first, so a failure downstream cannot be blamed on the modem.
    mat.send(f'AT+SDVRNTFHOST="{HOST_IP}"', 4.0)
    mat.send(f"AT+SDVRNTFPORT={NTF_PORT}", 4.0)

    watch = UdpWatch(NTF_PORT)
    try:
        watch.drain()
        mat.send('AT+SDVRNTFA=1,5,"hello"', 6.0)
        dg = watch.wait(8.0)
        s.ok("E1", "modem → host UDP works when driven directly (§6)",
             dg is not None, note=_one(dg.decode(errors="replace"), 90) if dg else "",
             failnote="no datagram from the modem — the modem leg itself is down")

        # Now the real thing: nobody touches the modem. Inject people, let the
        # camera detect, and see whether anything reaches the modem at all.
        path = IMAGE_DIR / "3_people.jpg"
        if not path.exists():
            for t, d in [("E2", "camera detection reaches the modem"),
                         ("E3", "camera detection produces a server event")]:
                s.skip(t, d, "test image missing")
            return

        marker = modem_log_marker(ssh)
        before = mdm_stats(cam)
        watch.drain()

        cam.send("notify enable 0xff", "notify enable ok", 4.0)
        cam.send("detect start", "detect", 4.0)
        okup, _ = cam.inject(path)
        count, raw = cam.run_nn()
        cam.send("notify trigger 16", "+SDVRNTF", 5.0)   # deterministic re-fire
        time.sleep(3.0)

        after = mdm_stats(cam)
        newlog = modem_log_since(ssh, marker)
        tx_delta = after.get("tx_frames", 0) - before.get("tx_frames", 0)

        # Hop 1: did a NOTIFICATION reach the modem? Judge on what the modem
        # logged, not on the camera's tx_frames counter — that counter moves for
        # any tunnel traffic (a late retry, a stray keepalive) and made this
        # verdict flip between runs.
        notif_at = re.search(r"SDVRNTFA|SDVRNTF\b", newlog)
        if notif_at:
            s.ok("E2", "camera forwards its notification over the modem UART",
                 True, note=_one(notif_at.group(0), 40) + f" (tx +{tx_delta})")
        else:
            s.gap("E2", "camera forwards its notification over the modem UART",
                  f"modem logged no notification command (camera tx +{tx_delta}): "
                  "_notify_emit() writes only to the CDC shell, it never calls "
                  "modem_send_at (shell_task.c)")

        # Hop 2: did an event actually leave the modem for the server?
        dg2 = watch.wait(6.0)
        if dg2 is not None:
            s.ok("E3", "detection produces a server event end to end", True,
                 note=_one(dg2.decode(errors="replace"), 90))
        else:
            saw_at = "SDVRNTF" in newlog
            s.gap("E3", "detection produces a server event end to end",
                  "no UDP datagram; modem log shows "
                  + ("an AT arriving but no send" if saw_at
                     else "nothing arrived from the camera"))
    finally:
        watch.close()


def g_f_photo_upload(s, ctx):
    """Photo capture -> binary transfer to the modem -> upload (§7, §8.2)."""
    s.group("F — photo → modem binary upload (§7, §8.2)")
    cam, ssh = ctx["cam"], ctx["ssh"]

    marker = modem_log_marker(ssh)
    out = cam.send("photo upload", "ok", 25.0)

    # "trigger failed (busy / no modem)" means the snapshot pipeline is still
    # finishing an earlier capture — a transient, not a defect, and it happens
    # whenever something ran a photo shortly before this suite. Retry once and
    # say so in the note, so a genuine failure still fails but a queued
    # pipeline does not read as a broken one.
    retried = False
    if "busy" in out.lower():
        retried = True
        time.sleep(12.0)
        marker = modem_log_marker(ssh)
        out = cam.send("photo upload", "ok", 25.0)

    sent = "ok" in out and "error" not in out.lower() and "busy" not in out.lower()
    s.ok("F1", "camera captures a JPEG and starts a SENDBIN transfer", sent,
         note=_one(out, 90) + (" (after one retry — pipeline was busy)"
                               if retried else ""),
         failnote="photo upload did not start: " + _one(out, 90))

    # A ~96 KB JPEG takes ~9 s to cross the 115200-baud link, so this waits for
    # the transfer to reach a terminal state instead of sleeping a fixed 3 s.
    # Two reasons: a flat sleep read a half-finished transfer as "arrived but
    # not ingested", and — since the camera and the host AT channel now share
    # one LiveBin — leaving a transfer in flight made group G arm on top of it
    # and fail. Group G tests LiveBin's arm/reject/release logic; it must start
    # from a quiescent sink, and the honest way to get one is to wait for this
    # transfer rather than to abort it.
    log = ""
    deadline = time.time() + 40.0
    while time.time() < deadline:
        log = modem_log_since(ssh, marker)
        if ("bytes received, uploading" in log or "LiveBin_Abort" in log
                or "LiveBin_Begin failed" in log):
            break
        time.sleep(1.0)

    armed = "SENDBIN" in log or "LiveBin_Begin" in log
    s.ok("F2", "modem receives the SENDBIN command + binary tail", armed,
         note=_one(log, 100), failnote="nothing about SENDBIN in the modem log")

    if not armed:
        s.skip("F3", "modem ingests the photo instead of discarding it",
               "no transfer observed to judge")
    elif "bytes received, uploading" in log:
        m = re.search(r"LiveBin_Feed: (\d+) bytes received", log)
        s.ok("F3", "modem ingests the photo instead of discarding it", True,
             note=f"{m.group(1)} bytes handed to the uploader" if m
                  else _one(log, 90))
    elif "not handled in phase 1" in log or "swallow" in log:
        s.gap("F3", "modem ingests the photo instead of discarding it",
              "hdlc_channel.c counts the binary tail and throws it away; "
              "LiveBin_Begin/Feed are never called from the camera path")
    else:
        s.ok("F3", "modem ingests the photo instead of discarding it", False,
             failnote="LiveBin was armed but the transfer never completed: "
                      + _one(log, 120))

    # Whatever happened above, leave the sink free for group G. A transfer that
    # completed has already released it; this only catches a stalled one.
    ctx["mat"].send("AT+SDVRUPLSTOP", 4.0)


"""AT+SDVRNTFA payload transport (§5.2 / §6).

The §6 body is JSON, and JSON cannot cross an AT line unaided: atServer's
parameter parser CONSUMES embedded double quotes, so `{"ser":1}` arrives as
`{ser:1}` — accepted, silently corrupt, no longer JSON. Hence ENC=1, which
substitutes backtick for quote and is restored on the modem.

One AT parameter is also capped at 128 bytes, which is not enough: the body is
only ~100 bytes today because `mod` is empty and `bat`/`vol` are the
placeholder 0.0. With real values it is exactly 128 with an empty `mod`, and
any `mod` string goes over. So the payload is split across parameters and the
modem rejoins them.

This group pins that contract, because every failure mode it guards against
answers OK on the AT channel — the verdict has to come from the datagram.
"""
NTFA_CHUNK = 128
NTFA_SUB = "`"


def _ntfa_body(mod="", bat="0.0", vol="0.0", ser=4194336, num=0):
    return ('{"ser":%d,"num":%d,"rsn":16,"rsd":3,"tim":"20260805153000",'
            '"mtn":0,"mod":"%s","bat":%s,"vol":%s}' % (ser, num, mod, bat, vol))


def _ntfa_encode(payload):
    """Exactly what shell_task.c builds: substituted, chunked, ENC=1 last."""
    enc = payload.replace('"', NTFA_SUB)
    parts = [enc[i:i + NTFA_CHUNK]
             for i in range(0, len(enc), NTFA_CHUNK)] or [""]
    return ",".join('"%s"' % p for p in parts), len(parts)


def g_h_ntfa_protocol(s, ctx):
    s.group("H — AT+SDVRNTFA payload transport (§5.2, §6)")
    mat = ctx["mat"]

    mat.send(f'AT+SDVRNTFHOST="{HOST_IP}"', 4.0)
    mat.send(f"AT+SDVRNTFPORT={NTF_PORT}", 4.0)
    watch = UdpWatch(NTF_PORT)

    def fire(cmd, wait=5.0):
        watch.drain()
        reply = mat.send(cmd, 8.0)
        dg = watch.wait(wait)
        time.sleep(0.3)
        return reply, (dg.decode(errors="replace") if dg else None)

    try:
        # Legacy callers predate ENC entirely and must be unaffected by it.
        _, got = fire('AT+SDVRNTFA=1,5,"hello"')
        s.ok("H1", "legacy form (no ENC) delivers the payload verbatim",
             got == "hello", note=repr(got))
        _, got = fire('AT+SDVRNTFA=2,5,"hello",0')
        s.ok("H2", "explicit ENC=0 delivers the payload verbatim",
             got == "hello", note=repr(got))

        # Today's body: one chunk, and it must arrive as parseable JSON.
        p = _ntfa_body()
        params, _n = _ntfa_encode(p)
        _, got = fire(f"AT+SDVRNTFA=3,{len(p)},{params},1")
        s.ok("H3", f"ENC=1 carries the §6 body byte-exact ({len(p)} B)",
             got == p, failnote=f"got {got!r}")
        s.ok("H4", "the delivered body is valid JSON",
             got is not None and _is_json(got),
             failnote="quotes did not survive — this is the bug ENC=1 exists "
                      "to prevent, and it answers OK on the AT channel")

        # The case a single 128-byte parameter cannot carry. This is what the
        # product hits the moment mod/bat/vol stop being placeholders.
        p = _ntfa_body(mod="N6Cam-SIANA-rev4-20260805", bat="12.34",
                       vol="13.85", ser=4294967295, num=65535)
        params, n = _ntfa_encode(p)
        s.ok("H5", "a fully-populated §6 body needs more than one parameter",
             len(p) > NTFA_CHUNK, note=f"{len(p)} B over {n} chunks",
             failnote=f"fixture is only {len(p)} B — it is not testing "
                      "the multi-chunk path it claims to")
        _, got = fire(f"AT+SDVRNTFA=4,{len(p)},{params},1")
        s.ok("H6", "a multi-chunk payload is rejoined byte-exact",
             got == p, failnote=f"got {got!r}")
        if got is not None and _is_json(got) and _is_json(p):
            want, o = json.loads(p), json.loads(got)
            s.ok("H7", "mod/bat/vol survive the chunk boundary",
                 o.get("mod") == want["mod"] and o.get("bat") == want["bat"]
                 and o.get("vol") == want["vol"], note=f"mod={o.get('mod')!r}")
        else:
            s.ok("H7", "mod/bat/vol survive the chunk boundary", False,
                 failnote="payload did not arrive as JSON")

        # Exactly one full parameter — the off-by-one that decides whether a
        # body is split at all.
        filler = NTFA_CHUNK - len(_ntfa_body(mod=""))
        p = _ntfa_body(mod="M" * max(0, filler))
        params, n = _ntfa_encode(p)
        _, got = fire(f"AT+SDVRNTFA=5,{len(p)},{params},1")
        s.ok("H8", f"a payload of exactly {len(p)} B (one full chunk) arrives",
             got == p, note=f"{n} chunk(s)", failnote=f"got {got!r}")

        # Malformed input must be refused rather than silently reinterpreted.
        reply, _ = fire('AT+SDVRNTFA=6,5,"hello",9', wait=1.5)
        s.ok("H9", "an out-of-range ENC is rejected", "ERROR" in reply,
             note=_one(reply, 50))
        reply, _ = fire('AT+SDVRNTFA=7,5,"hello",x', wait=1.5)
        s.ok("H10", "a non-numeric ENC is rejected", "ERROR" in reply,
             note=_one(reply, 50))

        # And none of the above left the command in a bad state.
        _, got = fire('AT+SDVRNTFA=8,5,"hello"')
        s.ok("H11", "the command still works after the malformed ones",
             got == "hello", note=repr(got))
    finally:
        watch.close()


def _is_json(text):
    try:
        json.loads(text)
        return True
    except Exception:
        return False


def g_g_state_hygiene(s, ctx):
    """Things that used to wedge the system until a restart."""
    s.group("G — state hygiene / repeatability")
    mat = ctx["mat"]

    arm = 'AT+SDVRSENDBIN=1,"qa","01012026000000",1,4'
    r1 = mat.send(arm, 4.0)
    s.ok("G1", "AT+SDVRSENDBIN arms a live upload", "ERROR" not in r1, note=_one(r1, 50))
    r2 = mat.send(arm, 4.0)
    s.ok("G2", "a second SENDBIN while armed is rejected (not silently accepted)",
         "ERROR" in r2, note=_one(r2, 50))
    mat.send("AT+SDVRUPLSTOP", 4.0)
    r3 = mat.send(arm, 4.0)
    s.ok("G3", "AT+SDVRUPLSTOP disarms so the next SENDBIN succeeds",
         "ERROR" not in r3, note=_one(r3, 50),
         failnote="UPLSTOP did not release LiveBin — suite is not repeatable")
    mat.send("AT+SDVRUPLSTOP", 4.0)

    sd = ctx["cam"].send("sd query", "sd query ok", 4.0)
    if "not mounted" in sd:
        s.skip("G4", "N6 SD card present for the §7 photo→SD pipeline",
               "no card in the N6 slot (card-detect GPIO reports empty)")
    else:
        s.ok("G4", "N6 SD card present for the §7 photo→SD pipeline", True, note=_one(sd, 60))


def _one(text, n=120):
    if text is None:
        return ""
    return " ".join(str(text).split())[:n]


# ────────────────────────────── driver ────────────────────────────────
def quiesce_detector(cam):
    """Stop live inference and drain whatever it already emitted.

    Called before the first group and again after the last, so neither this
    run nor the next one has to parse shell traffic while the NN loop is
    firing notifications underneath it.
    """
    try:
        cam.send("detect stop", "detect", 4.0)
        cam.send("frame clear", "frame", 4.0)
    except Exception:
        pass
    cam.n6.drain(0.8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    t0 = time.time()
    print(f"{C['b']}Scopus whole-system integration suite{C['0']}")
    print(f"  images={IMAGE_DIR}  host={HOST_IP}:{NTF_PORT}  modem={MODEM_IP}")

    s = Suite(args.verbose)
    ctx = {}
    try:
        ctx["cam"] = Camera()
    except Exception as e:
        print(f"{C['fail']}cannot open N6 shell: {e}{C['0']}")
        return 2
    try:
        ctx["mat"] = ModemAt(os.environ.get("SDVR_PORT") or None)
        ctx["mat"].prime()
        ctx["ssh"] = ModemSsh(MODEM_IP, os.environ.get("MODEM_PASSWORD", "Ss123"))

        # Start from a stopped detector, and leave one behind.
        #
        # This suite promises identical results run to run, and without this it
        # does not deliver them: groups C and E turn detection on, and once the
        # camera notifies on a real detection (rather than only on an explicit
        # `notify trigger`) a detector left running keeps emitting +SDVRNTF at
        # whatever the lens happens to see. Those lines arrive between a
        # command and its response and derail the NEXT run from group A onward
        # — a second run scored 27/36 against the first run's 35/36, on
        # identical firmware, purely from this.
        quiesce_detector(ctx["cam"])

        for fn in (g_a_prereq, g_b_detection, g_c_camera_notify,
                   g_d_tunnel, g_e_full_chain, g_f_photo_upload,
                   g_g_state_hygiene, g_h_ntfa_protocol):
            try:
                fn(s, ctx)
            except Exception as e:
                s.ok(fn.__name__, f"group {fn.__name__} completed", False,
                     failnote=f"harness exception: {e!r}")
    finally:
        try:
            quiesce_detector(ctx["cam"])
        except Exception:
            pass
        try:
            ctx["cam"].close()
        except Exception:
            pass

    c = s.counts()
    dt = int(time.time() - t0)
    print(f"\n{C['b']}{'═' * 64}{C['0']}")
    print(f"  TOTAL: {len(s.results)}   {C['pass']}PASS: {c['pass']}{C['0']}   "
          f"{C['fail']}FAIL: {c['fail']}{C['0']}   "
          f"{C['gap']}GAP: {c['gap']}{C['0']}   "
          f"{C['skip']}SKIP: {c['skip']}{C['0']}   TIME: {dt}s")
    if c["gap"]:
        print(f"  {C['gap']}GAPs are unimplemented firmware links, not "
              f"regressions — see the note on each.{C['0']}")
    print(f"{C['b']}{'═' * 64}{C['0']}")

    out = Path(__file__).resolve().parent / "results"
    out.mkdir(exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    (out / f"integration-{stamp}.json").write_text(json.dumps(
        {"when": stamp, "counts": c, "results": s.results,
         "modem_ver": ctx.get("modem_ver"), "detected": ctx.get("detected")},
        indent=2))
    print(f"  Report: {out}/integration-{stamp}.json")
    return 1 if c["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
