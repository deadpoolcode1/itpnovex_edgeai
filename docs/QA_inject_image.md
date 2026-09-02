# Testing the camera on a picture instead of the lens

Use this when you want to know what the neural network finds in a picture whose
answer you already know, without walking people in front of the camera.

Everything runs from the bench PC, in `~/work/itpnovex/edgeai`.

## 1. The normal way — one command

    cd ~/work/itpnovex/edgeai
    python3 scopus/preflight.py                                   # once, checks the camera is there
    python3 scopus/inference_test.py --image images/3_people.jpg --expect 3

`--image` takes any picture on the PC. `--expect` is how many people you know
are in it. With no arguments it runs `images/3_people.jpg` expecting 3.

The command does the whole cycle by itself: stops live detection, sends the
picture, runs the network on it, prints what it found, and puts the camera back
on its own lens. It takes about a minute.

Read the last line. It says `PASSED` or `FAILED`.

Pictures with known answers are in `images/`: `1_person.jpg`, `2_people.jpg`,
`3_people.jpg`, `5_people.jpg`, `7_people.jpg`. A larger set is in
`tests/images/` (`crowd_13.jpg`, `cars_15.jpg`, `13ppl_5trucks.jpg` and others).

### Two results that look wrong and are not

- `7_people.jpg` returns **6**, every time. One person is occluded.
- `5_people.jpg` returns **1**, every time. The other figures are too distant to
  survive the 256x256 network input.

Do not report these as faults.

## 2. Picture format and size

**There is no requirement. Do not convert anything.**

Any JPEG, PNG, BMP, TIFF or WEBP. Any width, height or file size. The tool
resizes and converts the picture for you before sending it.

Two things are still worth knowing, because they explain odd results:

- The picture is squeezed into a **square 256x256**, which is the network's
  input size. Aspect ratio is not preserved — a very wide picture comes out
  distorted, people in it get thin, and they may be missed. If a result looks
  wrong, crop the picture roughly square and try again.
- 256x256 is small. Anything that ends up a few pixels across in that square —
  a person far away in a wide landscape shot — will not be found. That is the
  network's input size, not a fault. (This is what `5_people.jpg` shows.)

For reference only: what actually goes over the wire is 256x256 RGB888,
exactly 196,608 bytes. You never build that yourself.

## 3. By hand, over the camera shell

Only if you need the steps separately. Run them in this order:

    python3 scopus/cam.py "detect stop"
    python3 n6cam-inject-frame.py images/3_people.jpg
    python3 scopus/cam.py "detect start"
    python3 scopus/cam.py "frame run"
    python3 scopus/cam.py "frame clear"

- `detect stop` first: the picture is a bulk binary transfer, and a
  notification arriving in the middle of it breaks the upload.
- `detect start` before `frame run`: `frame run` refuses to run with the
  network stopped.
- `frame run` prints the detection count, then one line per object with its
  class, confidence and box. `class=0` is a person, `class=2` a car,
  `class=7` a truck. 80-100 ms is a normal `NN` time.

What it looks like when it works:

    frame run: 3 detection(s), NN 87.9ms
      [0] class=0 conf=0.78 bbox=(0.73,0.71,0.18,0.44)
      [1] class=0 conf=0.71 bbox=(0.51,0.64,0.15,0.60)
      [2] class=0 conf=0.84 bbox=(0.36,0.73,0.14,0.41)

## 4. Always finish with `frame clear`

The injected picture is sticky. Until it is cleared, the camera keeps running
inference on that same still picture: the live view draws that picture's boxes
over real video, and — because the notification fires on the 0-to-N box edge —
no real person walking into view can raise an event.

While a picture is loaded, the live view shows `TEST PICTURE - NOT THE LENS`
with a countdown. If you see that banner, the camera is injected.

`inference_test.py` clears it for you on every exit, including a failed run.
If you did it by hand, or a run was interrupted, check with:

    python3 scopus/cam.py "frame query"

`frame: empty` is what you want. `frame: loaded` means a picture is still in
there — run `frame clear`. It also lapses on its own after 120 seconds.

## 5. Injected pictures do not send events — on purpose

By default an injected picture produces no snapshot, no upload and no
notification to the server. That is deliberate: a test picture must not reach
the customer's server as a real event.

To test the notification path with a picture, arm it **after** the injection
and before `frame run`:

    python3 n6cam-inject-frame.py images/3_people.jpg
    python3 scopus/cam.py "frame report on"
    python3 scopus/cam.py "detect start"
    python3 scopus/cam.py "frame run"

`frame upload` begins with a `frame clear`, and `frame clear` disarms this — so
arming it first does nothing. The notification also lags `frame run` by about a
second, so keep reading the console for a few seconds after the command
answers.

## 6. Many pictures at once

Put the pictures in a folder and run:

    python3 scopus/qa_sweep.py ~/qa-images

It injects each one in turn and prints what was found, by class. The results
are written to `scopus/results/qa-image-sweep.json`.

## 7. Seeing what the detector sees

Injection answers "what would the network find in this picture". It does not
answer "what picture is the network being given", and when the overlay
disagrees with the screen that second question is the one that matters: a
person who is plainly there and is not counted is either a network that missed
them or a network that was never shown them, and those have opposite fixes.

    python3 n6cam-grab-frame.py --source nn     -o nn_input.png    # the network's own input
    python3 n6cam-grab-frame.py --source live   -o live_frame.png  # what the screen shows

`--source nn` saves the 256x256 the network is handed. `--source live` saves
the 800x600 preview. Both come off the camera as it is running; neither
disturbs detection.

Each run also prints the geometry, and that line is the point of the tool:

    frame grab: sensor 2592x1944  nn-area 0,0 2592x1944  main-area 0,0 2592x1944

`nn-area` and `main-area` are the parts of the sensor the two pipes are fed
from. **They must be the same rectangle.** When they were not — the detector
used to be given a 1944x1944 centre crop while the screen showed all of
2592x1944 — the left and right eighths of every scene were invisible to the
detector and nothing anywhere said so (ScopusQA #25).

    frame grab: buffer 0x90030000, pipe filling 0x90000000

The picture must come from a buffer the camera is not writing into. If that
line says `SAME`, the frame can be a splice of two frames and anything moving
in it is torn.

## 8. If it fails

| What you see | What it means |
|---|---|
| `The camera was not found` | Run `python3 scopus/preflight.py`. |
| `No upload banner from kit` | The camera was busy sending a notification. Run `detect stop` and try again. |
| `ERROR: size=110592, expected 196608` | You used `n6cam-regress.py` or `n6cam-prep-tests.py`. Those two are out of date. Use `n6cam-inject-frame.py`. |
| `ERROR: CRC mismatch` | The transfer was corrupted. Repeat it. |
| `frame run: no frame loaded` | The upload did not finish. Repeat the upload. |
| `frame run: NN is stopped` | Run `detect start` first. |
| `frame run: no inference ... within 3s` | The camera pipeline is not running. Reboot the camera. |
| `must be 192x192 RGB888` | Ignore the numbers in that message — they are stale text. The real size is 196,608 bytes. |
| Same detections on every picture | The picture is not reaching the network. Run `frame query` — it should say `loaded`. |
