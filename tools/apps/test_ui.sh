#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
Q=/root/lilsmoke-applications-20261009
S=$P/package/lilsmoke-ui/src/src
mkdir -p "$Q"
flags=-O2
if [ "${LILSMOKE_UI_SANITIZE:-0}" = 1 ]; then flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; fi
g++ -std=c++17 $flags -Wall -Wextra -I"$S" -I"$P/package/lilsmoke-scripts/src" \
    "$P/tools/apps/ui_test.cpp" "$S/ApplicationsView.cpp" "$S/ApplicationArtwork.cpp" "$S/Renderer.cpp" \
    "$S/Font.cpp" "$S/Log.cpp" "$S/ProcessLaunch.cpp" "$S/Theme.cpp" \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf json-c) -lwebp -ldl -Wl,--wrap=SDL_GetTicks -o "$Q/ui-test"
python3 "$P/tools/apps/test_ui.py"
