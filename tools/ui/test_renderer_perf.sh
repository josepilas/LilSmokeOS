#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=$P/package/lilsmoke-ui/src/src
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
Q=${QA_OUTPUT:-/root/ui-renderer-perf-regressions}
mkdir -p "$Q"
set -- "$P/tools/ui/renderer-perf-regressions.cpp" "$S/Renderer.cpp" "$S/Font.cpp" "$S/Log.cpp"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -fno-pie -no-pie -I"$S" "$@" \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -lwebp -ldl -o "$Q/regressions-asan"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$Q/regressions-asan" "$Q/fixtures"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O3 -flto -Wall -Wextra -Werror \
    -I"$S" -I"$B/staging/usr/include" "$@" -lSDL2_image -lSDL2_ttf -lSDL2 -lwebp -ldl -o "$Q/regressions-mips"
QA_HEADLESS=1 qemu-mipsel -cpu 74Kf -L "$B/target" "$Q/regressions-mips" "$Q/mips-fixtures"
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -fno-pie -no-pie -rdynamic -I"$S" \
    "$P/tools/hwtest/ui_damage_smoke.cpp" "$S/Renderer.cpp" "$S/Font.cpp" "$S/Cursor.cpp" "$S/Log.cpp" \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -lwebp -ldl \
    -Wl,--wrap=SDL_RenderPresent -o "$Q/damage-asan"
SDL_VIDEODRIVER=dummy LILSMOKE_UI_WINDOWED=1 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$Q/damage-asan" /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
    "$B/target/usr/share/lilsmoke/cursors/lilsmoke-cursors.webp"
