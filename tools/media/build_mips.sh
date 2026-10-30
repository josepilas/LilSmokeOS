#!/bin/bash
set -euo pipefail
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
OUT=${1:?pass an isolated output directory}
main=${LILSMOKE_MEDIA_MAIN:-$P/package/lilsmoke-media/src/Player.cpp}
mkdir -p "$OUT"
export PKG_CONFIG_SYSROOT_DIR="$SDK/staging"
export PKG_CONFIG_LIBDIR="$SDK/staging/usr/lib/pkgconfig:$SDK/staging/usr/share/pkgconfig"
export PKG_CONFIG_PATH=
CC="$SDK/host/bin/mipsel-buildroot-linux-musl-gcc"
CXX="$SDK/host/bin/mipsel-buildroot-linux-musl-g++"
cpu=${LILSMOKE_MEDIA_BUILD_CPU:-$(awk '/Cpus_allowed_list/ {split($2,a,"[-,]"); print a[1]}' /proc/self/status)}
compile() { taskset -c "$cpu" nice -n 10 prlimit --as=805306368 -- "$@"; }
flags=(-O2 -g0 -mips32r2 -mabi=32 -mfp32 -mhard-float -ffunction-sections -fdata-sections)
pcmflags=(-DM8_PCM_BACKEND_TIMEOUT_MS=500U -DM8_PCM_BACKEND_MAX_POLLS=256U -DM8_PCM_ACK_TIMEOUT_MS=1000U
    -DM8_PENDING_LIMIT_MS=500U -DM8_STALL_LIMIT=256U)
compile "$CC" "${flags[@]}" "${pcmflags[@]}" -std=gnu11 -DM8_PCM_INFLIGHT_LIMIT=16U \
    -I"$P/board/segam-m8-v6-stock" -c "$P/package/opendingux-emulators/m8_pocketsnes_audio.c" -o "$OUT/pcm.o"
compile "$CC" "${flags[@]}" "${pcmflags[@]}" -std=gnu11 -c "$P/board/segam-m8-v6-stock/m8-audio.c" -o "$OUT/audio.o"
read -r -a cflags <<< "$("$SDK/host/bin/pkg-config" --cflags sdl2 SDL2_image SDL2_ttf libavformat libavcodec libavutil libswscale libswresample)"
read -r -a libs <<< "$("$SDK/host/bin/pkg-config" --libs sdl2 SDL2_image SDL2_ttf libavformat libavcodec libavutil libswscale libswresample)"
compile "$CXX" "${flags[@]}" -std=c++17 -DM8_PCM_INFLIGHT_LIMIT=16U -Wall -Wextra \
    -Wno-unused-parameter -Wno-misleading-indentation -I"$P/package/lilsmoke-ui/src/src" \
    -I"$P/package/opendingux-emulators" -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    "${cflags[@]}" "$main" "$P/package/lilsmoke-media/src/Decoder.cpp" "$P/package/lilsmoke-media/src/NativeVideo.cpp" \
    "$P/package/lilsmoke-ui/src/src/Renderer.cpp" "$P/package/lilsmoke-ui/src/src/Font.cpp" \
    "$P/package/lilsmoke-ui/src/src/Input.cpp" "$P/package/lilsmoke-ui/src/src/Keymap.cpp" \
    "$P/package/lilsmoke-ui/src/src/Log.cpp" "$P/package/lilsmoke-ui/src/src/Cursor.cpp" \
    "$OUT/pcm.o" "$OUT/audio.o" "${libs[@]}" -Wl,--gc-sections -ldl -lwebp -pthread -o "$OUT/lilsmoke-media"
sha256sum "$OUT/lilsmoke-media"
