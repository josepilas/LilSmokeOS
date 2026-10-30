#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out="test-results/remote-input"
mkdir -p "$out"
sources=()
for source in package/lilsmoke-ui/src/src/*.cpp; do
    case "$source" in */main.cpp) ;; *) sources+=("$source") ;; esac
done
read -r -a sdl_flags <<< "$(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf libpng)"
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter \
    -Ipackage/lilsmoke-ui/src/src tools/hwtest/remote_ui_smoke.cpp "${sources[@]}" \
    "${sdl_flags[@]}" -lz -o "$out/ui-smoke"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$out/ui-smoke" \
    "$out/screens" package/lilsmoke-ui/src/assets/lilsmoke-cursors.webp 2>&1 | tee "$out/ui-host.log"
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter \
    package/lilsmoke-ui/src/src/main.cpp "${sources[@]}" "${sdl_flags[@]}" -lz -o "$out/lilsmoke-ui"
g++ -std=c++17 -O1 -Wall -Ipackage/lilsmoke-ui/src/src \
    tools/hwtest/remote_input_smoke.cpp package/lilsmoke-ui/src/src/{Input,Keymap,Log}.cpp \
    -o "$out/input-smoke"
"$out/input-smoke" | tee "$out/input-host.log"
python3 tools/hwtest/usb_service_smoke.py | tee "$out/usb-service.log"
python3 tools/hwtest/network_service_smoke.py | tee "$out/network-service.log"
shellcheck -S warning package/lilsmoke-scripts/src/lilsmoke-{usb,network,sshd,wifi,config-read}
for script in package/lilsmoke-scripts/src/lilsmoke-{usb,network,sshd,wifi,config-read}; do
    busybox sh -n "$script"
done
echo REMOTE_HOST_VALIDATION_OK
