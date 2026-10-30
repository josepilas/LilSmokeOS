#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
R=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
src=$R/sources/mame4all
pin=3683f198f0d9f7043f4ba12fdce85ecc61867e77
mkdir -p "$R/engine-logs"
exec >"$R/engine-logs/mame4all-build.log" 2>&1
if [ ! -d "$src/.git" ]; then
    git clone --no-checkout https://github.com/alekmaul/mame4all.git "$src"
    git -C "$src" checkout --detach "$pin"
fi
test "$(git -C "$src" rev-parse HEAD)" = "$pin"
for name in mame4all-null mame4all-native mame4all-assets mame4all-quit mame4all-frontend-quit; do
    patch=$P/package/lilsmoke-od-engines/patches/$name.patch
    if [ "$name" = mame4all-native ] && grep -q odx_quit_requested "$src/src/odx/minimal.cpp"; then continue; fi
    if [ "$name" = mame4all-quit ] && grep -q odx_quit_handler "$src/src/odx/minimal.cpp"; then continue; fi
    if ! git -C "$src" apply --reverse --check --recount --unidiff-zero "$patch" 2>/dev/null; then
        git -C "$src" apply --recount --unidiff-zero "$patch"
    fi
done
make -C "$src" -f "$P/package/lilsmoke-od-engines/Makefile.mame4all" -j1 \
    OSTYPE=gcw0 OD_PACKAGE="$P/package/lilsmoke-od-engines" OD_CC="$SDK/bin/mipsel-buildroot-linux-musl-gcc" \
    OD_CXX="$SDK/bin/mipsel-buildroot-linux-musl-g++" OD_SYSROOT="$R/engine-sysroot" \
    OD_FLAGS='-O2 -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float' all mame.dge
mkdir -p "$R/native-candidates/bin" "$R/native-candidates/licenses/mame4all"
cp "$src/distrib/mame4all/mame" "$R/native-candidates/bin/mame4all-engine"
cp "$src/distrib/mame4all/mame.dge" "$R/native-candidates/bin/mame4all"
cp "$src/readme.txt" "$R/native-candidates/licenses/mame4all/"
mkdir -p "$R/native-candidates/data/share/mame4all/skins"
cp "$src/distrib/mame4all/skins/menu.bmp" "$src/distrib/mame4all/skins/splash.bmp" "$R/native-candidates/data/share/mame4all/skins/"
