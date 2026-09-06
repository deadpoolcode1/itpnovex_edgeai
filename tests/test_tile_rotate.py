#!/usr/bin/env python3
"""Build and run the turned-second-look test (ScopusQA #26).

The C side is self-checking; this compiles the REAL `tile_detect.c` against a
handful of host stubs and reports what it says. No kit needed, so it runs on
any machine and in a few milliseconds.

  $ python3 tests/test_tile_rotate.py

Exit code 0 if every check passes.

Why it exists: `detect rotate auto` makes a sweep LONGER part-way through, and
a caller that trusted the length tile_sweep_begin() returned would walk the
first 14 steps and stop, so the second looks would never run, no output would
look wrong, and a person lying down would go on being missed exactly as before.
That is the kind of failure a bench test cannot separate from "the network did
not see them", which is why this one is here and not there.

The three sizes the module reads out of camera_config.h are taken from the real
header and passed to the stub with -D, so the stub cannot quietly disagree with
the firmware about how big a frame is.
"""
from __future__ import annotations
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent
FW   = ROOT / "vendor" / "n6cam.core.bsp" / "Firmware"

C_SRC  = HERE / "c" / "test_tile_rotate.c"
STUBS  = HERE / "c" / "stubs"
TILE_C = FW / "Application" / "Core" / "Src" / "tile_detect.c"
INC_A  = FW / "Application" / "Core" / "Inc"
CAMCFG = INC_A / "Tasks" / "camera_config.h"
BIN    = Path("/tmp/n6_test_tile_rotate")

WANTED = ("CAMERA_MAIN_WIDTH", "CAMERA_MAIN_HEIGHT", "CAMERA_ANCILLARY_WIDTH")


def geometry() -> list[str]:
    """The live-pipe sizes, read out of the firmware's own camera_config.h.

    The file defines each name twice, once under ISP_MW_TUNING_TOOL_SUPPORT and
    once for the shipped build; the second is the one the firmware compiles, so
    take the last of each.
    """
    text = CAMCFG.read_text()
    out = []
    for name in WANTED:
        hits = re.findall(rf"^\s*#define\s+{name}\s+(\d+)U?\s*$", text, re.M)
        if not hits:
            print(f"could not find {name} in {CAMCFG}", file=sys.stderr)
            sys.exit(2)
        out.append(f"-D{name}={hits[-1]}U")
    return out


def main() -> int:
    cmd = ["gcc", "-Wall", "-Wextra", "-O2", "-std=c11",
           "-I", str(STUBS), "-I", str(INC_A), *geometry(),
           str(C_SRC), str(TILE_C), "-o", str(BIN), "-lm"]
    subprocess.run(cmd, check=True)
    return subprocess.run([str(BIN)]).returncode


if __name__ == "__main__":
    sys.exit(main())
