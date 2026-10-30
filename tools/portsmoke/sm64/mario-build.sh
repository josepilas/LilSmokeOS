#!/bin/bash
set -euo pipefail
T=$(cd -- "$(dirname -- "$0")" && pwd)
P=$(cd -- "$T/../../.." && pwd)
S="$P/package/portsmoke-sm64/src"
B="$T/_build/mario-worker"
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
mkdir -p "$B"
common=(-std=c++17 -O2 -Wall -Wextra -Werror -I"$S")
sources=("$S/mario_actor.cpp" "$S/bob_scene.cpp" "$S/asset_pack.cpp" "$S/runtime_assets.cpp")
# All compiles serial; SDK and main scene build directories are read-only.
for target in host mips asan; do
    compiler=g++; flags=()
    if [ "$target" = mips ]; then compiler="$SDK/host/bin/mipsel-buildroot-linux-musl-g++"; flags=(-static-libstdc++ -static-libgcc); fi
    if [ "$target" = asan ]; then flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); fi
    "$compiler" "${common[@]}" "${flags[@]}" "$T/test_mario_actor.cpp" "${sources[@]}" -o "$B/$target-test"
    "$compiler" "${common[@]}" "${flags[@]}" "$T/mario-private.cpp" "${sources[@]}" -o "$B/$target-private"
    if [ "$target" = mips ]; then qemu-mipsel -cpu 74Kf -L "$SDK/staging" "$B/$target-test";
    else "$B/$target-test"; fi
done
