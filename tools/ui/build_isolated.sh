#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
Q=${UI_BUILD_OUTPUT:-$(mktemp -d /tmp/lilsmoke-ui-build-XXXXXX)}
UI_OPT_FLAGS=${UI_OPT_FLAGS:--O3 -flto}
mkdir -p "$Q"
echo "Isolated UI build: $Q"
CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
SOURCES=$(make --no-print-directory -s -C "$S" --eval='print-sources:;@echo $(SRCS)' print-sources)
set --
for source in $SOURCES; do
    object=$Q/$(basename "$source" .cpp).o
    # UI_OPT_FLAGS is intentionally split into compiler options, never evaluated.
    "$CXX" -std=c++17 $UI_OPT_FLAGS -Wall -Wextra -Wno-unused-parameter \
        -ffunction-sections -fdata-sections \
        -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
        -I"$P/package/lilsmoke-scripts/src" -I"$P/package/lilsmoke-media/src" \
        -I"$B/staging/usr/include" -I"$B/staging/usr/include/SDL2" \
        -c "$S/$source" -o "$object"
    set -- "$@" "$object"
done
"$CXX" $UI_OPT_FLAGS -flto -Wl,--gc-sections "$@" -lSDL2_image -lSDL2_ttf -lSDL2 -lpng -ljson-c -lz -lwebp \
    -o "$Q/lilsmoke-ui"
file "$Q/lilsmoke-ui"
"$CXX" -std=c++17 $UI_OPT_FLAGS -Wall -Wextra -Werror -I"$S/src" \
    "$S/apps/session.cpp" "$S/src/GameSession.cpp" "$S/src/GameHistory.cpp" \
    -Wl,--gc-sections -o "$Q/lilsmoke-session"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" --strip-unneeded -o "$Q/lilsmoke-ui.stripped" "$Q/lilsmoke-ui"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" --strip-unneeded -o "$Q/lilsmoke-session.stripped" "$Q/lilsmoke-session"
sha256sum "$Q/lilsmoke-ui.stripped" "$Q/lilsmoke-session.stripped"
echo "No SDK, target, image, or SD writes were performed."
