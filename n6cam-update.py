#!/usr/bin/env python3
"""
n6cam-update.py — push a new Application or NN-model image to the SIANA
N6Cam kit over its CDC-ACM port. Replaces the SWD+boot-switch flash cycle
for both daily firmware iteration AND model swaps.

Usage:
    n6cam-update.py [--target app|model] <file> [/dev/ttyACMx]

The kit's CDC port is usually /dev/ttyACM1 (STLink VCP is /dev/ttyACM0).
Find it reliably with:
    ls /dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02

Files:
    - For 'app' target: Application_signed.bin (signed by STM32_SigningTool,
      max 1 MB, written to xSPI offset 0x00400000 = SLOT1_APP).
    - For 'model' target: either the raw binary (network_atonbuf.xSPI2.raw,
      auto-detected by the .raw / .bin / .npy extension), OR an Intel HEX
      file (network_data.hex, auto-converted to binary on the host). Max
      16 MB, written to xSPI offset 0x00600000 = SLOT1_WEIGHTS.
"""
import argparse
import os
import struct
import sys
import time
import zlib
from pathlib import Path


MAX_APP_SIZE   = 1024 * 1024            # SLOT1_APP
MAX_MODEL_SIZE = 16 * 1024 * 1024       # ceiling matched to firmware-side buffer


def _hex_to_bin(path: Path, base_offset: int = 0x70600000) -> bytes:
    """Parse an Intel HEX file and return the raw bytes that should land at
    `base_offset` (the chip-relative offset is then computed by the firmware).

    Ignores EOF / segment / extended-linear records that don't carry data.
    Records past `base_offset + MAX_MODEL_SIZE` are dropped — the kit can't
    accept them anyway.
    """
    out = bytearray()
    upper = 0
    minimum = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            n = int(line[1:3], 16)
            offset = int(line[3:7], 16)
            rectype = int(line[7:9], 16)
            data_hex = line[9:9 + n * 2]
            if rectype == 0x04:  # extended linear address
                upper = int(data_hex, 16) << 16
            elif rectype == 0x00:  # data
                addr = upper + offset
                if minimum is None or addr < minimum:
                    minimum = addr
                end_addr = addr + n
                # Expand buffer to accomodate (sparse → flat — assume model
                # records are contiguous from base_offset; non-contiguous
                # holes default to 0xFF which matches NOR's erased state).
                while len(out) < end_addr - base_offset:
                    out.append(0xFF)
                payload = bytes(int(data_hex[i:i + 2], 16) for i in range(0, n * 2, 2))
                out[addr - base_offset:end_addr - base_offset] = payload
            # rectype 0x01 (EOF), 0x02 (extended segment), 0x05 (start linear),
            # 0x03 (start segment): not relevant for our model dumps.
    if minimum is not None and minimum != base_offset:
        print(f"WARNING: hex's first data record is at 0x{minimum:08x}, "
              f"not the expected 0x{base_offset:08x}", file=sys.stderr)
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", choices=["app", "model"], default="app",
                    help="What to update (default: app)")
    ap.add_argument("file", help="Image to send: .bin / .raw / .hex")
    ap.add_argument("tty", nargs="?", default="/dev/ttyACM1",
                    help="Kit's CDC port (default: /dev/ttyACM1)")
    args = ap.parse_args()

    fn = Path(args.file)
    if not fn.exists():
        print(f"File not found: {fn}")
        return 1

    # Auto-convert Intel HEX → raw binary on the host so the firmware only
    # has to handle one format. Detected by suffix.
    if fn.suffix.lower() == ".hex":
        if args.target != "model":
            print("WARNING: pushing a .hex file as 'app' target — usually you "
                  "want the signed .bin")
        print(f"Converting Intel HEX → raw binary on the host…")
        data = _hex_to_bin(fn)
    else:
        with open(fn, "rb") as f:
            data = f.read()

    size = len(data)
    max_size = MAX_MODEL_SIZE if args.target == "model" else MAX_APP_SIZE
    if size == 0 or size > max_size:
        print(f"Bad size: {size} bytes (max {max_size} for target={args.target})")
        return 1
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Target: {args.target}")
    print(f"Image:  {fn} ({size} bytes, CRC32 0x{crc:08x})")

    # Put the tty into raw 115200 8N1
    rc = os.system(f"stty -F {args.tty} 115200 cs8 -cstopb -parenb raw -echo")
    if rc != 0:
        print(f"stty failed on {args.tty}")
        return 1

    # Use raw os.open with O_NONBLOCK so we don't block on modem control
    # lines that the CDC ACM stack doesn't drive. Then os.write loops over
    # the buffer manually with a small sleep on EAGAIN.
    fd = os.open(args.tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    def write_all(buf: bytes):
        n = 0
        while n < len(buf):
            try:
                n += os.write(fd, buf[n:])
            except BlockingIOError:
                time.sleep(0.001)

    try:
        # Trigger the shell 'update [app|model]' command.
        write_all(f"\nupdate {args.target}\n".encode())
        time.sleep(0.5)
        write_all(b"UPDT" + struct.pack("<II", size, crc))

        CHUNK = 4096
        t0 = time.time()
        for i in range(0, size, CHUNK):
            write_all(data[i:i + CHUNK])
            if size > 1024 * 1024 and (i // CHUNK) % 256 == 0:
                elapsed = time.time() - t0
                pct = 100.0 * i / size
                rate = (i / elapsed / 1024) if elapsed > 0 else 0.0
                print(f"  {pct:5.1f}%  ({i//1024} KB / {size//1024} KB, "
                      f"{rate:.0f} KB/s)")
        dt = time.time() - t0
        print(f"Sent {size} bytes in {dt:.1f}s ({size/dt/1024:.0f} KB/s). "
              f"Device will erase+write+reboot — model can take ~90s, "
              f"app ~5s.")
    finally:
        os.close(fd)

    return 0


if __name__ == "__main__":
    sys.exit(main())
