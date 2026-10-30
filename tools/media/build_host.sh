#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${1:-/root/lilsmoke-media-host}
main=${LILSMOKE_MEDIA_MAIN:-$P/package/lilsmoke-media/src/Player.cpp}
mkdir -p "$out"
flags='-O2 -g'
pcmflags='-DM8_PCM_BACKEND_TIMEOUT_MS=500U -DM8_PCM_BACKEND_MAX_POLLS=256U -DM8_PCM_ACK_TIMEOUT_MS=1000U -DM8_PENDING_LIMIT_MS=500U -DM8_STALL_LIMIT=256U'
if [ "${LILSMOKE_MEDIA_SANITIZE:-0}" = 1 ]; then flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; fi
cc $flags $pcmflags -std=gnu11 -DM8_PCM_INFLIGHT_LIMIT=16U -I"$P/board/segam-m8-v6-stock" \
    -c "$P/package/opendingux-emulators/m8_pocketsnes_audio.c" -o "$out/pcm.o"
cc $flags $pcmflags -std=gnu11 -c "$P/board/segam-m8-v6-stock/m8-audio.c" -o "$out/audio.o"
g++ $flags -std=c++17 -DM8_PCM_INFLIGHT_LIMIT=16U -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
    -I"$P/package/lilsmoke-ui/src/src" -I"$P/package/opendingux-emulators" \
    $(pkg-config --cflags sdl2 SDL2_image SDL2_ttf libavformat libavcodec libavutil libswscale libswresample) \
    "$main" "$P/package/lilsmoke-media/src/Decoder.cpp" "$P/package/lilsmoke-media/src/NativeVideo.cpp" \
    "$P/package/lilsmoke-ui/src/src/Renderer.cpp" "$P/package/lilsmoke-ui/src/src/Font.cpp" \
    "$P/package/lilsmoke-ui/src/src/Input.cpp" "$P/package/lilsmoke-ui/src/src/Keymap.cpp" \
    "$P/package/lilsmoke-ui/src/src/Log.cpp" "$out/pcm.o" "$out/audio.o" \
    "$P/package/lilsmoke-ui/src/src/Cursor.cpp" \
    $(pkg-config --libs sdl2 SDL2_image SDL2_ttf libavformat libavcodec libavutil libswscale libswresample) \
    -ldl -lwebp -pthread -o "$out/lilsmoke-media"
