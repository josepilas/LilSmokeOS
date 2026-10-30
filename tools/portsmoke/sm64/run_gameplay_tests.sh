#!/bin/bash
set -euo pipefail
T=$(cd -- "$(dirname -- "$0")" && pwd)
P=$(cd -- "$T/../../.." && pwd)
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
S="$P/package/portsmoke-sm64/src"
B="$T/_build/gameplay"
mkdir -p "$B"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -I"$S")
sources=("$T/test_gameplay.cpp" "$S/gameplay.cpp" "$S/bob_scene.cpp" "$S/asset_pack.cpp" "$S/runtime_assets.cpp")
g++ "${flags[@]}" "${sources[@]}" -o "$B/test-host"
"$B/test-host"
"$SDK/host/bin/mipsel-buildroot-linux-musl-g++" "${flags[@]}" "${sources[@]}" -static-libstdc++ -static-libgcc -o "$B/test-mips"
qemu-mipsel -cpu 74Kf -L "$SDK/staging" "$B/test-mips"
g++ "${flags[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "${sources[@]}" -o "$B/test-asan"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$B/test-asan"
