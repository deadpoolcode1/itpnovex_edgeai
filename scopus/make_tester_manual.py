#!/usr/bin/env python3
"""Generate Scopus_Tester_Manual.docx from the procedure below.

The manual is generated rather than hand-edited so the commands in it stay in
one place with the rest of the suite, and so a change to the procedure is a
reviewable diff instead of a binary blob nobody can read. Re-run after editing:

    python3 scopus/make_tester_manual.py

Every step in here was executed against the real bench before it was written
down; the "Expected" text is what the hardware actually produced, not what it
ought to produce.
"""
import datetime
import pathlib
import sys

try:
    from docx import Document
    from docx.enum.text import WD_ALIGN_PARAGRAPH
    from docx.shared import Pt, RGBColor, Inches
except ImportError:
    sys.exit("python-docx is required:  pip install python-docx")

OUT = pathlib.Path(__file__).resolve().parent / "Scopus_Tester_Manual.docx"

MONO = "Consolas"
GREY = RGBColor(0x44, 0x44, 0x44)
RED = RGBColor(0xB0, 0x00, 0x00)


def code(doc, text):
    """A command / output block."""
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Inches(0.3)
    p.paragraph_format.space_after = Pt(4)
    r = p.add_run(text)
    r.font.name = MONO
    r.font.size = Pt(9)
    return p


def note(doc, text, warn=False):
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Inches(0.3)
    r = p.add_run(text)
    r.font.size = Pt(9)
    r.font.italic = True
    r.font.color.rgb = RED if warn else GREY
    return p


def build():
    doc = Document()
    st = doc.styles["Normal"]
    st.font.name = "Calibri"
    st.font.size = Pt(10.5)

    doc.add_heading("Scopus — Tester Manual", 0)
    p = doc.add_paragraph()
    p.add_run("End-to-end manual test: camera detects people → modem sends the "
              "event → your PC receives it.").bold = True
    doc.add_paragraph(
        f"Generated {datetime.date.today().isoformat()} from "
        f"scopus/make_tester_manual.py. Every step below was executed against "
        f"the bench hardware; the expected output is what the devices actually "
        f"produced.")

    # ── What you are testing ───────────────────────────────────────────
    doc.add_heading("1. What this test proves", level=1)
    doc.add_paragraph(
        "The Scopus system is two devices: an N6Cam (camera + neural network) "
        "and a WP76 modem, joined by an internal UART. Each half can look "
        "perfectly healthy while the product does nothing, because nothing "
        "joins them. This procedure follows one real event all the way out:")
    code(doc,
         "  inject an image with 3 people\n"
         "      -> camera runs inference and detects them\n"
         "          -> camera notifies the modem over the internal UART\n"
         "              -> modem sends a UDP datagram to your PC\n"
         "  photo capture\n"
         "      -> camera sends the JPEG to the modem\n"
         "          -> modem uploads it to your PC over HTTP")
    doc.add_paragraph(
        "You will end the test with a JSON event and a JPEG file on your PC "
        "that came off the device. That is the pass condition — not a device "
        "log line saying it sent them.")

    # ── Equipment ──────────────────────────────────────────────────────
    doc.add_heading("2. What you need", level=1)
    t = doc.add_table(rows=1, cols=2)
    t.style = "Light Grid Accent 1"
    t.rows[0].cells[0].text = "Item"
    t.rows[0].cells[1].text = "Detail"
    for a, b in [
        ("PC", "Linux, on the same Ethernet subnet as the modem "
               "(the bench PC is 192.168.2.3)"),
        ("N6Cam", "USB to the PC. Shell appears as the '-if02' CDC port"),
        ("WP76 modem", "USB (AT on /dev/ttyUSB0) + Ethernet to the PC "
                       "(192.168.2.2)"),
        ("Internal link", "N6Cam UART wired to the modem — this is the link "
                          "under test"),
        ("Repo", "edgeai checked out; run everything from its root"),
    ]:
        row = t.add_row().cells
        row[0].text = a
        row[1].text = b

    doc.add_paragraph()
    doc.add_paragraph("Find the camera's port — it changes after a reflash, so "
                      "always resolve the by-id link rather than assuming "
                      "/dev/ttyACM1:")
    code(doc, "$ readlink -f /dev/serial/by-id/"
              "usb-STMicroelectronics_N6Cam_DEADBEEF-if02\n"
              "/dev/ttyACM2")
    code(doc, "$ export CAM=$(readlink -f /dev/serial/by-id/"
              "usb-STMicroelectronics_N6Cam_DEADBEEF-if02)")

    # ── Step 1 ─────────────────────────────────────────────────────────
    doc.add_heading("Step 1 — Start the server on your PC", level=1)
    doc.add_paragraph(
        "This is the 'server' the product uploads to. It listens for both "
        "things the device sends: notifications (UDP) and photos (HTTP). "
        "Leave it running in its own terminal for the whole test.")
    code(doc, "$ python3 scopus/test_server.py \\\n"
              "      --http-port 8080 --udp-port 9999 \\\n"
              "      --dir ~/scopus-received --from-modem 192.168.2.2")
    doc.add_paragraph("Expected:")
    code(doc, "Scopus test server\n"
              "  receiving into /home/user/scopus-received\n"
              "[17:39:05] listening      UDP  0.0.0.0:9999  (notifications)\n"
              "[17:39:05] listening      HTTP 0.0.0.0:8080 (photo uploads)")
    note(doc, "--from-modem makes the server ignore anything not from the "
              "modem. Unrelated traffic on the notification port has been "
              "mistaken for a passing test before.")

    # ── Step 2 ─────────────────────────────────────────────────────────
    doc.add_heading("Step 2 — Point the modem at your PC", level=1)
    doc.add_paragraph(
        "Connect to the modem's AT channel (FTDI host UART, /dev/ttyUSB0 at "
        "115200 8N1). Any terminal will do; the commands matter, not the tool.")
    code(doc, "$ python3 -c \"\n"
              "import sys; sys.path.insert(0,'scopus/lib')\n"
              "from devices import ModemAt\n"
              "at = ModemAt(); at.prime()\n"
              "for c in ['AT+SDVRNTFHOST=\\\"192.168.2.3\\\"',\n"
              "          'AT+SDVRNTFPORT=9999',\n"
              "          'AT+SDVRHOSTIP=\\\"192.168.2.3\\\"',\n"
              "          'AT+SDVRPORT=8080',\n"
              "          'AT+SDVRSRVRPATH=\\\"/upload\\\"']:\n"
              "    print(c, '->', at.send(c, 4.0).strip())\n"
              "print(at.send('AT+SDVRSRVGET', 4.0))\"")
    doc.add_paragraph("What each one sets:")
    t = doc.add_table(rows=1, cols=2)
    t.style = "Light Grid Accent 1"
    t.rows[0].cells[0].text = "Command"
    t.rows[0].cells[1].text = "Meaning"
    for a, b in [
        ('AT+SDVRNTFHOST="192.168.2.3"', "where notifications go (your PC)"),
        ("AT+SDVRNTFPORT=9999", "notification UDP port"),
        ('AT+SDVRHOSTIP="192.168.2.3"', "where photos go (your PC)"),
        ("AT+SDVRPORT=8080", "photo upload HTTP port"),
        ('AT+SDVRSRVRPATH="/upload"', "URL path for the upload POST"),
    ]:
        row = t.add_row().cells
        row[0].text = a
        row[1].text = b
    doc.add_paragraph()
    doc.add_paragraph("Expected — every command answers OK, and the read-back "
                      "shows what you set:")
    code(doc, '+SDVRSRVGET:"192.168.2.3","",8080,"/upload",http\n\nOK')
    note(doc, 'Quote the IP. The modem\'s AT parser rejects a bare dotted '
              'address: AT+SDVRHOSTIP=192.168.2.3 fails, '
              'AT+SDVRHOSTIP="192.168.2.3" works.', warn=True)

    # ── Step 3 ─────────────────────────────────────────────────────────
    doc.add_heading("Step 3 — Configure the camera to detect people", level=1)
    doc.add_paragraph("Open the camera shell on $CAM and send:")
    code(doc, "mdm AT\n"
              "detect profile 0x01 0x03\n"
              "notify enable 0xff\n"
              "detect start")
    t = doc.add_table(rows=1, cols=2)
    t.style = "Light Grid Accent 1"
    t.rows[0].cells[0].text = "Command"
    t.rows[0].cells[1].text = "Meaning"
    for a, b in [
        ("mdm AT", "wakes and proves the camera→modem link before you rely "
                   "on it"),
        ("detect profile 0x01 0x03", "detect people (0x01); on detection both "
                                     "save to SD and report (0x03). Bit 1 is "
                                     "what makes it notify."),
        ("notify enable 0xff", "enable all notification reasons"),
        ("detect start", "start inference"),
    ]:
        row = t.add_row().cells
        row[0].text = a
        row[1].text = b
    doc.add_paragraph()
    doc.add_paragraph("Expected:")
    code(doc, "mdm AT\nOK\nmdm AT ok\n\n"
              "detect profile: det_msk=0x01 action_msk=0x03\n"
              "notify enable: 0x000000ff\n"
              "detect: started")
    note(doc, "If 'mdm AT' does not answer OK, stop here — the camera cannot "
              "reach the modem and nothing downstream can work. Check the "
              "internal UART wiring before continuing.", warn=True)

    # ── Step 4 ─────────────────────────────────────────────────────────
    doc.add_heading("Step 4 — Inject a picture of people", level=1)
    doc.add_paragraph(
        "Rather than pointing the lens at real people, push a known image "
        "into the camera's inference buffer. The neural network cannot tell "
        "the difference, and the people count is known in advance, so the "
        "result is checkable.")
    code(doc, "$ python3 n6cam-inject-frame.py images/3_people.jpg $CAM")
    doc.add_paragraph("Expected:")
    code(doc, "Frame: 256x256 RGB888 (196608 bytes, CRC32 0x13a92fb1)\n"
              "frame upload: ok (196608 bytes, CRC 0x13a92fb1)\n"
              "Uploaded. Next:  > frame run")

    # ── Step 5 ─────────────────────────────────────────────────────────
    doc.add_heading("Step 5 — Run inference and watch the event leave", level=1)
    doc.add_paragraph("In the camera shell:")
    code(doc, "frame run")
    doc.add_paragraph("Expected on the camera — three people, and the "
                      "notification it raised:")
    code(doc, "frame run: 3 detection(s), NN 89.1ms\n"
              "  [0] class=0 conf=0.78 bbox=(0.73,0.71,0.18,0.44)\n"
              "  [1] class=0 conf=0.71 bbox=(0.51,0.64,0.15,0.60)\n"
              "  [2] class=0 conf=0.84 bbox=(0.36,0.73,0.14,0.41)\n"
              '+SDVRNTF: {"ser":4194336,"num":1,"rsn":16,"rsd":3,...}')
    doc.add_paragraph("Expected in the server terminal, within a second or two:")
    code(doc, "[17:44:06] NOTIFICATION   from 192.168.2.2  "
              "ser=4194336 num=1 rsn=16 rsd=3\n"
              "[17:44:06]                  valid JSON, 9 fields: "
              '{"ser":4194336,"num":1,"rsn":16,\n'
              '                             "rsd":3,"tim":"20000101000116",'
              '"mtn":0,"mod":"","bat":0.0,"vol":0.0}')
    doc.add_paragraph("Check three things, in this order:")
    for a in ["rsn=16 — reason 'people detected'.",
              "rsd=3 — three of them, matching the image and the camera's "
              "own count.",
              "'valid JSON' — the payload parsed. This is not cosmetic: the "
              "notification body is JSON, and JSON does not survive an AT "
              "command line unaided, so a broken transport shows up here as "
              "a datagram that arrives but will not parse."]:
        doc.add_paragraph(a, style="List Bullet")
    note(doc, "PASS for this step = a datagram on the PC with rsn=16, rsd=3, "
              "and valid JSON. The camera printing +SDVRNTF is NOT enough — "
              "that only proves the camera spoke, not that anything heard it.")

    # ── Step 6 ─────────────────────────────────────────────────────────
    doc.add_heading("Step 6 — Capture a photo and upload it", level=1)
    doc.add_paragraph("In the camera shell:")
    code(doc, "photo upload")
    doc.add_paragraph("Expected on the camera:")
    code(doc, "photo upload: capturing -> SDVR+SENDBIN ref=2 "
              "name=4194336_01012000_000123.rdy\n"
              '+SDVRNTF: {"ser":4194336,"num":2,"rsn":64,"rsd":2,...}')
    doc.add_paragraph(
        "The JPEG is ~95 KB and crosses the internal UART at 115200 baud, so "
        "allow about 10 seconds, then another second or two for the upload.")
    doc.add_paragraph("Expected in the server terminal:")
    code(doc, "[17:44:27] UPLOAD         from 192.168.2.2  94831 bytes  "
              "-> 174427_photo\n"
              "[17:44:27]                  JPEG, complete   path=/upload\n"
              "[17:44:27]                  X-Filename: photo\n"
              "[17:44:27]                  X-Filesize: 94831\n"
              "[17:44:27]                  X-Timestamp: 01012000000123\n"
              "[17:44:27]                  X-Ref: 2")
    note(doc, "'JPEG, complete' means the file starts with the JPEG "
              "start-of-image marker and contains the end-of-image marker — "
              "i.e. the whole picture arrived, not a truncated transfer that "
              "still POSTed happily.")

    # ── Step 7 ─────────────────────────────────────────────────────────
    doc.add_heading("Step 7 — Verify what landed on the PC", level=1)
    code(doc, "$ ls -la ~/scopus-received/\n"
              "-rw-rw-r-- 1 user user 94831 174427_photo\n"
              "-rw-rw-r-- 1 user user   372 notifications.log\n\n"
              "$ file ~/scopus-received/174427_photo\n"
              "JPEG image data, baseline, precision 8, 800x600, components 3\n\n"
              "$ cat ~/scopus-received/notifications.log")
    doc.add_paragraph("Open the JPEG in an image viewer. It is the scene the "
                      "camera is pointing at — the real lens image, not the "
                      "injected test frame, which only drives inference.")

    # ── Pass/fail ──────────────────────────────────────────────────────
    doc.add_heading("8. Pass criteria", level=1)
    t = doc.add_table(rows=1, cols=3)
    t.style = "Light Grid Accent 1"
    hdr = t.rows[0].cells
    hdr[0].text = "#"
    hdr[1].text = "Must be true"
    hdr[2].text = "Where you see it"
    for a, b, c in [
        ("1", "Modem answers AT+SDVRSRVGET with the endpoints you set",
         "modem AT channel"),
        ("2", "'mdm AT' returns OK", "camera shell"),
        ("3", "Inference reports 3 detections on 3_people.jpg", "camera shell"),
        ("4", "A datagram arrives with rsn=16 and rsd=3", "server terminal"),
        ("5", "That datagram is valid JSON with all 9 SoW §6 fields",
         "server terminal"),
        ("6", "Each event arrives EXACTLY ONCE — no duplicates",
         "notifications.log"),
        ("7", "A photo arrives and is reported 'JPEG, complete'",
         "server terminal"),
        ("8", "The saved file opens as a JPEG", "file / image viewer"),
    ]:
        row = t.add_row().cells
        row[0].text = a
        row[1].text = b
        row[2].text = c
    doc.add_paragraph()
    doc.add_paragraph("On Ctrl-C the server prints a summary:")
    code(doc, "Summary\n"
              "  notifications: 3 received, 3 valid JSON\n"
              "  uploads:       2 received, 2 complete JPEGs")

    # ── Troubleshooting ────────────────────────────────────────────────
    doc.add_heading("9. If something fails", level=1)
    t = doc.add_table(rows=1, cols=2)
    t.style = "Light Grid Accent 1"
    t.rows[0].cells[0].text = "Symptom"
    t.rows[0].cells[1].text = "Meaning / what to do"
    for a, b in [
        ("stty: /dev/ttyACMx: No such file or directory",
         "The camera re-enumerated (it does this after a firmware update). "
         "Wait ~30 s and re-resolve $CAM from the by-id link."),
        ("'mdm AT' does not answer",
         "The camera→modem UART is down. Everything downstream depends on it; "
         "fix this first. 'mdm stats' shows whether bytes are reaching the "
         "camera at all."),
        ("No upload banner from kit",
         "Something wrote to the camera shell between the command and its "
         "reply. Send 'frame clear', wait a moment, retry."),
        ("Notification arrives but is NOT valid JSON",
         "The AT transport is mangling the payload. Check the modem reports "
         "1.6.0 or later (AT+SDVRVER) — earlier builds could not carry JSON."),
        ("Nothing arrives at the server at all",
         "Check the server is actually running and bound (ss -tlnp | grep "
         "8080), and that AT+SDVRNTFHOST matches this PC's address on the "
         "modem subnet."),
        ("'photo upload: trigger failed (busy / no modem)'",
         "A previous capture is still in flight. Wait ~15 s and retry."),
        ("Photo arrives but is 'TRUNCATED'",
         "The transfer was cut short. Check 'mdm stats' on the camera for "
         "badcrc/stray — a clean link reports zero for both."),
        ("Counter 'ntf: ... unconfirmed=N' is non-zero",
         "The modem did not acknowledge in time. It does NOT mean the event "
         "was lost — the acknowledgement path is lossy while the command "
         "itself usually arrives. Judge by what reached the server."),
    ]:
        row = t.add_row().cells
        row[0].text = a
        row[1].text = b

    # ── Automated equivalent ───────────────────────────────────────────
    doc.add_heading("10. The automated equivalent", level=1)
    doc.add_paragraph(
        "This manual walks the same chain the automated suite asserts. Run it "
        "to check everything at once, including cases that are tedious by "
        "hand:")
    code(doc, "$ python3 scopus/run_integration_tests.py\n"
              "  TOTAL: 47   PASS: 46   FAIL: 0   GAP: 0   SKIP: 1")
    doc.add_paragraph(
        "The one skip is the SD card slot being empty. Use the manual "
        "procedure when you want to see the product work with your own eyes, "
        "or when the suite fails and you need to find out which hop broke.")

    doc.save(OUT)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    build()
