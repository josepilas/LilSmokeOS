#!/bin/bash
# Standalone SDK build; no shared Buildroot config, target tree or image writes.
set -euo pipefail
T=$(cd -- "$(dirname -- "$0")" && pwd)
P=$(cd -- "$T/../../.." && pwd)
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
B="$T/_build"
S="$P/package/portsmoke-sm64/src"
CXX="$SDK/host/bin/mipsel-buildroot-linux-musl-g++"
mkdir -p "$B"
common=(-std=c++17 -O2 -Wall -Wextra -Werror -I"$S")
core=("$S/asset_pack.cpp" "$S/runtime_assets.cpp" "$S/bob_scene.cpp")
gcc_license=$(find "$SDK/build" -maxdepth 2 -path '*/host-gcc-final-*/COPYING.RUNTIME' -print -quit)
test -n "$gcc_license"
gcc_license=$(dirname -- "$gcc_license")
# Each compile runs serially, equivalent to -j1.
g++ "${common[@]}" "$S/main.cpp" "${core[@]}" -o "$B/host-data"
"$CXX" "${common[@]}" "$S/main.cpp" "${core[@]}" -static-libstdc++ -static-libgcc -o "$B/mips-data"
"$SDK/host/bin/mipsel-buildroot-linux-musl-strip" "$B/mips-data"
for name in asset_pack runtime_assets bob_scene; do
    g++ "${common[@]}" -DSM64_TEST_FAILPOINTS "$T/test_$name.cpp" "${core[@]}" -o "$B/host-$name"
    "$CXX" "${common[@]}" -DSM64_TEST_FAILPOINTS "$T/test_$name.cpp" "${core[@]}" \
        -static-libstdc++ -static-libgcc -o "$B/mips-$name"
    "$B/host-$name"
    qemu-mipsel -cpu 74Kf -L "$SDK/staging" "$B/mips-$name"
    g++ "${common[@]}" -DSM64_TEST_FAILPOINTS -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$T/test_$name.cpp" "${core[@]}" -o "$B/asan-$name"
    ASAN_OPTIONS=detect_leaks=1 "$B/asan-$name"
done
gcc -shared -fPIC -Wall -Wextra -Werror "$T/test_no_file_locks.c" -o "$B/host-no-file-locks.so"
"$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" -shared -fPIC -Wall -Wextra -Werror \
    "$T/test_no_file_locks.c" -o "$B/mips-no-file-locks.so"
LD_PRELOAD="$B/host-no-file-locks.so" "$B/host-asset_pack"
qemu-mipsel -cpu 74Kf -L "$SDK/staging" -E LD_PRELOAD="$B/mips-no-file-locks.so" "$B/mips-asset_pack"
printf 'PASS with flock and POSIX record-lock calls returning ENOSYS\n'
printf 'PASS serial host/MIPS/ASan data/texture/F3D/discovery builds\n'
