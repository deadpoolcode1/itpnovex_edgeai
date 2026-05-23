#!/usr/bin/env python3
"""Cross-check the C HDLC encoder against the Python one.

Build the C host-test binary, run it, parse each `<name>\\t<hex>` line, and
require that tools/hdlc.encode() produces the same wire bytes for the same
payload. Catches drift between the firmware-side implementation and the
host-side tools (fake modem, photo upload listener, etc.).

  $ python3 tests/test_hdlc_crosscheck.py

Exit code 0 on success, 1 on any mismatch. Run as part of the broader
test suite via `make hdlc-check` (see Makefile).
"""
from __future__ import annotations
import os
import subprocess
import sys
from pathlib import Path

HERE   = Path(__file__).parent
ROOT   = HERE.parent
C_SRC  = HERE / "c" / "test_hdlc.c"
HDLC_C = ROOT / "vendor" / "n6cam.core.bsp" / "Firmware" / "Application" / "Core" / "Src" / "hdlc.c"
HDLC_H = ROOT / "vendor" / "n6cam.core.bsp" / "Firmware" / "Application" / "Core" / "Inc"
BIN    = Path("/tmp/n6_test_hdlc")

sys.path.insert(0, str(ROOT / "tools"))
import hdlc as py_hdlc  # noqa: E402

# Same set of payloads the C side uses; keep in lock-step.
CASES = {
    "empty":         b"",
    "at":            b"AT\r\n",
    "flags-and-esc": bytes([0x7E, 0x7D, 0x7E, 0x7D]) + b"hi",
    "0-255":         bytes(range(256)),
    "sdvrupl":       b'+SDVRUPL: PROGR,"4325412_23052026_111111.rdy",1\r\n',
}


def build() -> None:
    subprocess.run(
        ["gcc", "-Wall", "-Wextra", "-O2",
         "-I", str(HDLC_H),
         str(C_SRC), str(HDLC_C),
         "-o", str(BIN)],
        check=True,
    )


def run() -> dict[str, bytes]:
    out = subprocess.check_output([str(BIN)], text=True)
    wire: dict[str, bytes] = {}
    for line in out.splitlines():
        line = line.strip()
        if not line or line.startswith("ERROR:"):
            print(f"C test failure: {line}")
            sys.exit(1)
        name, hexstr = line.split("\t", 1)
        wire[name] = bytes.fromhex(hexstr)
    return wire


def main() -> int:
    build()
    c_wire = run()

    failed = 0
    for name, payload in CASES.items():
        if name not in c_wire:
            print(f"{name:18s} C did not emit a case for it")
            failed += 1
            continue
        py_wire = py_hdlc.encode(payload)
        if c_wire[name] != py_wire:
            print(f"{name:18s} MISMATCH")
            print(f"  C : {c_wire[name].hex()}")
            print(f"  Py: {py_wire.hex()}")
            failed += 1
        else:
            # Also verify the Python decoder accepts the C-emitted bytes.
            d = py_hdlc.Decoder()
            d.feed_bytes(c_wire[name])
            if d.frames != [payload]:
                print(f"{name:18s} Py decoder dropped a valid C frame")
                failed += 1
            else:
                print(f"{name:18s} OK  ({len(c_wire[name])} wire bytes)")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
