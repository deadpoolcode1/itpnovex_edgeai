#!/usr/bin/env python3
"""
n6cam-update.py — push a new Application_signed.bin to the SIANA N6Cam kit
over its CDC-ACM port. Replaces the SWD+boot-switch flash cycle for daily
firmware iteration.

Usage:
    n6cam-update.py <Application_signed.bin> [/dev/ttyACMx]

The kit's CDC port is usually /dev/ttyACM1 (the STLink VCP is /dev/ttyACM0).
Find it reliably with:
    ls /dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02
"""
import os
import struct
import sys
import time
import zlib


MAX_SIZE = 1024 * 1024  # SLOT1_APP region size on xSPI


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    fn = sys.argv[1]
    tty = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM1"

    with open(fn, "rb") as f:
        data = f.read()
    size = len(data)
    if size == 0 or size > MAX_SIZE:
        print(f"Bad size: {size} (max {MAX_SIZE})")
        return 1
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Firmware: {fn} ({size} bytes, CRC32 0x{crc:08x})")

    # Put the tty into raw 115200 8N1
    rc = os.system(f"stty -F {tty} 115200 cs8 -cstopb -parenb raw -echo")
    if rc != 0:
        print(f"stty failed on {tty}")
        return 1

    with open(tty, "r+b", buffering=0) as port:
        # Trigger the shell 'update' command.
        port.write(b"\nupdate\n")
        port.flush()
        # Give the App a moment to print its prompt and switch to binary RX.
        time.sleep(0.5)

        hdr = b"UPDT" + struct.pack("<II", size, crc)
        port.write(hdr)
        port.flush()

        # Chunk the payload — 4 KB at a time keeps the device buffer ahead of
        # CDC USB transfers without blocking on flow control.
        CHUNK = 4096
        for i in range(0, size, CHUNK):
            port.write(data[i:i + CHUNK])
            port.flush()
        print(f"Sent {size} bytes. Device should reboot in a few seconds.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
