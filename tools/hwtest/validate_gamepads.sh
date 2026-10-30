#!/bin/bash
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$(dirname "$0")/../.."
br=/root/lilsmoke-os/build/buildroot
out=test-results/gamepads
mkdir -p "$out"
sources=(tools/hwtest/gamepad_smoke.cpp package/lilsmoke-ui/src/src/{Input,Keymap,Log}.cpp)
g++ -std=c++17 -Os -Wall -Wextra -Werror -Ipackage/lilsmoke-ui/src/src \
    "${sources[@]}" -o "$out/gamepad-host"
"$out/gamepad-host" > "$out/host.log"
"$br/output/host/bin/mipsel-buildroot-linux-musl-g++" \
    -std=c++17 -Os -Wall -Wextra -Werror -Ipackage/lilsmoke-ui/src/src \
    "${sources[@]}" -o "$out/gamepad-mips"
qemu-mipsel -cpu 74Kf -L "$br/output/target" "$out/gamepad-mips" > "$out/mips.log"
mkdir -p test-results/remote-input
"$br/output/host/bin/mipsel-buildroot-linux-musl-g++" \
    -std=c++17 -Os -Wall -Wextra -Werror -Ipackage/lilsmoke-ui/src/src \
    tools/hwtest/remote_input_smoke.cpp package/lilsmoke-ui/src/src/{Input,Keymap,Log}.cpp \
    -o test-results/remote-input/input-smoke-mips
qemu-mipsel -cpu 74Kf -L "$br/output/target" test-results/remote-input/input-smoke-mips \
    > test-results/remote-input/input-mips.log
tail -1 "$out/host.log"
tail -1 "$out/mips.log"
tail -1 test-results/remote-input/input-mips.log
echo GAMEPAD_VALIDATION_OK
