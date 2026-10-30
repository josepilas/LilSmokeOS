#!/bin/bash
# Capture one complete LilSmokeOS theme/game flow from the MIPS QEMU target.

set -Eeuo pipefail

if [ "$#" -lt 6 ] || [ "$#" -gt 7 ]; then
    echo "usage: $0 <images-dir> <storage-dir> <persist-dir> <output-dir> <theme-name> <slug> [display-number]" >&2
    exit 2
fi

IMAGES_DIR="$(realpath "$1")"
STORAGE_DIR="$(realpath "$2")"
PERSIST_DIR="$(realpath -m "$3")"
OUTPUT_DIR="$(realpath -m "$4")"
THEME_NAME="$5"
SLUG="$6"
DISPLAY_NUMBER="${7:-99}"
DISPLAY_NAME=":$DISPLAY_NUMBER"
WIDTH=854
GUEST_WIDTH=856
HEIGHT=480
FPS=30

KERNEL="$IMAGES_DIR/vmlinux"
INITRAMFS="$IMAGES_DIR/rootfs.cpio.gz"
RUN_DIR="$(mktemp -d "/tmp/lilsmoke-${SLUG}-XXXXXX")"
SERIAL_LOG="$RUN_DIR/serial.log"
MONITOR_SOCKET="$RUN_DIR/qemu-monitor.sock"
QEMU_AUDIO="$RUN_DIR/guest-audio.wav"
RAW_VIDEO="$RUN_DIR/raw-video.webm"
PROBE_IMAGE="$RUN_DIR/probe.png"
WINDOW_NAME="LilSmokeOS QA - $SLUG"

XVFB_PID=""
QEMU_PID=""
FFMPEG_PID=""
WINDOW_ID=""

fail() {
    echo "capture_theme_vm: $*" >&2
    if [ -n "${OUTPUT_DIR:-}" ] && [ -s "${PROBE_IMAGE:-}" ]; then
        mkdir -p "$OUTPUT_DIR/logs"
        cp "$PROBE_IMAGE" "$OUTPUT_DIR/logs/failure-last-frame.png" 2>/dev/null || true
    fi
    if [ -s "$SERIAL_LOG" ]; then
        echo "--- guest serial tail ---" >&2
        tail -n 100 "$SERIAL_LOG" >&2 || true
    fi
    exit 1
}

stop_process() {
    pid="$1"
    signal="${2:-TERM}"
    [ -n "$pid" ] || return 0
    kill -"$signal" "$pid" 2>/dev/null || true
    for _ in $(seq 1 40); do
        if ! kill -0 "$pid" 2>/dev/null; then
            wait "$pid" 2>/dev/null || true
            return 0
        fi
        case "$(ps -o stat= -p "$pid" 2>/dev/null || true)" in
            Z*) wait "$pid" 2>/dev/null || true; return 0 ;;
        esac
        sleep 0.1
    done
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

cleanup() {
    stop_process "$FFMPEG_PID" INT
    stop_process "$QEMU_PID" TERM
    stop_process "$XVFB_PID" TERM
}
trap cleanup EXIT INT TERM

for tool in qemu-system-mipsel Xvfb xdotool nc ffmpeg ffprobe identify convert compare awk realpath; do
    command -v "$tool" >/dev/null 2>&1 || fail "required host tool is missing: $tool"
done
[ -f "$KERNEL" ] || fail "kernel not found: $KERNEL"
[ -f "$INITRAMFS" ] || fail "initramfs not found: $INITRAMFS"
[ -d "$STORAGE_DIR/themes/$THEME_NAME" ] || fail "theme not found: $STORAGE_DIR/themes/$THEME_NAME"
[ -f "$STORAGE_DIR/roms/snes/Super Mario World (PT-BR) - [snesforever.com.br].7z" ] || \
    fail "legal SNES QA ROM is missing from storage"

mkdir -p "$PERSIST_DIR/config" "$OUTPUT_DIR" "$OUTPUT_DIR/logs"
rm -rf "$PERSIST_DIR/overlay" "$PERSIST_DIR/logs" \
       "$PERSIST_DIR/saves" "$PERSIST_DIR/states"
mkdir -p "$PERSIST_DIR/logs" "$PERSIST_DIR/saves" "$PERSIST_DIR/states"

cat > "$PERSIST_DIR/config/lilsmoke.conf" <<EOF
LILSMOKE_VERSION=0.1.0-j0
DISPLAY_WIDTH=$GUEST_WIDTH
DISPLAY_HEIGHT=$HEIGHT
DISPLAY_ROTATION=0
SCALING_MODE=integer
SHOW_FPS=1
SHOW_CLOCK=0
SHOW_BATTERY=text
BATTERY_PERCENT_OVERRIDE=68
MASTER_VOLUME=70
FONT_PATH=/usr/share/fonts/dejavu/DejaVuSans.ttf
DEFAULT_THEME=$THEME_NAME
THEME_SYSTEM_VIEW=theme-default
THEME_GAMELIST_VIEW=theme-default
SCAN_ROMS_ON_BOOT=0
ASSET_CACHE_MB=16
REWIND_BUFFER_MB=0
ZRAM_ENABLED=on
ZRAM_SIZE_MB=48
CPU_GOVERNOR=ondemand
CPU_OVERCLOCK=stock
EOF

Xvfb "$DISPLAY_NAME" -screen 0 "${GUEST_WIDTH}x${HEIGHT}x24" -nolisten tcp -ac \
    >"$RUN_DIR/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 1
kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb failed to start"

export DISPLAY="$DISPLAY_NAME"
export SDL_VIDEODRIVER=x11
export SDL_AUDIODRIVER=dummy

QEMU_STARTED_MS="$(date +%s%3N)"
qemu-system-mipsel \
    -name "$WINDOW_NAME" \
    -M malta \
    -cpu 74Kf \
    -accel tcg,thread=single \
    -smp 1 \
    -m 256 \
    -kernel "$KERNEL" \
    -initrd "$INITRAMFS" \
    -append "console=ttyS0,115200n8 root=/dev/ram0 rdinit=/init video=cirrusfb:${GUEST_WIDTH}x${HEIGHT}M-24@60" \
    -serial "file:$SERIAL_LOG" \
    -monitor "unix:$MONITOR_SOCKET,server=on,wait=off" \
    -no-reboot \
    -vga cirrus \
    -device usb-kbd \
    -audiodev "wav,id=lilsmoke_audio,path=$QEMU_AUDIO" \
    -device AC97,audiodev=lilsmoke_audio \
    -virtfs "local,path=$STORAGE_DIR,mount_tag=storage,security_model=none" \
    -virtfs "local,path=$PERSIST_DIR,mount_tag=persist,security_model=none" \
    -display sdl,gl=off \
    >"$RUN_DIR/qemu-host.log" 2>&1 &
QEMU_PID=$!

for _ in $(seq 1 120); do
    kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited before creating its display"
    WINDOW_ID="$(xdotool search --onlyvisible --name "$WINDOW_NAME" 2>/dev/null | head -n 1 || true)"
    [ -n "$WINDOW_ID" ] && break
    sleep 0.5
done
[ -n "$WINDOW_ID" ] || fail "QEMU SDL window did not appear"

xdotool windowfocus --sync "$WINDOW_ID" 2>/dev/null || true

# S90 creates the persistent config before BusyBox init starts LilSmokeUI.
# Wait for a non-console frame as well so captures never silently contain boot text.
ready=0
for _ in $(seq 1 180); do
    kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited while waiting for LilSmokeUI"
    ffmpeg -y -nostdin -hide_banner -loglevel error \
        -f x11grab -draw_mouse 0 -video_size "${GUEST_WIDTH}x${HEIGHT}" \
        -i "${DISPLAY_NAME}.0+0,0" -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
        -frames:v 1 "$PROBE_IMAGE" || true
    if grep -q '\[lilsmoke\] S90: pre-flight for LilSmokeUI' "$SERIAL_LOG" 2>/dev/null && \
       grep -q 'ROMScanner:' "$PERSIST_DIR/logs/lilsmoke-ui.log" 2>/dev/null && \
       grep -q 'SystemView: first frame complete' "$PERSIST_DIR/logs/lilsmoke-ui.log" 2>/dev/null && \
       [ -s "$PROBE_IMAGE" ]; then
        colors="$(identify -format '%k' "$PROBE_IMAGE" 2>/dev/null || echo 0)"
        if [ "${colors:-0}" -gt 24 ]; then
            ready=1
            break
        fi
    fi
    sleep 1
done
[ "$ready" -eq 1 ] || fail "LilSmokeUI never produced a rendered theme frame"
if grep -q 'Renderer init failed' "$PERSIST_DIR/logs/lilsmoke-ui.log" 2>/dev/null; then
    fail "LilSmokeUI renderer initialization failed"
fi

sleep 3
CAPTURE_STARTED_MS="$(date +%s%3N)"
ffmpeg -y -nostdin -hide_banner -loglevel warning \
    -f x11grab -draw_mouse 0 -framerate "$FPS" \
        -video_size "${GUEST_WIDTH}x${HEIGHT}" -i "${DISPLAY_NAME}.0+0,0" \
        -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
    -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -row-mt 1 -threads 4 \
    -crf 35 -b:v 0 -pix_fmt yuv420p -an "$RAW_VIDEO" \
    >"$RUN_DIR/ffmpeg.log" 2>&1 &
FFMPEG_PID=$!
sleep 1

screenshot() {
    name="$1"
    ffmpeg -y -nostdin -hide_banner -loglevel error \
        -f x11grab -draw_mouse 0 -video_size "${GUEST_WIDTH}x${HEIGHT}" \
        -i "${DISPLAY_NAME}.0+0,0" -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
        -frames:v 1 "$OUTPUT_DIR/$name"
    [ -s "$OUTPUT_DIR/$name" ] || fail "empty screenshot: $name"
}

wait_for_color_frame() {
    threshold="$1"
    timeout="$2"
    description="$3"
    nudge_key="${4:-}"
    minimum_mean="${5:-0}"
    elapsed=0
    while [ "$elapsed" -lt "$timeout" ]; do
        kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited while waiting for $description"
        ffmpeg -y -nostdin -hide_banner -loglevel error \
            -f x11grab -draw_mouse 0 -video_size "${GUEST_WIDTH}x${HEIGHT}" \
            -i "${DISPLAY_NAME}.0+0,0" -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
            -frames:v 1 "$PROBE_IMAGE" || true
        colors="$(identify -format '%k' "$PROBE_IMAGE" 2>/dev/null || echo 0)"
        mean="$(identify -format '%[fx:mean]' "$PROBE_IMAGE" 2>/dev/null || echo 0)"
        case "$colors" in ''|*[!0-9]*) colors=0 ;; esac
        if [ "$colors" -gt "$threshold" ] && \
           awk -v value="$mean" -v floor="$minimum_mean" \
               'BEGIN { exit !(value > floor) }'; then
            return 0
        fi
        elapsed=$((elapsed + 1))
        if [ -n "$nudge_key" ] && [ $((elapsed % 15)) -eq 0 ]; then
            if [ "$nudge_key" = "game-buttons" ]; then
                case $(((elapsed / 15) % 5)) in
                    0) current_nudge=ctrl-right ;;
                    1) current_nudge=alt-right ;;
                    2) current_nudge=spc ;;
                    3) current_nudge=shift ;;
                    *) current_nudge=ret ;;
                esac
                send_guest_key "$current_nudge" 1500
            else
                send_guest_key "$nudge_key" 120
            fi
        fi
        sleep 1
    done
    fail "timed out waiting for $description (more than $threshold colors)"
}

normalized_rmse() {
    reference="$1"
    candidate="$2"
    metric="$(compare -metric RMSE "$reference" "$candidate" null: 2>&1 >/dev/null || true)"
    printf '%s\n' "$metric" | awk -F '[()]' '{ print $2 + 0 }'
}

wait_for_region_change() {
    reference="$1"
    crop="$2"
    threshold="$3"
    timeout="$4"
    description="$5"
    nudge_key="${6:-}"
    nudge_hold_ms="${7:-120}"
    minimum_mean="${8:-0}"
    reference_crop="$RUN_DIR/reference-crop.png"
    candidate_crop="$RUN_DIR/candidate-crop.png"
    elapsed=0

    convert "$reference" -crop "$crop" +repage "$reference_crop"
    while [ "$elapsed" -lt "$timeout" ]; do
        kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited while waiting for $description"
        ffmpeg -y -nostdin -hide_banner -loglevel error \
            -f x11grab -draw_mouse 0 -video_size "${GUEST_WIDTH}x${HEIGHT}" \
            -i "${DISPLAY_NAME}.0+0,0" -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
            -frames:v 1 "$PROBE_IMAGE" || true
        convert "$PROBE_IMAGE" -crop "$crop" +repage "$candidate_crop"
        difference="$(normalized_rmse "$reference_crop" "$candidate_crop")"
        mean="$(identify -format '%[fx:mean]' "$PROBE_IMAGE" 2>/dev/null || echo 0)"
        if awk -v value="$difference" -v floor="$threshold" \
               -v mean="$mean" -v mean_floor="$minimum_mean" \
               'BEGIN { exit !(value > floor && mean > mean_floor) }'; then
            return 0
        fi
        elapsed=$((elapsed + 1))
        if [ -n "$nudge_key" ] && [ $((elapsed % 45)) -eq 0 ]; then
            send_guest_key "$nudge_key" "$nudge_hold_ms"
        fi
        sleep 1
    done
    fail "timed out waiting for $description (RMSE above $threshold)"
}

wait_for_overworld_map() {
    timeout="$1"
    elapsed=0
    map_crop="$RUN_DIR/map-signature.png"

    while [ "$elapsed" -lt "$timeout" ]; do
        kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited while waiting for the overworld map"
        ffmpeg -y -nostdin -hide_banner -loglevel error \
            -f x11grab -draw_mouse 0 -video_size "${GUEST_WIDTH}x${HEIGHT}" \
            -i "${DISPLAY_NAME}.0+0,0" -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
            -frames:v 1 "$PROBE_IMAGE" || true
        convert "$PROBE_IMAGE" -crop "90x330+270+95" +repage "$map_crop"
        blue_mean="$(identify -format '%[fx:mean.b]' "$map_crop" 2>/dev/null || echo 0)"
        frame_mean="$(identify -format '%[fx:mean]' "$PROBE_IMAGE" 2>/dev/null || echo 0)"
        if awk -v blue="$blue_mean" -v mean="$frame_mean" \
               'BEGIN { exit !(blue > 0.45 && mean > 0.30) }'; then
            return 0
        fi
        elapsed=$((elapsed + 1))
        if [ $((elapsed % 12)) -eq 0 ]; then
            # Depending on TCG load, the first input may land on the title,
            # save-slot menu, player-count menu, or translated intro dialog.
            # Pace all relevant handheld controls so each screen can consume
            # the one it expects before checking for the map again.
            send_guest_key ret 1200
            sleep 2
            send_guest_key ctrl 1200
            sleep 2
            send_guest_key alt 1200
            sleep 2
            send_guest_key spc 1200
            sleep 2
            send_guest_key right 4000
        fi
        sleep 1
    done
    fail "timed out waiting for the overworld map visual signature"
}

send_key() {
    key="$1"
    case "$key" in
        Right)     qemu_key="right" ;;
        Left)      qemu_key="left" ;;
        Up)        qemu_key="up" ;;
        Down)      qemu_key="down" ;;
        Control_L) qemu_key="ctrl" ;;
        Alt_L)     qemu_key="alt" ;;
        Shift_L)   qemu_key="shift" ;;
        Return)    qemu_key="ret" ;;
        space)     qemu_key="spc" ;;
        *)         fail "unsupported capture key: $key" ;;
    esac
    send_guest_key "$qemu_key" 220
    sleep 0.20
}

send_guest_key() {
    qemu_key="$1"
    hold_ms="$2"
    printf 'sendkey %s %s\n' "$qemu_key" "$hold_ms" | \
        nc -N -U "$MONITOR_SOCKET" >/dev/null 2>&1 || \
        fail "failed to send guest key: $qemu_key"
    sleep 0.15
}

wait_for_log() {
    file="$1"
    pattern="$2"
    timeout="$3"
    for _ in $(seq 1 "$timeout"); do
        kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited while waiting for: $pattern"
        grep -Fq "$pattern" "$file" 2>/dev/null && return 0
        sleep 1
    done
    fail "timed out waiting for: $pattern"
}

send_until_new_log() {
    file="$1"
    pattern="$2"
    key="$3"
    timeout="$4"
    initial_count="$(grep -Fc "$pattern" "$file" 2>/dev/null || true)"
    elapsed=0

    while [ "$elapsed" -lt "$timeout" ]; do
        kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited while waiting for: $pattern"
        current_count="$(grep -Fc "$pattern" "$file" 2>/dev/null || true)"
        if [ "$current_count" -gt "$initial_count" ]; then
            return 0
        fi
        if [ $((elapsed % 10)) -eq 0 ]; then
            send_key "$key"
        fi
        elapsed=$((elapsed + 1))
        sleep 1
    done
    fail "timed out waiting for a new log entry: $pattern"
}

# 1-4: exercise the actual themed frontend and settings like a handheld user.
screenshot "01-system-snes.png"
send_until_new_log "$PERSIST_DIR/logs/lilsmoke-ui.log" \
    "SystemView: theme ready for __retroarch" Right 90
sleep 2
screenshot "02-system-carousel.png"

# Move through Explorer to Settings, inspect the lower settings page, and
# briefly adjust volume in both directions so the persisted value is restored.
send_until_new_log "$PERSIST_DIR/logs/lilsmoke-ui.log" \
    "SystemView: theme ready for __explorer" Right 90
send_until_new_log "$PERSIST_DIR/logs/lilsmoke-ui.log" \
    "SystemView: theme ready for __settings" Right 90
send_key Control_L
sleep 2
for _ in $(seq 1 9); do send_key Down; done
send_key Right
send_key Left
sleep 2
screenshot "03-settings-options.png"

# Return to SNES, open it from the system carousel, and wait for the complete
# EmulationStation game-list theme to parse before capturing or launching it.
send_key Alt_L
sleep 2
# These systems are already cached, so revisiting them deliberately emits no
# new theme-ready log. Pace the three known carousel steps instead.
for _ in 1 2 3; do
    send_key Left
    sleep 3
done
send_until_new_log "$PERSIST_DIR/logs/lilsmoke-ui.log" \
    "GameList: ready for snes" Control_L 120
sleep 3
screenshot "04-game-list.png"

# Launch from the UI. The production wrapper must extract the 7z and invoke
# the installed MIPS libretro core; its persistent log is our readiness signal.
LAUNCH_STARTED_MS="$(date +%s%3N)"
send_until_new_log "$PERSIST_DIR/logs/lilsmoke-ui.log" \
    "Launcher: starting" Control_L 60
retroarch_ready=0
for _ in $(seq 1 120); do
    kill -0 "$QEMU_PID" 2>/dev/null || fail "QEMU exited while launching RetroArch"
    if [ -s "$PERSIST_DIR/logs/retroarch.log" ]; then
        retroarch_ready=1
        break
    fi
    sleep 1
done
[ "$retroarch_ready" -eq 1 ] || fail "RetroArch did not create its launch log"
wait_for_log "$PERSIST_DIR/logs/retroarch.log" \
    "[Core]: Loading dynamic libretro core from:" 60
grep -Fq '/usr/lib/libretro/snes9x_2005_libretro.so' \
    "$PERSIST_DIR/logs/retroarch.log" || \
    fail "RetroArch did not load the expected MIPS SNES core"
wait_for_log "$PERSIST_DIR/logs/retroarch.log" \
    "[ALSA]: Initialized PLAYBACK device" 60
if grep -Fq '/.virtfs_metadata/' "$PERSIST_DIR/logs/retroarch.log"; then
    fail "RetroArch selected a QEMU metadata sidecar instead of the ROM"
fi

# This legal PT-BR patch has a long unskippable splash sequence. On one-core
# MIPS TCG its duration varies substantially, so wait for the colorful title
# frame instead of sending Start into an intro that cannot receive input.
wait_for_color_frame 220 300 "the Super Mario World title screen" ctrl 0.15
sleep 2
screenshot "05-game-title.png"

# Start the game, select the first save, and advance the opening sequence until
# the translated introduction returns control on the Yoshi's House map node.
send_guest_key ret 1200
sleep 25
send_guest_key ret 1200
sleep 55
send_guest_key ctrl 1200
sleep 25
send_guest_key ctrl 1200
sleep 20
send_guest_key left 2200
sleep 12
send_guest_key alt 1200
sleep 65
wait_for_overworld_map 300
sleep 5
ffmpeg -y -nostdin -hide_banner -loglevel error \
    -f x11grab -draw_mouse 0 -video_size "${GUEST_WIDTH}x${HEIGHT}" \
    -i "${DISPLAY_NAME}.0+0,0" -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
    -frames:v 1 "$RUN_DIR/overworld-start.png"

# Move from Yoshi's House to the first Yoshi's Island action node. The guest
# may run at under ten rendered frames per host second, so verify the title
# region changed before pressing the face button.
send_guest_key left 2500
wait_for_region_change "$RUN_DIR/overworld-start.png" \
    "390x40+400+60" 0.14 180 "a Yoshi's Island action node" left 2500 0.20
sleep 12
ffmpeg -y -nostdin -hide_banner -loglevel error \
    -f x11grab -draw_mouse 0 -video_size "${GUEST_WIDTH}x${HEIGHT}" \
    -i "${DISPLAY_NAME}.0+0,0" -vf "crop=${WIDTH}:${HEIGHT}:1:0" \
    -frames:v 1 "$RUN_DIR/overworld-level-node.png"

# Enter the selected level and reject both black transition frames and any
# overworld frame. This prevents a successful-looking capture that never
# reached side-scrolling gameplay on slow software emulation.
send_guest_key alt-right 2500
sleep 30
wait_for_region_change "$RUN_DIR/overworld-level-node.png" \
    "390x40+400+60" 0.35 240 "the first action-stage frame" alt-right 2500 0.15
sleep 2
screenshot "06-gameplay-input.png"
send_guest_key left 1500
send_guest_key alt-left 1000
send_guest_key shift-left 1000
send_guest_key ctrl-left 800
send_guest_key spc 800
send_guest_key up 800
send_guest_key down 800
sleep 10
sleep 2

stop_process "$FFMPEG_PID" INT
FFMPEG_PID=""
stop_process "$QEMU_PID" TERM
QEMU_PID=""
stop_process "$XVFB_PID" TERM
XVFB_PID=""

[ -s "$RAW_VIDEO" ] || fail "screen recording is empty"

# QEMU's WAV backend begins writing when the guest opens the PCM stream. Delay
# it to the UI-to-game launch point; if no samples were emitted, retain a valid
# silent WebM rather than fabricating host audio.
FINAL_VIDEO="$OUTPUT_DIR/${SLUG}.webm"
if [ -s "$QEMU_AUDIO" ] && \
   ffprobe -v error -show_entries stream=codec_type \
       -of default=noprint_wrappers=1:nokey=1 "$QEMU_AUDIO" 2>/dev/null | grep -q audio; then
    launch_delay_ms=$((LAUNCH_STARTED_MS - CAPTURE_STARTED_MS))
    ffmpeg -y -nostdin -hide_banner -loglevel error \
        -i "$RAW_VIDEO" -i "$QEMU_AUDIO" \
        -filter_complex "[1:a]adelay=${launch_delay_ms}:all=1,apad[a]" \
        -map 0:v:0 -map '[a]' -c:v copy -c:a libopus -b:a 64k \
        -shortest "$FINAL_VIDEO"
else
    cp "$RAW_VIDEO" "$FINAL_VIDEO"
fi

cp "$SERIAL_LOG" "$OUTPUT_DIR/logs/guest-serial.log"
cp "$PERSIST_DIR/logs/lilsmoke-ui.log" "$OUTPUT_DIR/logs/lilsmoke-ui.log"
cp "$PERSIST_DIR/logs/retroarch.log" "$OUTPUT_DIR/logs/retroarch.log"
cp "$RUN_DIR/qemu-host.log" "$OUTPUT_DIR/logs/qemu-host.log"
cp "$RUN_DIR/ffmpeg.log" "$OUTPUT_DIR/logs/ffmpeg.log"

VIDEO_DURATION="$(ffprobe -v error -show_entries format=duration \
    -of default=noprint_wrappers=1:nokey=1 "$FINAL_VIDEO")"
BOOT_TO_CAPTURE_MS=$((CAPTURE_STARTED_MS - QEMU_STARTED_MS))
printf 'THEME_CAPTURE_OK theme=%s slug=%s duration=%s boot_to_capture_ms=%s output=%s\n' \
    "$THEME_NAME" "$SLUG" "$VIDEO_DURATION" "$BOOT_TO_CAPTURE_MS" "$OUTPUT_DIR"
