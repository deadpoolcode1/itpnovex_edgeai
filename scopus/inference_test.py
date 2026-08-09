#!/usr/bin/env python3
"""Check the neural network counts people correctly, on a known picture.

Pointing the lens at real people proves the product works but proves nothing
about the count, because nobody knows what the right answer was. So this pushes
a picture whose answer is known into the camera's inference buffer and checks
what comes back:

    python3 scopus/inference_test.py                    # 3 people
    python3 scopus/inference_test.py --image images/7_people.jpg --expect 7

It does the five things Step 7 of the tester manual describes, in the order
that works:

  1. stop live detection    — the picture upload is a bulk binary transfer, and
                              a notification arriving in the middle of it
                              breaks the upload
  2. inject the picture
  3. start detection        — 'frame run' refuses to run with the NN stopped
  4. run inference on it
  5. clear the picture      — see below; this one is not optional

and retries the cycle if the camera's console reply goes missing, which happens
when the camera is sending a notification at that moment. The run itself is
fine; only the reply to it is lost, so retrying is right and reporting a
failure would not be.

Step 5 exists because the injected picture is sticky. 'frame upload' registers
it as the NN's input buffer immediately and nothing but 'frame clear' un-
registers it, so a run that ends at step 4 leaves the camera inferring on the
same still picture for ever after: the live view draws that picture's boxes
over the real video, and — because the detection notification fires on the
0->N box edge — the box count never falls back to zero, so no real person
walking into view can ever raise an event again. It is cleared on every exit
path, including a failed run, because a camera left wedged that way looks
healthy from the outside.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "lib"))
from devices import N6Shell                                  # noqa: E402

INJECT = os.path.join(REPO, "n6cam-inject-frame.py")
G, R, Y, Z = "\033[92m", "\033[91m", "\033[93m", "\033[0m"


def sh_cmd(sh, cmd, wait=6.0):
    body = sh.send(cmd, None, wait)
    i = body.find(cmd)
    if i >= 0:
        body = body[i + len(cmd):]
    return body.replace("\r", "").strip("\n> \t")


def wait_ready(sh, cap=40.0):
    """Wait until the shell is answering again.

    Starting detection usually produces a detection immediately — the camera is
    pointed at a room — and while it is sending that notification the console
    prints nothing at all, not even an echo. Sleeping a fixed two seconds and
    hoping is what made this test need three attempts; asking the shell whether
    it is back, with a command that costs nothing, makes it one.
    """
    end = time.time() + cap
    while time.time() < end:
        if "Uptime" in sh.send("uptime", "Uptime", 3.0):
            return True
        time.sleep(1.0)
    return False


def restore_live(sh):
    """Put the NN back on the lens, and say so in the tester's own words.

    Called on every exit path. The reply is printed rather than swallowed
    because 'frame: cleared (NN back to live camera)' is the line that tells
    the tester Step 9 can work — without it, Step 9 cannot, and the failure
    looks like a detection problem rather than a leftover from this step.
    """
    print(f"{Y}  [5/5]{Z} returning the camera to the live lens")
    try:
        out = sh_cmd(sh, "frame clear", 6.0)
    except OSError as e:
        # The port went away — the camera re-enumerated, i.e. it restarted.
        # Say that plainly instead of burying the real failure under a
        # traceback from the clean-up step.
        print(f"        {R}the camera's port disappeared ({e}). That means it "
              f"restarted; a restart clears the picture by itself.{Z}")
        print(f"        {R}Wait 30 s, then run:  python3 scopus/preflight.py"
              f"{Z}")
        return False
    for line in out.splitlines():
        if line.strip():
            print(f"        {line.strip()}")
    if "cleared" not in out:
        print(f"        {R}the camera did not confirm 'frame: cleared'. Run "
              f"this before Step 9:{Z}")
        print(f"        {R}    python3 scopus/cam.py \"frame clear\"{Z}")
        return False
    return True


def attempt(sh, image):
    print(f"{Y}  [1/5]{Z} stopping live detection")
    sh_cmd(sh, "detect stop", 4.0)
    time.sleep(1.5)

    print(f"{Y}  [2/5]{Z} injecting {os.path.relpath(image, REPO)}")
    sh.close()                        # the uploader needs the port to itself
    up = subprocess.run([sys.executable, INJECT, image],
                        capture_output=True, text=True, cwd=REPO)
    for line in up.stdout.splitlines():
        if line.strip():
            print(f"        {line.strip()}")
    sh._open()
    sh.drain(0.3)
    if "frame upload: ok" not in up.stdout:
        print(f"        {R}the picture did not load{Z}")
        return None

    print(f"{Y}  [3/5]{Z} starting detection")
    sh_cmd(sh, "detect start", 4.0)
    wait_ready(sh)

    print(f"{Y}  [4/5]{Z} running inference on it")
    out = sh_cmd(sh, "frame run", 20.0)
    for line in out.splitlines():
        if line.strip():
            print(f"        {line.strip()}")
    if "detection(s)" in out:
        return out
    print(f"        {Y}(no reply — the camera was busy sending a "
          f"notification; retrying){Z}")
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Run inference on a known image.")
    ap.add_argument("--image", default=os.path.join(REPO, "images",
                                                    "3_people.jpg"))
    ap.add_argument("--expect", type=int, default=3,
                    help="how many people are in the picture")
    ap.add_argument("--tries", type=int, default=4)
    args = ap.parse_args()

    image = (args.image if os.path.isabs(args.image)
             else os.path.join(REPO, args.image))
    if not os.path.exists(image):
        print(f"{R}No such image: {image}{Z}", file=sys.stderr)
        return 2

    try:
        sh = N6Shell()
    except RuntimeError:
        print(f"{R}The camera was not found. Run: python3 "
              f"scopus/preflight.py{Z}", file=sys.stderr)
        return 2
    print(f"Camera on {sh.tty} — expecting {args.expect} people in "
          f"{os.path.basename(image)}\n")

    out = None
    restored = False
    try:
        for n in range(1, args.tries + 1):
            print(f"Attempt {n} of {args.tries}:")
            out = attempt(sh, image)
            if out:
                break
            print()
    finally:
        # In the finally, not after the loop: a camera left holding the test
        # picture is the one failure of this script that silently breaks the
        # step after it, so Ctrl-C and an unhandled error have to clear it too.
        try:
            restored = restore_live(sh)
        finally:
            sh.close()

    if not out:
        print(f"\n{R}FAILED{Z} — the camera never reported a result.")
        return 1

    got = out.split("detection(s)")[0].split(":")[-1].strip()
    ms = out.split("NN ")[-1].split("ms")[0] if "NN " in out else "?"
    if got != str(args.expect):
        print(f"\n{R}FAILED{Z} — the camera counted {got}, the picture "
              f"has {args.expect}.")
        return 1
    if not restored:
        print(f"\n{R}FAILED{Z} — {got} people were counted correctly, but the "
              f"picture is still loaded in the camera. Clear it before Step 9.")
        return 1
    who = "person" if got == "1" else "people"
    print(f"\n{G}PASSED{Z} — {got} {who} detected in {ms} ms, which matches "
          f"the picture. The camera is back on its live lens.")
    print("'class=0' on each line is the person class; 'conf' is how sure the "
          "network is.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
