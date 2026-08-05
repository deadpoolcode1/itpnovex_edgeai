#!/usr/bin/env python3
"""HDLC prober for the SDVR host UART. Mirrors sdvr-app/.../hdlc.c exactly:
CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021, no reflect, no final xor),
0x7E flags, 0x7D escape with XOR 0x20."""
import os, sys, time, termios

FLAG, ESC, MASK = 0x7E, 0x7D, 0x20


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(payload: bytes) -> bytes:
    body = payload + crc16(payload).to_bytes(2, "big")
    out = bytearray([FLAG])
    for b in body:
        if b in (FLAG, ESC):
            out += bytes([ESC, b ^ MASK])
        else:
            out.append(b)
    out.append(FLAG)
    return bytes(out)


def decode_stream(buf: bytes):
    """Yield (payload, crc_ok) for every complete frame in buf."""
    frames, cur, in_f, esc = [], bytearray(), False, False
    for b in buf:
        if b == FLAG:
            if in_f and cur:
                if len(cur) >= 2:
                    payload, rx = bytes(cur[:-2]), int.from_bytes(cur[-2:], "big")
                    frames.append((payload, crc16(payload) == rx))
                else:
                    frames.append((bytes(cur), False))
            cur, in_f, esc = bytearray(), True, False
            continue
        if not in_f:
            continue
        if esc:
            cur.append(b ^ MASK); esc = False
        elif b == ESC:
            esc = True
        else:
            cur.append(b)
    return frames


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


def read_for(fd, secs):
    t0, buf = time.time(), b""
    while time.time() - t0 < secs:
        try:
            buf += os.read(fd, 4096)
        except BlockingIOError:
            time.sleep(0.02)
    return buf


def main():
    dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
    cmds = sys.argv[2:] or ["AT", "AT+SDVRPING", "AT+SDVRVER"]

    # Self-check against the byte pattern recorded in the edgeai hadars commit.
    ref = encode(b"AT\r\n").hex(" ")
    print(f"encoder self-check  AT\\r\\n -> {ref}")
    print(f"expected (from commit msg): 7e 41 54 0d 0a c9 f0 7e")
    print(f"MATCH: {ref == '7e 41 54 0d 0a c9 f0 7e'}\n")

    fd = open_raw(dev)
    try:
        idle = read_for(fd, 1.0)
        if idle:
            print(f"[idle] {len(idle)}B {idle[:200]!r}")
        for c in cmds:
            frame = encode(c.encode() + b"\r\n")
            termios.tcflush(fd, termios.TCIFLUSH)
            os.write(fd, frame)
            raw = read_for(fd, 2.5)
            frames = decode_stream(raw)
            print(f"\n>>> {c}")
            print(f"    tx wire: {frame.hex(' ')}")
            print(f"    rx raw : {len(raw)}B {raw[:250]!r}")
            if frames:
                for p, ok in frames:
                    print(f"    FRAME crc_ok={ok} payload={p!r}")
            else:
                print("    (no complete HDLC frame in reply)")
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
