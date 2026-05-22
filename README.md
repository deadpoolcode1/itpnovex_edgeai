# EdgeAI — SIANA N6Cam People-Counter Firmware

Custom firmware for the SIANA N6Cam (STM32N657, IMX335 5MP camera + Neural-Art NPU) per the Scopus PoC SoW. Streams H.264 over USB-C UVC, runs on-device person/vehicle detection, captures JPEGs to SD, and (when wired) tunnels alerts + photos to a WP76 modem over UART.

| Property | Value |
|---|---|
| USB VID:PID | `0483:5740` (UVC 1.50 + CDC-ACM) |
| Resolution | 800×600 H.264 30 fps |
| Camera serial console | `/dev/ttyACM1` @ 115200 8N1 |
| STLink VCP (trace) | `/dev/ttyACM0` @ 115200 8N1 |

## 1. Connect and view the live stream

```bash
sudo apt-get install -y v4l-utils ffmpeg mpv
# Plug the kit's USB-C into the laptop. Wait 3s.

VIDEODEV=/dev/v4l/by-id/usb-STMicroelectronics_N6Cam_*-video-index0
ffmpeg -hide_banner -loglevel warning -f v4l2 -input_format h264 \
       -video_size 800x600 -framerate 30 -i $VIDEODEV \
       -c copy -f h264 - | \
mpv --profile=low-latency --demuxer-lavf-format=h264 -
```

Press `q` to quit. For a still frame instead of live: `… -frames:v 1 -update 1 -y frame.jpg`.

## 2. Talk to the firmware

The kit exposes a CDC-ACM shell on `/dev/ttyACM1`. Use any serial terminal at 115200 8N1, or send one-shot commands:

```bash
N6CAM_TTY=$(readlink -f /dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02)
stty -F $N6CAM_TTY 115200 raw -echo
(timeout 3 cat $N6CAM_TTY) & sleep 0.5
printf "help\n" > $N6CAM_TTY
wait
```

### Shell commands

| Command | What it does |
|---|---|
| `help` | List all commands. |
| `fw` / `hw` / `uid` / `uptime` | Built-ins: firmware version, hardware rev, MCU UID, uptime ms. |
| `version` | Application version string. |
| `reboot` | Soft reboot. |
| `echo on\|off\|query` | Terminal echo + prompt (off = scripted/silent). Persists in flash. |
| `rtc` | Print current RTC time. |
| `rtc set DDMMYYYYHHMMSS` | Set RTC. Example: `rtc set 22052026140000` = 2026-05-22 14:00:00. |
| `camera flip [H\|V\|off]` | Image flip. |
| `camera aec [value\|off]` | Auto-exposure compensation, range `-2.0..2.0`. |
| `camera awb [value\|auto]` | White-balance profile, `0..5`. |
| `camera gain [value]` | Analog gain `0..72000` mdB. |
| `camera exposure [value]` | Shutter `0..33000` µs. |
| `camera brightness [value]` | Brightness `0..100`. |
| `irled on\|off\|query` | IR LED state. (PoC: drives LED_USER3 until real GPIO wired.) |
| `motion sense <0..100> <timeout_s>` / `motion query` | Motion sensitivity + no-motion timeout. Persists. |
| `img size H W` | Photo dimensions, persists. |
| `img quality <1..100>` | JPEG quality, persists. |
| `img color YCBCR\|RGB\|CMYK` | Color space, persists. |
| `img chroma 0\|1` | Chroma subsampling — 0=4:4:4, 1=4:2:2. Persists. |
| `img query` | Print current photo settings. |
| `detect start\|stop` | Run/halt on-device people detection. Persists across reboots. |
| `detect profile <det_msk> <act_msk>` | det_msk bit0=people bit1=vehicles; act_msk bit0=save SD bit1=cellular report. Persists. |
| `detect profile query` | Print current detection profile. |
| `notify enable <mask>` / `disable` | Notification bitmask: `0x1`=NetReg `0x2`=MotStart `0x4`=MotStop `0x8`=Periodic `0x10`=People `0x20`=Vehicle. |
| `notify period <s>` | Periodic-notification interval. |
| `notify trigger <code>` | Emit one notification now with `rsn=code`. |
| `notify query` | Current notify state + message count. |
| `photo savesd` / `photo upload` | Capture JPEG → SD card (`savesd`) or modem (`upload`, needs WP76). Filename `serial_DDMMYYYY_HHMMSS.rdy`. |
| `recovery` | Reboot into FSBL recovery halt (limited use on this kit — debug auth locks SWD in op mode regardless). |
| `update` | Receive new App firmware over CDC and reflash xSPI. Used by `n6cam-update.py` — you won't run this by hand. |

Successful commands respond `<cmd> [<sub>] ok`. Notifications appear on the same port as `+SDVRNTF: {"ser":...,"num":...,"rsn":...,...}` (SoW §6 JSON).

## 3. Build the firmware

Toolchain:

```bash
# STM32CubeIDE 1.18+ (bundles the right ARM GCC 13.3); 1.19 tested
# STM32CubeProgrammer 2.18+ (signing + SWD flash tool); 2.21 tested
# Both installable as native x86_64 from st.com.
# Default paths used below:
CUBEIDE=/opt/st/stm32cubeide_1.19.0
CUBEPROG=$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer
```

The entire SIANA BSP is vendored at `vendor/n6cam.core.bsp/` with our additions — no separate fetch step.

```bash
# 1. Build (~30s first time, ~2s incremental). Outputs Application.bin + .elf.
WS=/tmp/cubeide_ws_n6cam
mkdir -p $WS
$CUBEIDE/stm32cubeide --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data $WS \
  -import vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application \
  -build "Application/Release"

# 2. Sign (STM32N6 rejects unsigned firmware). Outputs Application_signed.bin.
export PATH="$CUBEPROG/bin:$PATH"
STM32_SigningTool_CLI \
  -bin vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application.bin \
  -nk -of 0x80000000 -t fsbl -hv 2.3 -align \
  -o   vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin
```

To build the FSBL too, use `-import vendor/.../STM32CubeIDE/FSBL -build "FSBL/Release"` and sign its output the same way. **You rarely need to** — once the recovery-capable FSBL is in flash, only the Application changes during normal iteration.

## 4. Flash the firmware

### Daily path: USB-C self-update (no SWD, no boot switch)

```bash
./n6cam-update.py vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin
```

The script opens the kit's CDC port (`/dev/ttyACM1` by default), triggers the `update` shell command, streams the signed binary, and the kit reboots into the new firmware. Total ~10s.

### Recovery / FSBL path: SWD via STLink-V3 (boot switch needed)

Only required when changing the FSBL itself, or when the Application is bricked enough that USB-C won't enumerate.

1. Set the kit's boot switch to **development mode**, power-cycle (unplug **all** cables to the kit for ≥2s, replug).
2. Run:
   ```bash
   export PATH="$CUBEPROG/bin:$PATH"
   EL="$CUBEPROG/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"
   STM32_Programmer_CLI -c port=SWD mode=HOTPLUG ap=1 -el "$EL" \
     -w vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/FSBL/Release/FSBL_signed.bin 0x70000000 \
     -w vendor/n6cam.core.bsp/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin 0x70400000 \
     -w vendor/n6cam.core.bsp/Firmware/Model/network_data.hex
   ```
3. Set the boot switch back to **operation mode**, fully power-cycle again.

Why two paths? STM32N6's Debug Authentication locks the SWD access ports while the FSBL is running in op mode, so SWD only works from dev-mode boot. The CDC self-updater is in-application — it writes xSPI flash from inside the running App, bypassing SWD entirely. See `memory/project_n6cam_debug_auth_locked.md` for the full story.

## 5. Troubleshooting

- **`lsusb` shows nothing for the kit after plug-in** — cable is charge-only (no data lines), or you plugged into a power-only port. Try a different USB-C cable / port directly on the laptop, not the dock.
- **`Device or resource busy` on `/dev/videoN`** — another process holds the V4L2 device. UVC is single-reader; close mpv/ffmpeg first.
- **`non-existing PPS 0 referenced` from ffmpeg at start** — H.264 decoder warming up before the first I-frame. Harmless; video starts within ~1s.
- **`/dev/ttyACM1` is permission-denied right after self-update** — race during USB re-enumeration. Wait 5–10s and re-`stty`. Future re-runs are fine.
- **`STM32_Programmer_CLI` says "Cannot connect to access port"** — chip is in op mode (FSBL is running and debug auth has engaged). Flip the boot switch to dev mode and full power-cycle.

## Reference

- **`Scopus_SoW_v3.pdf`** — Scopus PoC system spec (this firmware implements §3.1, 3.4, 3.5, 3.7, 3.8, 4.1–4.5, partial §6).
- **`Kamacode_N6Cam_Proposal_v4.docx`** — WBS-based cost proposal + milestone plan.
- **`SIANA.N6Cam_Using the N6Cam Viewer_250306-1430.pdf`** — vendor viewer GUI (Windows build works; Linux build broken on Ubuntu 24.04 Wayland; use `ffmpeg | mpv` instead).
- **`e2ip EdgeAI_Datasheet.pdf`** — hardware spec, pinout, sensor list.
- **`vendor/n6cam.core.bsp/Documentation/SIANA.N6Cam.*_Schematics-RevB4-PVT.pdf`** — full kit schematics (camera, compute, IO).
- **`memory/`** — long-form engineering notes on debug-auth lock, flash procedure, and the self-update internals.

## Source layout

```
n6cam-update.py            # host-side self-update script (stdlib only)
captures/                  # sample JPEGs from the running kit (gitignored)
vendor/n6cam.core.bsp/     # SIANA BSP snapshot with our additions
  Firmware/
    FSBL/Core/Src/main.c                  # recovery-magic hook
    Application/Core/Src/Tasks/shell_task.c   # all our shell commands
    Application/Core/Src/Tasks/nn_task.c      # detect gate + suspend/resume
    Application/Core/Src/Tasks/snapshot_task.c# JPEG → SD via FileX (vendor)
    Application/Core/Src/fx_app.c             # FileX SD mount + write (vendor)
    Application/Core/Inc/registry.h           # V3 persistent settings
    Model/network_data.hex                    # Neural-Art model weights (PeopleNet)
```

## Vendor links

- Product: https://www.siana-systems.com/n6cam · https://e2ip.com/edge-ai-sensing-kit/
- Source upstream: https://bitbucket.org/sianasystems/n6cam.core.bsp
- Wiki: https://siana-systems.atlassian.net/wiki/spaces/N6Cam
- Vendor support: `n6cam.support@siana-systems.com`
