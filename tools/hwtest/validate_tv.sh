#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$(dirname "$0")/../.."
out=test-results/tv-and-image-2026-10-04
mkdir -p "$out"
ui=package/lilsmoke-ui/src/src
build=${BUILD:-/root/lilsmoke-os/build/buildroot/output}
sources="tools/hwtest/tv_ui_smoke.cpp $ui/SettingsView.cpp $ui/Config.cpp $ui/Renderer.cpp $ui/Font.cpp $ui/Theme.cpp $ui/Log.cpp"
# shellcheck disable=SC2086
c++ -std=c++17 -O2 -Wall -Wextra -I"$ui" $sources \
    $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf) -lz -o "$out/tv-host"
SDL_VIDEODRIVER=dummy LILSMOKE_UI_WINDOWED=1 "$out/tv-host" "$out" > "$out/tv-host.log" 2>&1
# shellcheck disable=SC2086
"$build/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -I"$ui" \
    -I"$build/staging/usr/include/SDL2" $sources \
    -lSDL2_image -lSDL2_ttf -lSDL2 -lz -o "$out/tv-mips"
qemu-mipsel -cpu 74Kf -L "$build/target" "$out/tv-mips" > "$out/tv-mips.log" 2>&1
c++ -std=c++17 -O2 -I"$ui" tools/hwtest/remote_input_smoke.cpp \
    "$ui/Input.cpp" "$ui/Keymap.cpp" "$ui/Log.cpp" -o "$out/input-host"
"$out/input-host" > "$out/input-host.log" 2>&1
"$build/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -I"$ui" \
    tools/hwtest/remote_input_smoke.cpp "$ui/Input.cpp" "$ui/Keymap.cpp" "$ui/Log.cpp" -o "$out/input-mips"
qemu-mipsel -cpu 74Kf -L "$build/target" "$out/input-mips" > "$out/input-mips.log" 2>&1
tail -n 1 "$out/tv-host.log" "$out/tv-mips.log" "$out/input-host.log" "$out/input-mips.log"
