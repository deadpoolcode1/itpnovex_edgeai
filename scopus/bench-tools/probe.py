#!/usr/bin/env python3
"""Passive-ish probe: open a tty raw @ baud, send a prompt, dump what comes back."""
import os, sys, time, termios, tty

def probe(dev, baud, sends, listen=2.0, label=""):
    speed = getattr(termios, f"B{baud}")
    print(f"\n===== {label or dev} @ {baud} =====")
    try:
        fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as e:
        print(f"  OPEN FAILED: {e}")
        return
    try:
        attrs = termios.tcgetattr(fd)
        iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
        iflag = 0
        oflag = 0
        cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
        lflag = 0
        cc = list(cc)
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW,
                          [iflag, oflag, cflag, lflag, speed, speed, cc])
        termios.tcflush(fd, termios.TCIOFLUSH)

        # 1) purely passive first — is anything talking on its own?
        t0 = time.time(); passive = b""
        while time.time() - t0 < 1.0:
            try:
                passive += os.read(fd, 4096)
            except BlockingIOError:
                time.sleep(0.02)
        if passive:
            print(f"  [passive 1s] {len(passive)}B: {passive[:300]!r}")
        else:
            print("  [passive 1s] (silent)")

        # 2) active prompts
        for s in sends:
            termios.tcflush(fd, termios.TCIFLUSH)
            os.write(fd, s)
            t0 = time.time(); buf = b""
            while time.time() - t0 < listen:
                try:
                    buf += os.read(fd, 4096)
                except BlockingIOError:
                    time.sleep(0.02)
            status = "REPLY" if buf else "NO REPLY"
            print(f"  send {s!r:30} -> {status} {len(buf)}B")
            if buf:
                print(f"      {buf[:400]!r}")
    finally:
        os.close(fd)

if __name__ == "__main__":
    dev = sys.argv[1]; baud = int(sys.argv[2])
    sends = [b"\r\n", b"system version\r\n", b"AT\r\n"]
    probe(dev, baud, sends, label=dev)
