"""
Device-access layer for the Scopus whole-system suite.

Three endpoints, matching the SoW block diagram:
  - N6Shell  : the Main-CPU control channel — N6Cam CDC-ACM shell (SoW §4.1).
  - ModemAt  : the modem SDVR command channel — the host UART ttyHSL1, which
               sdvrApp claims through portService and which reaches this PC
               over the modem's FTDI adapter. NOT the Sierra module's own
               native AT port; see the note on the class.
  - ModemSsh : root shell on the modem for side-effect verification (mounts,
               /sdvr config tree, sdvr.log, logread).

Serial I/O is raw os.open + stty (no pyserial dependency) so the suite runs on
a bare remote with only python3.
"""
from __future__ import annotations

import glob
import os
import subprocess
import time
from typing import Optional, Tuple

from settings import S


# ───────────────────────── N6 Main-CPU shell ──────────────────────────
class N6Shell:
    """Open + drive the N6Cam CDC shell with non-blocking I/O."""

    BY_ID = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02"

    def __init__(self, tty: Optional[str] = None):
        self.tty = tty or self.discover()
        if not self.tty:
            raise RuntimeError("N6Cam CDC shell port not found")
        self._open()
        self.drain(0.3)

    @classmethod
    def discover(cls) -> Optional[str]:
        for link in glob.glob(cls.BY_ID):
            real = os.path.realpath(link)
            if os.path.exists(real):
                return real
        return "/dev/ttyACM1" if os.path.exists("/dev/ttyACM1") else None

    def _open(self):
        os.system(f"stty -F {self.tty} 115200 cs8 -cstopb -parenb raw -echo 2>/dev/null")
        self.fd = os.open(self.tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass

    def drain(self, secs=0.2):
        end = time.time() + secs
        while time.time() < end:
            try:
                os.read(self.fd, 4096)
            except BlockingIOError:
                time.sleep(0.02)

    def _write(self, data: bytes):
        n = 0
        while n < len(data):
            try:
                n += os.write(self.fd, data[n:])
            except BlockingIOError:
                time.sleep(0.005)

    @staticmethod
    def _after_echo(buf: bytes, line: str, sb: bytes) -> bytes:
        """The part of `buf` that can legitimately hold the response.

        The N6 shell echoes the command line back before answering, so a
        sentinel that is also a substring of the command itself — e.g.
        "SDVRPING" in "mdm AT+SDVRPING=5", or "sd" in "sd query" — matches the
        echo and returns before the real reply. Locally that usually still
        passes, because the reply lands in the same os.read() chunk as the
        echo; over the ~180 ms modem tunnel it never does, so T11.2 failed
        every run while working by hand. Match only past the echo.
        """
        eb = line.encode()
        i = buf.find(eb)
        if i >= 0:
            return buf[i + len(eb):]
        # Echo not seen yet. If the sentinel cannot occur in the command there
        # is nothing to confuse it with, so the whole buffer is fair game.
        return b"" if sb in eb else buf

    def send(self, line: str, sentinel: str = None, max_secs=3.0) -> str:
        """Send one shell line; read until `sentinel` appears or timeout."""
        self.drain(0.05)
        self._write(line.encode() + b"\n")
        end = time.time() + max_secs
        buf = b""
        sb = sentinel.encode() if sentinel else None
        while time.time() < end:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                buf += chunk
                if sb and sb in self._after_echo(buf, line, sb):
                    break
            else:
                time.sleep(0.02)
        return buf.decode(errors="replace")

    def alive(self, max_secs=2.0) -> bool:
        return "Uptime" in self.send("uptime", "Uptime", max_secs)


# ───────────────────────── modem SDVR AT channel ──────────────────────
class ModemAt:
    """Raw AT channel to the modem (and through the bridge, the SDVR app).

    Auto-discovers the port that answers `AT` with OK — and that is NOT
    sufficient to have found the right one.

    sdvrApp claims **ttyHSL1**, the WP76's host UART, via portService
    (`qmi_at_fwd.c`, le_port_Request("uart")); on this bench ttyHSL1 reaches
    the PC through the modem's FTDI adapter, normally /dev/ttyUSB0. That is
    the only wire on which AT+SDVR… commands exist.

    The Sierra module separately exposes **its own** AT parser on native USB
    (…-if03 → /dev/ttyUSB3). That port answers a bare `AT` with OK and answers
    every AT+SDVR… with ERROR. With the FTDI unplugged, discover() lands on it
    and everything looks alive while every SDVR command fails — which reads as
    a broken product rather than a missing cable. run_scopus_tests.py probes
    for exactly that mismatch (bare AT works, AT+SDVRPING does not) and skips
    the modem groups with the real reason instead of failing them.

    An earlier version of this docstring claimed the Sierra port was the SDVR
    channel on this bench. It is not, and believing it cost a session.

    **The camera tunnel is a real fallback.** Pass a live N6Shell as
    `via_camera` and, when the discovered port turns out to be the module's own
    AT parser, every AT+SDVR… is routed as `mdm <cmd>` over the CN805 link
    instead. That reaches the identical handler in the identical app — it is
    how `mdm AT+SDVRPING=7` has always worked — so a bench whose FTDI adapter
    is unplugged still exercises the whole SDVR command set. `is_sdvr_channel`
    says whether the wire itself was usable; `route` says which one is
    actually carrying the commands.
    """

    def __init__(self, tty: Optional[str] = None, via_camera=None):
        self.tty = tty or self.discover()
        if not self.tty:
            raise RuntimeError("Modem AT port not found")
        os.system(f"stty -F {self.tty} 115200 cs8 -cstopb -parenb raw -echo 2>/dev/null")
        self.prime()

        self._cam = via_camera
        self.is_sdvr_channel = self._probe_sdvr_channel()
        self.route = "direct" if self.is_sdvr_channel else (
            "camera-tunnel" if self._cam is not None else "none")

    def _probe_sdvr_channel(self) -> bool:
        """Does THIS port carry AT+SDVR…?

        Two probes that have to disagree: a bare AT must work — so the port is
        alive and this is not a serial fault — while a command present in every
        SDVR build since 1.0 must not. That combination has one cause, and it
        is the FTDI adapter being unplugged.
        """
        if "OK" not in self._raw_send("AT", 2.0):
            return False                       # not a working AT port at all
        return "+SDVRPING:" in self._raw_send("AT+SDVRPING=1", 3.0)

    def _tunnel(self, cmd: str, timeout: float) -> str:
        """Send one AT command over the camera's `mdm` tunnel."""
        return self._cam.send(f"mdm {cmd}", max_secs=max(timeout, 4.0)) or ""

    def prime(self):
        """The SDVR app's UartFilter opens the modem AT port lazily on the
        first byte, and ttyHSL1 is shared with the kernel console — so the
        first command(s) after an idle gap are often swallowed. Send a couple
        of throwaway ATs to wake the bridge before real traffic."""
        for _ in range(3):
            try:
                self.send("AT", timeout=1.5)
            except Exception:
                pass

    @staticmethod
    def _probe(tty: str) -> bool:
        try:
            os.system(f"stty -F {tty} 115200 cs8 -cstopb -parenb raw -echo 2>/dev/null")
            fd = os.open(tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError:
            return False
        try:
            os.write(fd, b"AT\r")
            end = time.time() + 1.0
            buf = b""
            while time.time() < end:
                try:
                    buf += os.read(fd, 256)
                except BlockingIOError:
                    time.sleep(0.02)
                if b"OK" in buf:
                    return True
            return False
        finally:
            os.close(fd)

    @classmethod
    def discover(cls) -> Optional[str]:
        """Pick the SDVR command channel.

        Architecturally this is the modem's host UART (/dev/ttyHSL1 on the
        modem), reached on the bench through the FTDI bridge — the same
        SDVR_HOST_UART the V20 harness uses, NOT the Sierra native AT port.
        The SDVR app's UartFilter dispatches AT+SDVR* there; the native AT
        port answers ERROR for them. Honour SDVR_PORT, then prefer the FTDI
        host UART, then fall back to any port that at least answers AT.
        """
        env = os.environ.get("SDVR_PORT")
        if env:
            return env
        # FTDI host UART — the SDVR command channel.
        for link in sorted(glob.glob("/dev/serial/by-id/*FTDI*")):
            real = os.path.realpath(link)
            if os.path.exists(real):
                return real
        if os.path.exists("/dev/ttyUSB0"):
            return "/dev/ttyUSB0"
        # Last resort: a port that answers bare AT (native AT port).
        for n in range(6):
            c = f"/dev/ttyUSB{n}"
            if os.path.exists(c) and cls._probe(c):
                return c
        return None

    def send(self, cmd: str, timeout=3.0) -> str:
        """Send an AT command, over whichever wire actually carries it.

        Anything that is not an AT+SDVR… command goes down the serial port
        regardless: a bare AT, or a module command like AT+CPIN?, is answered
        by the module's own parser and has no business in the tunnel.
        """
        if (not self.is_sdvr_channel and self._cam is not None
                and cmd.upper().startswith("AT+SDVR")):
            return self._tunnel(cmd, timeout)
        return self._raw_send(cmd, timeout)

    def _raw_send(self, cmd: str, timeout=3.0) -> str:
        """Send an AT command down the serial port; read until OK/ERROR."""
        fd = os.open(self.tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            try:
                os.read(fd, 8192)
            except BlockingIOError:
                pass
            os.write(fd, (cmd + "\r").encode())
            end = time.time() + timeout
            buf = b""
            term_at = 0.0
            while time.time() < end:
                try:
                    chunk = os.read(fd, 8192)
                except BlockingIOError:
                    chunk = b""
                if chunk:
                    buf += chunk
                    if not term_at and (b"\r\nOK\r\n" in buf or b"\r\nERROR\r\n" in buf
                                        or b"+SDVRERR" in buf):
                        term_at = time.time()
                else:
                    if term_at and time.time() - term_at > 0.15:
                        break
                    time.sleep(0.02)
            return buf.decode(errors="replace")
        finally:
            os.close(fd)

    def expect(self, cmd: str, needle: str, timeout=3.0) -> Tuple[bool, str]:
        r = self.send(cmd, timeout)
        # One retry if the shared/lazy bridge swallowed the command (no
        # terminator came back at all) — distinguishes "channel ate it" from
        # a genuine ERROR, which we keep.
        if needle not in r and "OK" not in r and "ERROR" not in r and "+SDVR" not in r:
            r = self.send(cmd, timeout)
        return (needle in r, r)


# ───────────────────────── modem SSH (side effects) ───────────────────
class ModemSsh:
    """sshpass-based root shell on the modem for side-effect checks."""

    OPTS = [
        "-o", "StrictHostKeyChecking=no",
        "-o", "HostKeyAlgorithms=+ssh-rsa",
        "-o", "PubkeyAcceptedAlgorithms=+ssh-rsa",
        "-o", "KexAlgorithms=+diffie-hellman-group14-sha1",
        "-o", "ConnectTimeout=6",
    ]

    def __init__(self, ip=None, password=None):
        # ScopusQA #11: no credential in the source. The address is the modem's
        # own ECM-link constant and is a committed default; the password is
        # site data and comes from bench.ini (or $MODEM_PASSWORD), which is
        # untracked. See scopus/lib/settings.py.
        ip       = ip or S.get("modem", "ip")
        password = password if password is not None else S.require("modem", "password")
        self.ip = ip
        self.password = password

    def run(self, cmd: str, timeout=15.0) -> Tuple[int, str, str]:
        argv = ["sshpass", "-p", self.password, "ssh", *self.OPTS,
                f"root@{self.ip}", cmd]
        try:
            p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
            return p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            return 124, "", "timeout"

    def reachable(self) -> bool:
        return self.run("true", timeout=8)[0] == 0

    def app_running(self, app="sdvrApp") -> bool:
        rc, out, _ = self.run(f"/legato/systems/current/bin/app status {app}")
        return rc == 0 and "[running]" in out

    def sdvr_log(self, lines=40) -> str:
        return self.run(f"tail -n {int(lines)} /data/sdvr/sdvr.log 2>/dev/null")[1]
