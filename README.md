# Edge AI Sensing Kit — Quick Start

e2ip / SIANA N6Cam — STM32N657 Edge AI Sensing Kit.

The default out-of-the-box demo is a **live UVC H.264 video stream** from the 5MP IMX335 camera, exposed over USB-C. Plug it in and view the stream.

**Device specs (as seen on host):**

| Property        | Value                                       |
| --------------- | ------------------------------------------- |
| USB VID:PID     | `0483:5740` (STMicroelectronics / N6Cam)    |
| USB class       | UVC 1.50 video + CDC-ACM serial             |
| Resolution      | 800×600                                     |
| Pixel format    | H.264 (Main profile, yuv420p)               |
| Frame rate      | 30 fps                                      |
| Bitrate         | ~3 Mbps                                     |
| Serial console  | `/dev/ttyACM0` @ CDC-ACM                    |

## Requirements

- Linux host (tested on Ubuntu 24.04, kernel 6.17)
- User in `video`, `dialout`, and `plugdev` groups
- USB-C **data** cable (not charge-only)
- Packages: `v4l-utils`, `ffmpeg`, `mpv`

```bash
sudo apt-get install -y v4l-utils ffmpeg mpv
```

## 1. Connect the kit

Plug the kit into the host via the USB-C port on the back of the device. Wait ~3 seconds for enumeration.

Verify enumeration:

```bash
lsusb | grep 0483:5740
#  Bus 003 Device XXX: ID 0483:5740 STMicroelectronics Virtual COM Port

v4l2-ctl --list-devices
#  N6Cam (usb-...):
#    /dev/videoN
#    /dev/videoN+1
#    /dev/mediaN
```

Use the stable by-id path so re-enumeration doesn't break scripts:

```bash
VIDEODEV=/dev/v4l/by-id/usb-STMicroelectronics_N6Cam_*-video-index0
ls -l $VIDEODEV
```

## 2. Run the default demo (live view)

Pipe ffmpeg (which reads the V4L2 H.264 stream cleanly) into mpv:

```bash
ffmpeg -hide_banner -loglevel warning \
  -f v4l2 -input_format h264 -video_size 800x600 -framerate 30 \
  -i $VIDEODEV \
  -c copy -f h264 - | \
mpv --title="N6Cam Live" --profile=low-latency --demuxer-lavf-format=h264 -
```

Press `q` in the mpv window to quit.

## 3. Capture to file (optional)

```bash
# 5-second H.264 clip
ffmpeg -f v4l2 -input_format h264 -video_size 800x600 -framerate 30 \
  -i $VIDEODEV -t 5 -c copy capture.h264

# Extract a JPEG still
ffmpeg -i capture.h264 -frames:v 1 frame.jpg
```

## 4. Firmware serial console (optional)

```bash
sudo usermod -a -G dialout $USER   # once, then re-login
screen /dev/ttyACM0 115200         # or: minicom, picocom
```

## Vendor viewer (reference only)

The vendor-supplied `SIANA-N6Cam-Viewer` auto-detects the kit and is the documented GUI. The **Linux v0.9 binary fails on Ubuntu 24.04 with Wayland + Mesa** (GLFW/GLX FBConfig error — `Glfw Error 65542: No GLXFBConfigs returned`). The viewer's own Windows build works; the CLI invocation docs live in `SIANA.N6Cam_Using the N6Cam Viewer_250306-1430.pdf`. Until the vendor ships a fixed Linux build, use the `ffmpeg | mpv` pipeline above — it streams the same UVC feed.

## Troubleshooting

- **Nothing appears in `lsusb` after plug-in** → cable is charge-only, or the unit is defective. Swap to a known-data USB-C cable; if still nothing, the unit is DOA (we confirmed this failure mode on one of our two units).
- **Brief `error -71` / re-enumeration on plug-in** → seen on the working unit; self-recovers after one power-cycle retry. Harmless.
- **`Device or resource busy`** → another process is already holding the V4L2 device. V4L2 is single-reader; close the first viewer first.
- **`non-existing PPS 0 referenced` at start** → H.264 decoder warming up before the first I-frame. Ignore; video starts within ~1s.
- **No `/dev/video*` node for N6Cam** → check `dmesg | tail -20` right after plug-in. Expected lines include `Product: N6Cam` and `Found UVC 1.50 device N6Cam (0483:5740)`.

## Firmware serial CLI

The kit firmware exposes a command shell on `/dev/ttyACM0` (115200 8N1). Useful commands:

```text
help                  — list all commands
fw                    — firmware version
hw                    — hardware revision
uid                   — MCU unique ID
uptime                — MCU uptime (ms)
reboot                — soft reboot

camera help           — list camera sub-commands
camera flip   [H|V|off]
camera aec    [value|off]   — auto-exposure compensation, -2.0..2.0
camera awb    [value|auto]  — white-balance profile, 0..5
camera gain   [value]       — analog gain, 0..72000 mdB
camera exposure [value]     — shutter, 0..33000 µs
```

Note: the firmware does **not** expose focus / sharpness / zoom controls. The IMX335 sensor has no lens actuator; focus is set mechanically by rotating the M12 lens barrel (the vendor thread-locks it at the factory — see SIANA wiki "Lens Info" page).

## Building the firmware from source

Source: [`bitbucket.org/sianasystems/n6cam.core.bsp`](https://bitbucket.org/sianasystems/n6cam.core.bsp) (public). Two firmware projects: `FSBL` (First-Stage Boot Loader) and `Application` (People Detection demo + UVC streamer + camera shell).

### Prerequisites

| Tool | Version | Notes |
| --- | --- | --- |
| STM32CubeIDE | 1.18+ (tested 1.19) | brings in the **correct** ARM GCC |
| ARM GCC (bundled) | **13.3.rel1** | bundled with CubeIDE — do NOT use 14.3 (breaks the build per vendor release notes) |
| STM32CubeProgrammer | 2.18+ (tested 2.21) | provides `STM32_Programmer_CLI` and `STM32_SigningTool_CLI` |
| STLink-V3 or V3MINIE | — | required to flash (SWD; no USB-DFU procedure is documented) |

Install STM32CubeIDE and STM32CubeProgrammer from st.com (both have native Linux x86_64 installers). Default install locations used below:

- CubeIDE: `/opt/st/stm32cubeide_1.19.0/`
- CubeProgrammer: `~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/`

### Clone the source

```bash
mkdir -p vendor && cd vendor
git clone --depth 1 https://bitbucket.org/sianasystems/n6cam.core.bsp.git
cd n6cam.core.bsp
```

The repo ships pre-built Neural-Art model blobs (`Firmware/Model/network_data.hex`, ~8.3 MB), so you do **not** need the `stedgeai` CLI unless you want to retrain / regenerate the model.

### Build (headless — no GUI needed)

```bash
# Fresh Eclipse workspace
WS=/tmp/cubeide_ws_n6cam
rm -rf "$WS" && mkdir -p "$WS"

REPO=$PWD   # assumes you're in vendor/n6cam.core.bsp

# FSBL (~8 s)
/opt/st/stm32cubeide_1.19.0/stm32cubeide \
  --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "$WS" \
  -import "$REPO/Firmware/STM32CubeIDE/FSBL" \
  -build "FSBL/Release"

# Application (~20 s)
/opt/st/stm32cubeide_1.19.0/stm32cubeide \
  --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "$WS" \
  -import "$REPO/Firmware/STM32CubeIDE/Application" \
  -build "Application/Release"
```

Expected outputs:

```
Firmware/STM32CubeIDE/FSBL/Release/FSBL.bin           ~107 KB
Firmware/STM32CubeIDE/Application/Release/Application.bin   ~673 KB
```

Both projects also produce `.elf`, `.map`, and `.list` files for debugging.

### Sign (mandatory — STM32N6 rejects unsigned firmware)

```bash
export PATH="$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin:$PATH"

# FSBL
STM32_SigningTool_CLI \
  -bin Firmware/STM32CubeIDE/FSBL/Release/FSBL.bin \
  -nk -of 0x80000000 -t fsbl -hv 2.3 -align \
  -o   Firmware/STM32CubeIDE/FSBL/Release/FSBL_signed.bin

# Application
STM32_SigningTool_CLI \
  -bin Firmware/STM32CubeIDE/Application/Release/Application.bin \
  -nk -of 0x80000000 -t fsbl -hv 2.3 -align \
  -o   Firmware/STM32CubeIDE/Application/Release/Application_signed.bin
```

Note: the `-align` flag is required for STM32CubeProgrammer **2.21+**. Omit it for older versions.

### Flash (optional — requires STLink-V3 and SWD wiring)

```bash
export PATH="$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin:$PATH"
EL="$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"

# 1. Set the kit's boot switch to "development mode", power on, connect STLink to SWD.
# 2. Flash FSBL, Application, and model weights to the external xSPI2 flash:

STM32_Programmer_CLI -c port=SWD mode=HOTPLUG ap=1 -el "$EL" \
  -w Firmware/STM32CubeIDE/FSBL/Release/FSBL_signed.bin 0x70000000 \
  -w Firmware/STM32CubeIDE/Application/Release/Application_signed.bin 0x70400000 \
  -w Firmware/Model/network_data.hex

# 3. Set boot switch back to "operation mode", power-cycle the device.
```

See the vendor wiki's [Workstation Setup](https://siana-systems.atlassian.net/wiki/spaces/N6Cam/pages/4223434754/Workstation+Setup) and [Using the BSP (Core)](https://siana-systems.atlassian.net/wiki/spaces/N6Cam/pages/4223139865/Using+the+BSP+Core) pages for the canonical procedure.

### Build alternative: GUI

If you prefer the GUI, open STM32CubeIDE, then `File → Import → General → Existing Projects into Workspace`, point at `Firmware/STM32CubeIDE/`, import the `FSBL` and `Application` projects, and build each in the `Release` configuration.

## Reference docs (in repo)

- `e2ip EdgeAI_Datasheet.pdf` — hardware spec, pinout, sensor list
- `SIANA.N6Cam_Using the N6Cam Viewer_250306-1430.pdf` — vendor viewer usage

## Vendor links

- Product: https://www.siana-systems.com/n6cam · https://e2ip.com/edge-ai-sensing-kit/
- Source: https://bitbucket.org/sianasystems/n6cam.core.bsp
- Wiki: https://siana-systems.atlassian.net/wiki/spaces/N6Cam
- Support: `n6cam.support@siana-systems.com` · `sylvain@siana-systems.com`
