#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=${QA_SOURCE:-$P/package/lilsmoke-ui/src/src}
Q=${QA_OUTPUT:-/root/lilsmoke-candidate42-layout}
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
mkdir -p "$Q"
if [ "${QA_MIPS:-0}" = 1 ]; then
    CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
    FLAGS="-I$B/staging/usr/include"
    RUN="qemu-mipsel -cpu 74Kf -L $B/target"
else
    CXX=g++
    FLAGS="$(pkg-config --cflags sdl2 SDL2_image SDL2_ttf) -fsanitize=address,undefined -fno-pie -no-pie"
    RUN=
fi
"$CXX" -std=c++17 -O2 -g -Wall -Wextra -Werror $FLAGS -I"$S" \
    "$P/tools/ui/candidate42-layout-test.cpp" "$S/Renderer.cpp" "$S/Font.cpp" \
    "$S/Theme.cpp" "$S/ThemeRender.cpp" "$S/Log.cpp" \
    -lSDL2_image -lSDL2_ttf -lSDL2 -lwebp -ldl -o "$Q/layout-test"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    $RUN "$Q/layout-test" "$Q/fixtures" /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
