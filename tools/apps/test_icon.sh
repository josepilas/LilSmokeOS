#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
Q=$(mktemp -d /tmp/lilsmoke-app-icon-XXXXXX)
trap 'rm -rf "$Q"' EXIT HUP INT TERM
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$S" \
  "$P/tools/apps/icon_test.cpp" "$S/Renderer.cpp" "$S/Font.cpp" "$S/Log.cpp" "$S/Theme.cpp" \
  $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf json-c) -lwebp -ldl -o "$Q/icon-test"
"$Q/icon-test" "$@"
if [ "${QA_MIPS:-0}" = 1 ]; then
  B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
  "$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra -Werror \
    -I"$S" -I"$B/staging/usr/include" \
    "$P/tools/apps/icon_test.cpp" "$S/Renderer.cpp" "$S/Font.cpp" "$S/Log.cpp" "$S/Theme.cpp" \
    -lSDL2_image -lSDL2_ttf -lSDL2 -ljson-c -lwebp -ldl -o "$Q/icon-test-mips"
  qemu-mipsel -cpu 74Kf -L "$B/target" "$Q/icon-test-mips" "$@"
fi
