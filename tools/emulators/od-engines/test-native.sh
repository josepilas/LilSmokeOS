#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
SYS=$ROOT/engine-sysroot
QA=$ROOT/engine-build/native-qa
LIBS=${OD_QA_LIBDIR:-$ROOT/engine-build/sdl-qa}:$ROOT/built-native/lib:$SYS/usr/lib:$SYS/lib
mkdir -p "$QA/empty" "$ROOT/engine-logs"
exec > >(tee -a "$ROOT/engine-logs/native-qa.log") 2>&1
"$SDK/bin/mipsel-buildroot-linux-musl-gcc" -O1 -fPIC -shared -mabi=32 -mfp32 \
    -I"$SYS/usr/include/SDL2" "$HERE/menu-probe.c" -lSDL2 -ldl -o "$QA/menu-probe.so"
"$SDK/bin/mipsel-buildroot-linux-musl-gcc" -O1 -mabi=32 -mfp32 \
    -I"$SYS/usr/include/SDL2" "$HERE/menu-probe-test.c" "$QA/menu-probe.so" -lSDL2 \
    -o "$QA/menu-probe-test"
timeout 20 qemu-mipsel -cpu 74Kf -L "$SYS" \
    -E LD_LIBRARY_PATH="$LIBS" "$QA/menu-probe-test"
for engine in "${@:-prboom}"; do
    BIN=${OD_NATIVE_BIN_ROOT:-$ROOT/built-native/bin}/$engine
    test -x "$BIN"
    echo "CLI $engine"
    help_option=--help
    [ "$engine" != mgba ] || help_option=--version
    timeout 20 qemu-mipsel -cpu 74Kf -L "$SYS" \
        -E LD_LIBRARY_PATH="$LIBS" -E SDL_VIDEODRIVER=dummy -E SDL_AUDIODRIVER=dummy \
        "$BIN" "$help_option" > "$QA/$engine-help.txt" 2>&1
    head -6 "$QA/$engine-help.txt"
    echo "NO-CONTENT MENU $engine"
    set --
    [ "$engine" != scummvm ] || set -- --gui-theme=scummclassic
    cd "$QA/empty"
    PRELOAD=$QA/menu-probe.so
    if [ "${OD_NATIVE_VIDEO_TEST:-0}" = 1 ]; then
        PRELOAD=$PRELOAD:$ROOT/engine-build/native-video-qa/libod-video-qa.so:$ROOT/engine-build/native-video-qa/presenter-mock.so
    fi
    timeout 30 qemu-mipsel -cpu 74Kf -L "$SYS" \
        -E LD_LIBRARY_PATH="$LIBS" -E LD_PRELOAD="$PRELOAD" \
        -E LILSMOKE_OD_NATIVE="${OD_NATIVE_VIDEO_TEST:-0}" -E LILSMOKE_OD_QA="${OD_NATIVE_VIDEO_TEST:-0}" \
        -E LILSMOKE_OD_ENGINE="$engine" \
        -E SDL_VIDEODRIVER=dummy -E SDL_AUDIODRIVER=dummy -E SDL_RENDER_DRIVER=software \
        -E LILSMOKE_NX_ROOT="$QA/empty" -E LILSMOKE_PRBOOM_ROOT="$QA/empty" \
        -E OD_MENU_QA_FRAME="$QA/$engine-menu.bmp" "$BIN" \
        "$@" > "$QA/$engine-menu.txt" 2>&1
    cat "$QA/$engine-menu.txt"
    grep -q OD_MENU_QA "$QA/$engine-menu.txt"
    test -s "$QA/$engine-menu.bmp"
    echo "PASS $engine help and nonblank native no-content menu with quit event"
done
