#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
R=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
src=$R/sources/openbor-svn
mkdir -p "$R/engine-logs"
exec >"$R/engine-logs/openbor-build.log" 2>&1
pin=20f5a7e71ddab0ac419a355850716d0c743524de
if [ ! -d "$src/.git" ]; then
    git clone --no-checkout https://github.com/rofl0r/openbor.git "$src"
    git -C "$src" checkout --detach "$pin"
fi
test "$(git -C "$src" rev-parse HEAD)" = "$pin"
for name in openbor-version openbor-native; do
    patch=$P/package/lilsmoke-od-engines/patches/$name.patch
    if ! git -C "$src" apply --reverse --check --recount --unidiff-zero "$patch" 2>/dev/null; then
        git -C "$src" apply --recount --unidiff-zero "$patch"
    fi
done
make -C "$src" -f "$P/package/lilsmoke-od-engines/Makefile.openbor" -j1 \
    BUILD_OPENDINGUX=1 OpenBOR.elf CC="$SDK/bin/mipsel-buildroot-linux-musl-gcc" \
    OPENDINGUX_TOOLCHAIN_PREFIX="$R/engine-sysroot/usr" OD_SYSROOT="$R/engine-sysroot" \
    OD_FLAGS='-Os -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float'
mkdir -p "$R/native-candidates/bin" "$R/native-candidates/licenses/openbor"
install -m 0755 "$src/OpenBOR.elf" "$R/native-candidates/bin/openbor"
"$SDK/bin/mipsel-buildroot-linux-musl-strip" "$R/native-candidates/bin/openbor"
cp "$src/LICENSE" "$src/README" "$R/native-candidates/licenses/openbor/"
