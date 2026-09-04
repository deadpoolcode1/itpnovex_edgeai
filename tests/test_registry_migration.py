#!/usr/bin/env python3
"""Build and run the registry version-upgrade test (ScopusQA #27).

The C side is self-checking, so this only has to compile it against the real
`slib32_registry.c` and the real `registry.h` and report what it says. Doing it
from Python keeps it in the same place as the other host tests and means it
runs without a kit on the bench.

  $ python3 tests/test_registry_migration.py

Exit code 0 if every check passes.

Why it exists: the store used to be reset to factory defaults by every
REGISTRY_VERSION bump, so a firmware update that added one field silently threw
away the operator's notification mask, detection profile, server endpoints and
image settings. Adding a field is routine; losing a customer's configuration
because of it is not, and nothing in the tree would have caught it.
"""
from __future__ import annotations
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent
FW   = ROOT / "vendor" / "n6cam.core.bsp" / "Firmware"

C_SRC  = HERE / "c" / "test_registry_migration.c"
REG_C  = FW / "Middlewares" / "SIANA" / "slib32" / "slib32_registry.c"
INC_A  = FW / "Application" / "Core" / "Inc"
INC_S  = FW / "Middlewares" / "SIANA" / "slib32"
BIN    = Path("/tmp/n6_test_registry_migration")


def main() -> int:
    subprocess.run(
        ["gcc", "-Wall", "-Wextra", "-O2",
         "-I", str(INC_A), "-I", str(INC_S),
         str(C_SRC), str(REG_C), "-o", str(BIN)],
        check=True,
    )
    r = subprocess.run([str(BIN)], text=True)
    if r.returncode != 0:
        print("registry migration test FAILED", file=sys.stderr)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
