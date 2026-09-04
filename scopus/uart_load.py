#!/usr/bin/env python3
"""Hammer the camera-to-modem link and read the RX ring counters (ScopusQA #23).

The question this answers is not "does the link work" but "is it losing bytes
nobody notices". A command that goes unanswered is re-sent by the layer above,
so a link that drops the occasional byte still delivers every command and looks
healthy from the outside. A URC does not get that protection: `+SDVRNET: UP`,
`+SDVRRDY` and an incoming remote command are not replies to anything, and a
lost one is lost.

So this runs N AT round trips while the camera is doing its heaviest work (a
tiled sweep every ~1.5 s) and then prints `mdm stats`. The numbers that matter:

    ring: ... lost=0        bytes the DMA wrote over before anyone read them.
                            Must be 0. This is the only way the ring can still
                            drop a URC.
    ring: ... peak=N/1024   the most that has ever been waiting. Evidence for
                            the ring's size, and evidence that it is doing
                            something: a peak well above zero is bytes that
                            arrived while nobody was reading.
    ring: ... restarts=N    re-arms after an RX error aborted the DMA. Climbing
                            means the line is noisy, not that the code is wrong.
    rx: badcrc / stray      corruption that got as far as the decoder.

    python3 scopus/uart_load.py [count]

Counting an `ERROR` as answered is deliberate: this measures the transport, and
a reply that comes back at all has made the full round trip. Whether the modem
liked the command is a different question.
"""
import subprocess
import sys
import time

CAM = ["python3", "scopus/cam.py"]


def cam(cmd, timeout=30):
    r = subprocess.run(CAM + [cmd], capture_output=True, text=True,
                       timeout=timeout)
    return r.stdout


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 60

    cam("mdm stats reset")
    answered = missed = 0
    t0 = time.time()
    for _ in range(n):
        try:
            out = cam("mdm AT")
        except subprocess.TimeoutExpired:
            missed += 1
            continue
        if ("OK" in out) or ("ERROR" in out):
            answered += 1
        else:
            missed += 1
    dt = time.time() - t0

    print(f"round trips: {n} sent, {answered} answered, {missed} missed, "
          f"{dt:.0f}s")
    print(cam("mdm stats"))
    return 0 if missed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
