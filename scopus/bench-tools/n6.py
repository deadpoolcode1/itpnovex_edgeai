#!/usr/bin/env python3
"""Minimal N6 CDC shell driver: send commands, print replies."""
import os, sys, time, termios

DEV = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02"


def open_raw(dev, baud=115200):
    speed = getattr(termios, f"B{baud}")
    fd = os.open(os.path.realpath(dev), os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    cc = list(termios.tcgetattr(fd)[6])
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW,
                      [0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL, 0,
                       speed, speed, cc])
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def send(fd, cmd, wait=2.5):
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, cmd.encode() + b"\r\n")
    t0, buf = time.time(), b""
    while time.time() - t0 < wait:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.02)
        if buf.endswith(b"> ") and len(buf) > len(cmd) + 4:
            # keep draining briefly in case more is coming
            time.sleep(0.15)
            try:
                buf += os.read(fd, 4096)
            except BlockingIOError:
                pass
            break
    return buf.decode(errors="replace")


if __name__ == "__main__":
    fd = open_raw(DEV)
    try:
        for cmd in sys.argv[1:]:
            wait = 6.0 if cmd.startswith("mdm") else 2.5
            out = send(fd, cmd, wait)
            print(f"\n{'='*60}\n>>> {cmd}\n{'-'*60}\n{out.strip()}")
    finally:
        os.close(fd)
