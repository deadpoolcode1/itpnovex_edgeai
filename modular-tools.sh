#!/bin/bash
set -e

# ============================================================
# Kamacode Ltd. | Edge AI Sensing Kit - Modular Tools
# ============================================================
# Usage: ./modular-tools.sh <command> [args]
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Configuration ──────────────────────────────────────────────
PROJECT_ROOT="${SCRIPT_DIR}"
BSP_REPO_URL="https://bitbucket.org/sianasystems/n6cam.core.bsp.git"
BSP_DIR="${PROJECT_ROOT}/vendor/n6cam.core.bsp"
CAPTURES_DIR="${PROJECT_ROOT}/captures"
CUBEIDE_BIN="/opt/st/stm32cubeide_1.19.0/stm32cubeide"
CUBEPROG_BIN_DIR="${HOME}/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin"
CUBEPROG_CLI="${CUBEPROG_BIN_DIR}/STM32_Programmer_CLI"
SIGNING_CLI="${CUBEPROG_BIN_DIR}/STM32_SigningTool_CLI"
EXTERNAL_LOADER="${CUBEPROG_BIN_DIR}/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"
CUBEIDE_WORKSPACE="/tmp/cubeide_ws_n6cam"

# USB device identifiers — stable across re-enumeration
USB_VIDPID="0483:5740"
VIDEODEV_PATTERN="/dev/v4l/by-id/usb-STMicroelectronics_N6Cam_*-video-index0"
SERIAL_CONSOLE="/dev/ttyACM0"

# Default stream parameters (from device's advertised UVC capabilities)
STREAM_FMT="h264"
STREAM_W=800
STREAM_H=600
STREAM_FPS=30

# Flash addresses (from SIANA BSP "Using the BSP (Core)" wiki page)
FSBL_FLASH_ADDR="0x70000000"
APP_FLASH_ADDR="0x70400000"
MODEL_DATA_HEX="${BSP_DIR}/Firmware/Model/network_data.hex"

FSBL_BIN="${BSP_DIR}/Firmware/STM32CubeIDE/FSBL/Release/FSBL.bin"
APP_BIN="${BSP_DIR}/Firmware/STM32CubeIDE/Application/Release/Application.bin"
FSBL_SIGNED="${BSP_DIR}/Firmware/STM32CubeIDE/FSBL/Release/FSBL_signed.bin"
APP_SIGNED="${BSP_DIR}/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin"

# ── Colors ─────────────────────────────────────────────────────
RED=$(tput setaf 1 2>/dev/null || true)
GREEN=$(tput setaf 2 2>/dev/null || true)
YELLOW=$(tput setaf 3 2>/dev/null || true)
CYAN=$(tput setaf 6 2>/dev/null || true)
BOLD=$(tput bold 2>/dev/null || true)
NC=$(tput sgr0 2>/dev/null || true)

info()    { echo "${CYAN}[INFO]${NC} $*"; }
success() { echo "${GREEN}[OK]${NC} $*"; }
warn()    { echo "${YELLOW}[WARN]${NC} $*"; }
error()   { echo "${RED}[ERROR]${NC} $*" >&2; exit 1; }
step()    { echo "${BOLD}${CYAN}==>${NC} ${BOLD}$*${NC}"; }

# ── Helpers ────────────────────────────────────────────────────
require_cmd() {
    command -v "$1" &>/dev/null || error "'$1' not found. Run: ./modular-tools.sh setup"
}

require_bsp() {
    [ -d "${BSP_DIR}" ] || error "BSP not cloned. Run: ./modular-tools.sh build (clones automatically)"
}

require_cubeide() {
    [ -x "${CUBEIDE_BIN}" ] || error "STM32CubeIDE not at ${CUBEIDE_BIN}. Install from st.com or adjust CUBEIDE_BIN in this script."
}

require_cubeprog() {
    [ -x "${CUBEPROG_CLI}" ] || error "STM32CubeProgrammer not at ${CUBEPROG_CLI}. Install from st.com or adjust CUBEPROG_BIN_DIR."
    [ -x "${SIGNING_CLI}" ] || error "STM32_SigningTool_CLI not at ${SIGNING_CLI}."
}

require_signed_artifacts() {
    [ -f "${FSBL_SIGNED}" ] && [ -f "${APP_SIGNED}" ] || \
        error "Signed artifacts missing. Run: ./modular-tools.sh build"
}

resolve_videodev() {
    local dev=$(ls ${VIDEODEV_PATTERN} 2>/dev/null | head -1)
    [ -n "$dev" ] || error "N6Cam not enumerated. Run: ./modular-tools.sh doctor"
    echo "$dev"
}

resolve_videodev_or_empty() {
    ls ${VIDEODEV_PATTERN} 2>/dev/null | head -1
}

timestamp() { date +%Y%m%d_%H%M%S; }

# ── setup ──────────────────────────────────────────────────────
cmd_setup() {
    step "Installing host packages"
    local pkgs=(v4l-utils ffmpeg mpv python3-opencv python3-numpy poppler-utils)
    local missing=()
    for p in "${pkgs[@]}"; do
        dpkg -s "$p" &>/dev/null || missing+=("$p")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        info "Installing: ${missing[*]}"
        sudo apt-get update -qq
        sudo apt-get install -y "${missing[@]}"
    else
        success "All apt packages already installed"
    fi

    step "Verifying user groups (video, dialout, plugdev)"
    local need_groups=()
    for g in video dialout plugdev; do
        id -nG "$USER" | tr ' ' '\n' | grep -qx "$g" || need_groups+=("$g")
    done
    if [ ${#need_groups[@]} -gt 0 ]; then
        warn "User not in groups: ${need_groups[*]}. Adding..."
        for g in "${need_groups[@]}"; do sudo usermod -a -G "$g" "$USER"; done
        warn "You must log out + log in (or reboot) for new groups to take effect."
    else
        success "User in all required groups"
    fi

    step "Checking ST toolchain"
    if [ -x "${CUBEIDE_BIN}" ]; then
        success "STM32CubeIDE: ${CUBEIDE_BIN}"
    else
        warn "STM32CubeIDE not found at ${CUBEIDE_BIN}"
        echo "  Install: https://www.st.com/en/development-tools/stm32cubeide.html"
        echo "  (required version 1.18+, bundles ARM GCC 13.3.rel1)"
    fi
    if [ -x "${CUBEPROG_CLI}" ] && [ -x "${SIGNING_CLI}" ]; then
        success "STM32CubeProgrammer: ${CUBEPROG_BIN_DIR}"
    else
        warn "STM32CubeProgrammer not found at ${CUBEPROG_BIN_DIR}"
        echo "  Install: https://www.st.com/en/development-tools/stm32cubeprog.html (2.18+)"
    fi

    step "Creating captures directory"
    mkdir -p "${CAPTURES_DIR}"
    success "${CAPTURES_DIR}"

    step "Setup complete"
    echo "Next: ./modular-tools.sh doctor   (verify device)"
    echo "      ./modular-tools.sh build    (build firmware)"
    echo "      ./modular-tools.sh demo-view (live video)"
}

# ── Build ──────────────────────────────────────────────────────
cmd_bsp_clone() {
    if [ -d "${BSP_DIR}/.git" ]; then
        info "BSP already cloned at ${BSP_DIR}"
        return 0
    fi
    step "Cloning SIANA N6Cam BSP (~218 MB)"
    mkdir -p "$(dirname "${BSP_DIR}")"
    git clone --depth 1 "${BSP_REPO_URL}" "${BSP_DIR}"
    success "Clone complete"
}

cmd_bsp_update() {
    require_bsp
    step "Pulling latest BSP"
    (cd "${BSP_DIR}" && git pull --depth 1 --ff-only)
}

cmd_build() {
    require_cubeide
    require_cubeprog
    cmd_bsp_clone

    local pristine=false
    [ "${1:-}" = "--pristine" ] && pristine=true

    if $pristine; then
        step "Pristine build — removing workspace + Release dirs"
        rm -rf "${CUBEIDE_WORKSPACE}"
        rm -rf "${BSP_DIR}/Firmware/STM32CubeIDE/FSBL/Release" \
               "${BSP_DIR}/Firmware/STM32CubeIDE/Application/Release"
    fi

    mkdir -p "${CUBEIDE_WORKSPACE}"

    step "Building FSBL/Release (headless CubeIDE)"
    "${CUBEIDE_BIN}" \
        --launcher.suppressErrors -nosplash \
        -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
        -data "${CUBEIDE_WORKSPACE}" \
        -import "${BSP_DIR}/Firmware/STM32CubeIDE/FSBL" \
        -build "FSBL/Release" 2>&1 | tail -5
    [ -f "${FSBL_BIN}" ] || error "FSBL.bin not produced"
    success "FSBL: $(stat -c%s "${FSBL_BIN}") bytes"

    step "Building Application/Release (headless CubeIDE)"
    "${CUBEIDE_BIN}" \
        --launcher.suppressErrors -nosplash \
        -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
        -data "${CUBEIDE_WORKSPACE}" \
        -import "${BSP_DIR}/Firmware/STM32CubeIDE/Application" \
        -build "Application/Release" 2>&1 | tail -5
    [ -f "${APP_BIN}" ] || error "Application.bin not produced"
    success "Application: $(stat -c%s "${APP_BIN}") bytes"

    step "Signing binaries (-hv 2.3 -align, for STM32CubeProgrammer 2.21+)"
    # STM32_SigningTool_CLI writes its output as r--r--r-- and hangs if the target
    # already exists and is not writable. Always remove first.
    rm -f "${FSBL_SIGNED}" "${APP_SIGNED}"
    "${SIGNING_CLI}" -bin "${FSBL_BIN}" -nk -of 0x80000000 -t fsbl -hv 2.3 -align -o "${FSBL_SIGNED}" 2>&1 | tail -2
    [ -f "${FSBL_SIGNED}" ] || error "FSBL signing failed"
    "${SIGNING_CLI}" -bin "${APP_BIN}"  -nk -of 0x80000000 -t fsbl -hv 2.3 -align -o "${APP_SIGNED}"  2>&1 | tail -2
    [ -f "${APP_SIGNED}" ]  || error "Application signing failed"
    success "FSBL signed:        ${FSBL_SIGNED}"
    success "Application signed: ${APP_SIGNED}"

    echo
    step "Build summary"
    ls -la "${FSBL_BIN}" "${FSBL_SIGNED}" "${APP_BIN}" "${APP_SIGNED}" 2>/dev/null | awk '{printf "  %10s  %s\n", $5, $NF}'
    echo
    echo "Next: ./modular-tools.sh flash   (requires STLink-V3 on SWD)"
}

cmd_clean() {
    step "Removing build artifacts"
    rm -rf "${CUBEIDE_WORKSPACE}"
    rm -rf "${BSP_DIR}/Firmware/STM32CubeIDE/FSBL/Release" \
           "${BSP_DIR}/Firmware/STM32CubeIDE/Application/Release" 2>/dev/null || true
    success "Cleaned"
}

# ── Flash ──────────────────────────────────────────────────────
cmd_flash() {
    require_cubeprog
    require_signed_artifacts

    cat <<EOF
${YELLOW}Before flashing, physically prepare the kit:${NC}
  1. Connect STLink-V3 or V3MINIE to the kit's SWD header
  2. Set the boot switch to ${BOLD}"development mode"${NC}
  3. Power on the device
  4. Leave the USB-C cable connected (for power)

EOF
    read -r -p "Ready to flash? [y/N] " ans
    [[ "$ans" =~ ^[Yy]$ ]] || { info "Aborted."; return 0; }

    [ -x "${EXTERNAL_LOADER}" ] || [ -f "${EXTERNAL_LOADER}" ] || \
        error "External loader missing: ${EXTERNAL_LOADER}"

    step "Flashing FSBL -> ${FSBL_FLASH_ADDR}"
    "${CUBEPROG_CLI}" -c port=SWD mode=HOTPLUG ap=1 -el "${EXTERNAL_LOADER}" \
        -w "${FSBL_SIGNED}" "${FSBL_FLASH_ADDR}"

    step "Flashing Application -> ${APP_FLASH_ADDR}"
    "${CUBEPROG_CLI}" -c port=SWD mode=HOTPLUG ap=1 -el "${EXTERNAL_LOADER}" \
        -w "${APP_SIGNED}" "${APP_FLASH_ADDR}"

    if [ -f "${MODEL_DATA_HEX}" ]; then
        step "Flashing model weights (network_data.hex)"
        "${CUBEPROG_CLI}" -c port=SWD mode=HOTPLUG ap=1 -el "${EXTERNAL_LOADER}" \
            -w "${MODEL_DATA_HEX}"
    else
        warn "network_data.hex not found; skipping model flash"
    fi

    echo
    success "Flash complete."
    echo "Next steps:"
    echo "  1. Set the boot switch back to ${BOLD}\"operation mode\"${NC}"
    echo "  2. Power-cycle the device"
    echo "  3. ./modular-tools.sh doctor    (verify FW version)"
    echo "  4. ./modular-tools.sh demo-view (view live AI demo)"
}

cmd_flash_model() {
    require_cubeprog
    [ -f "${MODEL_DATA_HEX}" ] || error "Model data missing: ${MODEL_DATA_HEX} (run ./modular-tools.sh build first)"
    step "Flashing model weights only"
    "${CUBEPROG_CLI}" -c port=SWD mode=HOTPLUG ap=1 -el "${EXTERNAL_LOADER}" \
        -w "${MODEL_DATA_HEX}"
}

# ── Demo: live view + capture ──────────────────────────────────
cmd_demo_view() {
    local dev; dev=$(resolve_videodev)
    local title="N6Cam Live Demo (q to quit)"
    info "Using device: $dev"
    info "Press 'q' in the mpv window (or Ctrl+C here) to quit."
    require_cmd ffmpeg
    require_cmd mpv

    # Notes on the flags:
    #  --hwdec=no                      VA-API fails on pipe-fed H.264 Annex B
    #                                  ("VK_KHR_video_decode_queue not supported")
    #  --no-correct-pts                the stream carries no PTS; don't let mpv
    #                                  try to "correct" them (it treats the first
    #                                  missing PTS as EOF)
    #  --container-fps-override=30     tell mpv the real framerate explicitly
    #  --untimed                       disable mpv's presentation clock entirely
    #  --cache=no                      realtime viewing, no buffering
    ffmpeg -hide_banner -loglevel error \
        -f v4l2 -input_format ${STREAM_FMT} \
        -video_size ${STREAM_W}x${STREAM_H} -framerate ${STREAM_FPS} \
        -i "$dev" \
        -c copy -f h264 - 2>/tmp/n6cam_ffmpeg_view.log | \
    mpv --no-config --title="${title}" \
        --hwdec=no \
        --no-correct-pts --container-fps-override=${STREAM_FPS} \
        --untimed --cache=no \
        --demuxer-lavf-format=h264 - 2>/tmp/n6cam_mpv_view.log || true

    # If mpv exited quickly, surface the log snippets to help the user
    if [ -s /tmp/n6cam_ffmpeg_view.log ] || [ -s /tmp/n6cam_mpv_view.log ]; then
        info "(view closed; tails of logs below — keep for debugging)"
        [ -s /tmp/n6cam_ffmpeg_view.log ] && { echo "-- ffmpeg --"; tail -5 /tmp/n6cam_ffmpeg_view.log; }
        [ -s /tmp/n6cam_mpv_view.log   ] && { echo "-- mpv --";    tail -5 /tmp/n6cam_mpv_view.log; }
    fi
}

cmd_demo_capture() {
    local dev; dev=$(resolve_videodev)
    mkdir -p "${CAPTURES_DIR}"
    local ts=$(timestamp)
    local h264="${CAPTURES_DIR}/n6cam_${ts}.h264"
    local jpg="${CAPTURES_DIR}/n6cam_${ts}.jpg"
    require_cmd ffmpeg

    step "Capturing 3s clip for frame extraction"
    ffmpeg -hide_banner -loglevel error -y \
        -f v4l2 -input_format ${STREAM_FMT} \
        -video_size ${STREAM_W}x${STREAM_H} -framerate ${STREAM_FPS} \
        -i "$dev" -t 3 -c copy "$h264"

    step "Extracting still frame (from 2s in — after AE warmup)"
    ffmpeg -hide_banner -loglevel error -y -i "$h264" -ss 2 -frames:v 1 -q:v 2 "$jpg"
    rm -f "$h264"

    success "Saved: $jpg ($(stat -c%s "$jpg") bytes)"
    echo "$jpg"
}

cmd_demo_record() {
    local dev; dev=$(resolve_videodev)
    local secs="${1:-10}"
    mkdir -p "${CAPTURES_DIR}"
    local ts=$(timestamp)
    local h264="${CAPTURES_DIR}/n6cam_${ts}.h264"
    local mp4="${CAPTURES_DIR}/n6cam_${ts}.mp4"
    require_cmd ffmpeg

    step "Recording ${secs}s H.264 clip"
    ffmpeg -hide_banner -loglevel error -y \
        -f v4l2 -input_format ${STREAM_FMT} \
        -video_size ${STREAM_W}x${STREAM_H} -framerate ${STREAM_FPS} \
        -i "$dev" -t "$secs" -c copy "$h264"

    step "Remuxing to MP4"
    ffmpeg -hide_banner -loglevel error -y -i "$h264" \
        -c copy -bsf:v h264_mp4toannexb -movflags +faststart "$mp4"
    rm -f "$h264"

    success "Saved: $mp4 ($(stat -c%s "$mp4") bytes)"
    echo "$mp4"
}

cmd_demo_sharpness() {
    local jpg="${1:-}"
    [ -z "$jpg" ] && jpg=$(cmd_demo_capture | tail -1)
    [ -f "$jpg" ] || error "File not found: $jpg"
    require_cmd python3
    python3 - "$jpg" <<'PY'
import cv2, sys
img = cv2.imread(sys.argv[1], cv2.IMREAD_GRAYSCALE)
scene = img[50:490, 40:760]  # exclude HUD overlay bands
lv = cv2.Laplacian(scene, cv2.CV_64F).var()
print(f"scene-only Laplacian variance: {lv:.0f}  (3000+ = sharp, <1000 = soft)")
PY
}

cmd_demo_cli() {
    [ -e "${SERIAL_CONSOLE}" ] || error "${SERIAL_CONSOLE} not present. Is the kit connected?"
    info "Opening serial console on ${SERIAL_CONSOLE} @ 115200"
    info "Type 'help' for commands. Ctrl+A then Ctrl+X to exit picocom."
    if command -v picocom &>/dev/null; then
        picocom -b 115200 "${SERIAL_CONSOLE}"
    elif command -v screen &>/dev/null; then
        echo "Ctrl+A then K to kill screen."
        screen "${SERIAL_CONSOLE}" 115200
    else
        error "Install picocom or screen: sudo apt-get install -y picocom"
    fi
}

cmd_demo_fw_version() {
    [ -e "${SERIAL_CONSOLE}" ] || error "${SERIAL_CONSOLE} not present."
    stty -F "${SERIAL_CONSOLE}" 115200 raw -echo 2>/dev/null || true
    (timeout 2 cat "${SERIAL_CONSOLE}" 2>/dev/null) &
    local pid=$!
    sleep 0.3
    printf "fw\r\n"     > "${SERIAL_CONSOLE}" 2>/dev/null; sleep 0.3
    printf "hw\r\n"     > "${SERIAL_CONSOLE}" 2>/dev/null; sleep 0.3
    printf "uid\r\n"    > "${SERIAL_CONSOLE}" 2>/dev/null; sleep 0.3
    printf "uptime\r\n" > "${SERIAL_CONSOLE}" 2>/dev/null; sleep 0.3
    wait $pid 2>/dev/null || true
}

# ── Diagnostics ────────────────────────────────────────────────
cmd_doctor() {
    step "USB enumeration (${USB_VIDPID})"
    if lsusb | grep -q "${USB_VIDPID}"; then
        lsusb | grep "${USB_VIDPID}" | sed 's/^/  /'
        success "N6Cam enumerated"
    else
        error "N6Cam not seen on USB. Is it plugged in and powered?"
    fi

    step "V4L2 video nodes"
    if command -v v4l2-ctl &>/dev/null; then
        v4l2-ctl --list-devices 2>/dev/null | grep -A2 N6Cam | sed 's/^/  /' || warn "N6Cam not in v4l2 list"
    else
        warn "v4l2-ctl not installed — run ./modular-tools.sh setup"
    fi

    step "Stable by-id path"
    local dev; dev=$(resolve_videodev_or_empty)
    if [ -n "$dev" ]; then
        echo "  $dev  ->  $(readlink -f "$dev")"
        success "Available"
    else
        warn "by-id symlink not created yet; check /dev/video*"
    fi

    step "Serial console (${SERIAL_CONSOLE})"
    if [ -e "${SERIAL_CONSOLE}" ]; then
        ls -la "${SERIAL_CONSOLE}" | sed 's/^/  /'
        success "Present"
        info "Firmware info:"
        cmd_demo_fw_version 2>&1 | sed 's/^/  /'
    else
        warn "${SERIAL_CONSOLE} not present"
    fi

    step "Toolchain"
    [ -x "${CUBEIDE_BIN}" ]   && success "CubeIDE: ${CUBEIDE_BIN}"           || warn "CubeIDE missing"
    [ -x "${CUBEPROG_CLI}" ]  && success "CubeProgrammer: ${CUBEPROG_CLI}"   || warn "CubeProgrammer missing"
    [ -d "${BSP_DIR}" ]       && success "BSP cloned: ${BSP_DIR}"            || warn "BSP not cloned (./modular-tools.sh build to fetch)"
    [ -f "${FSBL_SIGNED}" ]   && success "FSBL signed built"                 || warn "FSBL signed binary missing"
    [ -f "${APP_SIGNED}" ]    && success "Application signed built"          || warn "Application signed binary missing"
}

cmd_info() {
    cat <<EOF
${CYAN}Edge AI Sensing Kit${NC}  (e2ip / SIANA N6Cam)

${YELLOW}Hardware${NC}
  MCU              STM32N657 (ARM Cortex-M55 + Neural-ART NPU, 600 GOPS)
  Camera sensor    Sony IMX335 (5MP) + M12 lens (manual focus)
  Connectivity     USB-C (UVC + CDC-ACM), WiFi (dual-band)

${YELLOW}USB interface${NC}
  VID:PID          ${USB_VIDPID}
  UVC format       ${STREAM_FMT}  @  ${STREAM_W}x${STREAM_H}  ${STREAM_FPS} fps
  Serial console   ${SERIAL_CONSOLE}  @  115200 8N1

${YELLOW}Firmware${NC}
  Source (public)  ${BSP_REPO_URL}
  Build tool       STM32CubeIDE 1.19 (headless) + bundled arm-none-eabi-gcc 13.3.rel1
  Flash tool       STM32_Programmer_CLI via STLink-V3 (SWD)
  Signing          STM32_SigningTool_CLI (-hv 2.3 -align)

${YELLOW}Flash layout${NC}
  FSBL          @ ${FSBL_FLASH_ADDR}
  Application   @ ${APP_FLASH_ADDR}
  Model weights @ 0x70600000 (via network_data.hex)

${YELLOW}Project paths${NC}
  BSP clone     ${BSP_DIR}
  Workspace     ${CUBEIDE_WORKSPACE}
  Captures      ${CAPTURES_DIR}

${YELLOW}Vendor links${NC}
  Product       https://e2ip.com/edge-ai-sensing-kit/
  Source        ${BSP_REPO_URL}
  Wiki          https://siana-systems.atlassian.net/wiki/spaces/N6Cam
  Support       n6cam.support@siana-systems.com
EOF
}

# ── Help ───────────────────────────────────────────────────────
cmd_update() {
    # Self-update via CDC — no SWD, no boot switch. Wraps n6cam-update.py.
    # Usage:
    #   update             — push signed Application_signed.bin (default)
    #   update app         — same
    #   update model [path]— push NN weights (default: vendor/.../Model/network_data.hex)
    local TARGET="app"
    local IMAGE=""
    if [ "${1:-}" = "app" ] || [ "${1:-}" = "model" ]; then
        TARGET="$1"; shift
        IMAGE="${1:-}"
    fi
    local TTY
    TTY=$(readlink -f /dev/serial/by-id/usb-STMicroelectronics_N6Cam_*-if02 2>/dev/null)
    [ -n "${TTY}" ] || error "N6Cam CDC port not found"

    if [ "${TARGET}" = "model" ]; then
        # Default to the vendor's network_data.hex if no path given.
        [ -n "${IMAGE}" ] || IMAGE="${BSP_DIR}/Firmware/Model/network_data.hex"
        [ -f "${IMAGE}" ] || error "Model file not found: ${IMAGE}"
        step "Pushing model ${IMAGE##*/} via CDC self-updater (target=model)"
        python3 "${PROJECT_ROOT}/n6cam-update.py" --target model "${IMAGE}" "${TTY}"
        info "Wait ~90s for the kit to erase ~MB + write + reboot."
    else
        local APP_SIGNED="${BSP_DIR}/Firmware/STM32CubeIDE/Application/Release/Application_signed.bin"
        [ -f "${APP_SIGNED}" ] || error "Signed Application not built. Run: ./modular-tools.sh build"
        step "Pushing ${APP_SIGNED##*/} via CDC self-updater (target=app)"
        python3 "${PROJECT_ROOT}/n6cam-update.py" --target app "${APP_SIGNED}" "${TTY}"
        info "Wait ~15s for the kit to flash + reboot before sending more commands."
    fi
}

cmd_test() {
    # Comprehensive UART/SD/NN test suite + HTML report.
    local TTY IMAGES OUT
    TTY=""
    IMAGES="${PROJECT_ROOT}/tests/images"
    OUT=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --tty)    TTY="$2"; shift 2 ;;
            --images) IMAGES="$2"; shift 2 ;;
            --out)    OUT="$2"; shift 2 ;;
            *) error "Unknown test option: $1" ;;
        esac
    done

    step "Running N6Cam test suite"
    mkdir -p "${PROJECT_ROOT}/results"
    local ARGS=()
    [ -n "${TTY}" ] && ARGS+=(--tty "${TTY}")
    [ -n "${IMAGES}" ] && ARGS+=(--images "${IMAGES}")
    [ -n "${OUT}" ] && ARGS+=(--out "${OUT}")

    set +e
    python3 "${PROJECT_ROOT}/tests/run_tests.py" "${ARGS[@]}"
    local rc=$?
    set -e

    # Generate PDF alongside HTML if a converter is available
    local LATEST_HTML
    LATEST_HTML=$(ls -1t "${PROJECT_ROOT}/results"/test-report-*.html 2>/dev/null | head -1)
    if [ -n "${LATEST_HTML}" ]; then
        local PDF="${LATEST_HTML%.html}.pdf"
        if command -v wkhtmltopdf &>/dev/null; then
            wkhtmltopdf --quiet "${LATEST_HTML}" "${PDF}" 2>/dev/null && info "PDF: ${PDF}"
        elif command -v google-chrome &>/dev/null || command -v chromium-browser &>/dev/null; then
            local CHROME=$(command -v google-chrome || command -v chromium-browser)
            "${CHROME}" --headless --disable-gpu --print-to-pdf="${PDF}" \
                "file://${LATEST_HTML}" >/dev/null 2>&1 && info "PDF: ${PDF}"
        fi
    fi
    exit ${rc}
}

cmd_help() {
    cat <<EOF
${CYAN}Kamacode Ltd. | Edge AI Sensing Kit - Modular Tools${NC}
=====================================================
Usage: ./modular-tools.sh <command> [args]

${YELLOW}First-time setup:${NC}
  setup                 Install apt deps (v4l-utils ffmpeg mpv opencv), add user to
                        video/dialout/plugdev groups, create captures/, check ST tools.

${YELLOW}Firmware — build & flash:${NC}
  build                 Clone BSP (if needed) + headless CubeIDE build of FSBL +
                        Application + sign both with -hv 2.3 -align.
  build --pristine      Clean workspace + Release dirs before building.
  clean                 Remove build artifacts + CubeIDE workspace.
  flash                 Flash signed FSBL + Application + model weights via
                        STLink-V3 SWD. Prompts for confirmation. Requires
                        boot switch in "development mode".
  flash-model           Flash just the neural-network weights (network_data.hex).
  bsp-clone             Clone the BSP source only (no build).
  bsp-update            git pull latest BSP.

${YELLOW}Demo — live video & capture:${NC}
  demo-view             Open live video window (ffmpeg | mpv pipeline).
                        Press 'q' in the mpv window to quit.
  demo-capture          Grab a single high-quality JPEG still to captures/.
  demo-record [sec]     Record an MP4 clip (default 10 s) to captures/.
  demo-sharpness [jpg]  Measure scene sharpness (Laplacian variance) of a capture.
                        If no file passed, captures a fresh frame first.
  demo-cli              Open firmware serial console (picocom @ 115200).
  demo-fw-version       One-shot query of fw/hw/uid/uptime over serial.

${YELLOW}Daily firmware iteration:${NC}
  update [app]          Push the signed Application via the kit's CDC
                        self-updater (no SWD, no boot switch).
  update model [PATH]   Push the NN weights (default: vendor/.../Model/
                        network_data.hex) via CDC. Same path — no SWD.

${YELLOW}Tests:${NC}
  test                  Run the full N6Cam test suite (UART shell, SD,
                        notifications, frame injection, NN algorithm,
                        performance). Emits an HTML + PDF report under
                        results/ similar to the V20_SDVR format.
                        Options: --tty PATH --images DIR --out FILE

${YELLOW}Diagnostics:${NC}
  doctor                Check USB enumeration, v4l2 nodes, serial console,
                        firmware versions, and installed tools. Run this
                        FIRST if anything misbehaves.
  info                  Print kit specs + paths + URLs.
  help                  This help.

${CYAN}Typical flows${NC}

  First-time bring-up:
    ./modular-tools.sh setup
    # (plug device in)
    ./modular-tools.sh doctor
    ./modular-tools.sh demo-view

  Rebuild firmware from source:
    ./modular-tools.sh build
    ./modular-tools.sh flash          # requires STLink-V3 on SWD
    ./modular-tools.sh demo-view

  Capture an image for a client:
    ./modular-tools.sh demo-capture
    ./modular-tools.sh demo-sharpness captures/n6cam_*.jpg

${CYAN}Key paths${NC}
  BSP:         ${BSP_DIR}
  Captures:    ${CAPTURES_DIR}
  CubeIDE:     ${CUBEIDE_BIN}
  CubeProg:    ${CUBEPROG_BIN_DIR}

${CYAN}Reference docs (in repo)${NC}
  e2ip EdgeAI_Datasheet.pdf
  SIANA.N6Cam_Using the N6Cam Viewer_250306-1430.pdf
  README.md
EOF
}

# ── Dispatch ───────────────────────────────────────────────────
case "${1:-help}" in
    setup)              shift; cmd_setup "$@" ;;
    build)              shift; cmd_build "$@" ;;
    clean)              shift; cmd_clean "$@" ;;
    bsp-clone)          shift; cmd_bsp_clone "$@" ;;
    bsp-update)         shift; cmd_bsp_update "$@" ;;
    flash)              shift; cmd_flash "$@" ;;
    flash-model)        shift; cmd_flash_model "$@" ;;
    demo-view)          shift; cmd_demo_view "$@" ;;
    demo-capture)       shift; cmd_demo_capture "$@" ;;
    demo-record)        shift; cmd_demo_record "$@" ;;
    demo-sharpness)     shift; cmd_demo_sharpness "$@" ;;
    demo-cli)           shift; cmd_demo_cli "$@" ;;
    demo-fw-version)    shift; cmd_demo_fw_version "$@" ;;
    update)             shift; cmd_update "$@" ;;
    test)               shift; cmd_test "$@" ;;
    doctor)             shift; cmd_doctor "$@" ;;
    info)               shift; cmd_info "$@" ;;
    help|--help|-h|"")  cmd_help ;;
    *)
        echo "${RED}Error: Unknown command '${1}'${NC}"; echo ""; cmd_help; exit 1 ;;
esac
