#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
Q=${QA_LAYOUT_OUTPUT:-$(mktemp -d /tmp/lilsmoke-ui-layout-XXXXXX)}
mkdir -p "$Q"
g++ -std=c++17 -O0 -Wall -Wextra -Werror -I"$S" \
    "$P/tools/ui/render_game_list_layout.cpp" "$S/GridView.cpp" "$S/VirtualKeyboard.cpp" \
    "$S/Renderer.cpp" "$S/Font.cpp" "$S/Theme.cpp" "$S/ThemeRender.cpp" "$S/Log.cpp" \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -lwebp -ldl -o "$Q/render-layout"
"$Q/render-layout" "$P/board/qemu-mipsel/theme.xml" \
    /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "$Q"
echo "Host software/dummy-rendered layout evidence only, not physical console proof: $Q"
