#!/usr/bin/env python3
"""Talk to the modem's console/getty on the FTDI (/dev/ttyUSB0) and ask it
what is actually running and which tty the SDVR app holds."""
import os, sys, time, termios

DEV = "/dev/ttyUSB0"


def open_raw(dev, baud=115200):
    speed = getattr(termios, f"B{baud}")
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    cc = list(termios.tcgetattr(fd)[6])
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW,
                      [0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL, 0,
                       speed, speed, cc])
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def talk(fd, s, wait=3.0):
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, s.encode())
    t0, buf = time.monotonic(), b""
    while time.monotonic() - t0 < wait:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.02)
    return buf.decode(errors="replace")


fd = open_raw(DEV)
try:
    print("=== wake console ===")
    print(repr(talk(fd, "\r\n", 2.0)))

    for cmd in ("\r\n",
                "root\r\n",
                "echo MARKER_$((6*7))\r\n",
                "ps w | grep -i sdvr\r\n",
                "ls -l /dev/ttyHS0 /dev/ttyHSL1\r\n",
                "fuser /dev/ttyHS0 /dev/ttyHSL1\r\n",
                "cat /legato/systems/current/apps/*/read-only/usr/bin/* 2>/dev/null | head -c 0; app status 2>&1 | head -20\r\n"):
        print(f"\n>>> {cmd.strip()!r}")
        print(talk(fd, cmd, 3.5)[:800])
finally:
    os.close(fd)
