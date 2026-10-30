#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
R=/root/lilsmoke-emulator-expansion-20261008
Q=$R/engine-build/native-video-qa
SRC=$P/package/lilsmoke-od-sdl
CC=$B/host/bin/mipsel-buildroot-linux-musl-gcc
CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
mkdir -p "$Q"
# Reuse only compiled production logic; replace hardware gate/PCM in this
# isolated test library. Neither QA artifact is accepted by release staging.
"$CC" -O2 -fPIC -DOD_SDL_QA -I"$SRC/src" -c "$SRC/src/frontend.c" -o "$Q/frontend.o"
"$CC" -O2 -fPIC -I"$P/package/opendingux-emulators" -c "$SRC/tests/pcm-mock.c" -o "$Q/pcm.o"
"$CXX" -shared -Wl,-z,defs "$Q/frontend.o" "$Q/pcm.o" \
    "$R/adapter/video.o" "$R/adapter/audio.o" "$R/adapter/events1.o" "$R/adapter/events2.o" \
    "$R/adapter/input.o" "$R/adapter/common-input.o" "$R/adapter/common-keymap.o" \
    "$R/adapter/common-log.o" -lpthread -ldl -o "$Q/libod-video-qa.so"
"$CC" -shared -O2 -fPIC "$P/tools/emulators/od-engines/native-present-mock.c" -o "$Q/presenter-mock.so"
