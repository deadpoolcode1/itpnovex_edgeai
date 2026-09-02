#!/usr/bin/env python3
"""
n6cam-grab-frame.py — save the picture the detector is actually looking at.

    n6cam-grab-frame.py [--source nn|live] [--port /dev/ttyACMx] [-o out.png]

`--source nn` (the default) saves the ancillary buffer: the exact bytes the
neural network is handed, 256x256 RGB888. `--source live` saves the main pipe,
800x600 — what the preview window and the UVC stream show.

Why both. Every other view of the detector shows its answer (boxes, counts, the
overlay). When the answer disagrees with the screen there is no way, from the
answer alone, to tell whether the network was wrong about the picture or was
handed a different picture — and those two have opposite fixes. ScopusQA #25
sat between them: a frame the live path found nobody in was found correctly
when the very same pixels were pushed back in over `frame upload`.

The tool also prints the two pipes' effective sensor areas, which is what makes
the two pictures comparable: if `nn-area` and `main-area` are not the same
rectangle, the detector and the screen are looking at different parts of the
sensor and no amount of staring at boxes will say so.
"""
import argparse
import base64
import glob
import os
import sys
import time

BY_ID = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02"


def find_port() -> str:
    hits = sorted(glob.glob(BY_ID))
    if hits:
        return os.path.realpath(hits[0])
    hits = sorted(glob.glob("/dev/ttyACM*"))
    if not hits:
        sys.exit("No N6Cam CDC port found (looked for %s and /dev/ttyACM*)" % BY_ID)
    return hits[-1]


def open_port(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        import termios

        attrs = termios.tcgetattr(fd)
        attrs[0] = attrs[1] = attrs[3] = 0          # iflag, oflag, lflag: raw
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception:
        pass                                        # a plain CDC node is fine
    return fd


def write_all(fd: int, data: bytes) -> None:
    n = 0
    while n < len(data):
        try:
            n += os.write(fd, data[n:])
        except BlockingIOError:
            time.sleep(0.005)


def drain_quiet(fd: int, quiet: float = 0.4, cap: float = 5.0) -> None:
    """Read until the port has said nothing for `quiet` seconds.

    Whatever the shell was doing when we opened the port keeps arriving
    afterwards; those bytes would otherwise be read as the start of our dump.
    """
    end = time.time() + cap
    last = time.time()
    while time.time() < end:
        try:
            chunk = os.read(fd, 8192)
        except BlockingIOError:
            chunk = b""
        if chunk:
            last = time.time()
        elif time.time() - last >= quiet:
            return
        time.sleep(0.01)


def read_dump(fd: int, timeout: float):
    """Collect the base64 body between `frame grab: begin` and `: end`.

    Returns (header_lines, payload_bytes). A dump that never reaches `end` is
    an error rather than a short picture: half a frame decoded into a PNG looks
    like a camera fault, which is the exact confusion this tool exists to
    remove.
    """
    end_at = time.time() + timeout
    buf = b""
    headers, body = [], []
    started = finished = False

    while time.time() < end_at:
        try:
            chunk = os.read(fd, 65536)
        except BlockingIOError:
            chunk = b""
        if not chunk:
            time.sleep(0.005)
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("ascii", "replace").strip()
            if not line:
                continue
            if line.startswith("frame grab:"):
                if line.endswith("begin"):
                    started = True
                elif line.endswith("end"):
                    finished = True
                else:
                    headers.append(line)
                    if "no camera buffer" in line:
                        sys.exit(line)
                continue
            if started and not finished:
                body.append(line)
        if finished:
            break

    if not finished:
        sys.exit("frame grab did not finish within %.0fs (got %d lines)"
                 % (timeout, len(body)))
    return headers, base64.b64decode("".join(body))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", choices=("nn", "live"), default="nn")
    ap.add_argument("--port", default=None)
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    port = args.port or find_port()
    out = args.out or ("nn_input.png" if args.source == "nn" else "live_frame.png")

    fd = open_port(port)
    try:
        drain_quiet(fd)
        write_all(fd, ("frame grab %s\r\n" % args.source).encode())
        headers, data = read_dump(fd, args.timeout)
    finally:
        os.close(fd)

    for h in headers:
        print(h)

    size = None
    for h in headers:
        for tok in h.split():
            if "x" in tok and tok.replace("x", "").isdigit():
                w, s, hgt = tok.partition("x")
                size = (int(w), int(hgt))
                break
        if size:
            break
    if size is None:
        sys.exit("no geometry in the dump header")

    want = size[0] * size[1] * 3
    if len(data) != want:
        sys.exit("short dump: %d bytes, expected %d for %dx%d"
                 % (len(data), want, size[0], size[1]))

    try:
        from PIL import Image
    except ImportError:
        raw = os.path.splitext(out)[0] + ".raw"
        open(raw, "wb").write(data)
        print("Pillow not installed — wrote raw RGB888 to %s (%dx%d)"
              % (raw, size[0], size[1]))
        return 0

    Image.frombytes("RGB", size, data).save(out)
    print("wrote %s (%dx%d)" % (out, size[0], size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
