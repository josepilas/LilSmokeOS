#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
SYS=$ROOT/engine-sysroot
QA=$ROOT/engine-build/deps-qa
test -s "$SYS/usr/lib/liblcf.a"
mkdir -p "$QA"
exec > >(tee -a "$ROOT/engine-logs/deps-qa.log") 2>&1
"$SDK/bin/mipsel-buildroot-linux-musl-g++" -O1 -std=c++17 -mabi=32 -mfp32 \
    -isystem "$SYS/usr/include" -isystem "$SYS/usr/include/pixman-1" \
    -isystem "$SYS/usr/include/freetype2" "$HERE/deps-test.cpp" \
    -L"$SYS/usr/lib" -Wl,-rpath-link,"$SYS/usr/lib" \
    -llcf -linih -lfmt -lpixman-1 -lvorbis -logg -lpng16 -lfreetype -lz \
    -o "$QA/deps-test"
timeout 30 qemu-mipsel -cpu 74Kf -L "$SYS" \
    -E LD_LIBRARY_PATH="$SYS/usr/lib:$SYS/lib" "$QA/deps-test"
sha256sum "$SYS/usr/lib/liblcf.a" "$SYS/usr/lib/libfmt.a" "$SYS/usr/lib/libinih.a"
