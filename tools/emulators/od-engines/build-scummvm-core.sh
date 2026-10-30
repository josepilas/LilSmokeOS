#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
R=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host}
B=${SDK%/host}
src=$R/sources/scummvm-libretro-2.9
if [ ! -d "$src/.git" ]; then git clone --shared "$R/sources/scummvm" "$src"; fi
test "$(git -C "$src" rev-parse HEAD)" = 38af1883a2eb7a622c31bfc3708888bbcc47db48
back=$src/backends/platform/libretro
patch=$P/package/retroarch-mips/scummvm-disabled-features.patch
if ! git -C "$src" apply --reverse --check --recount "$patch" 2>/dev/null; then
    git -C "$src" apply --recount "$patch"
fi
cp "$P/tools/emulators/od-engines/scummvm-engines.list" "$back/lite_engines.list"
export CC=$B/host/bin/mipsel-buildroot-linux-musl-gcc CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
export AR=$B/host/bin/mipsel-buildroot-linux-musl-ar RANLIB=$B/host/bin/mipsel-buildroot-linux-musl-ranlib
export CFLAGS='-Os -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -fPIC'
export CXXFLAGS="$CFLAGS"
export CPPFLAGS="-isystem $R/engine-sysroot/usr/include -I$R/engine-sysroot/usr/include/freetype2"
profile=$(sha256sum "$0" | cut -d' ' -f1)
marker=$back/.lilsmoke-profile
if [ ! -f "$marker" ] || [ "$(cat "$marker")" != "$profile" ]; then
    # All of these are generated files in the isolated libretro build directory.
    # Upstream does not rebuild existing objects when feature defines change.
    find "$back" -type f \( -name '*.o' -o -name '*.a' \) -delete
    printf '%s\n' "$profile" > "$marker"
fi
make -C "$back" -f Makefile -f "$P/package/retroarch-mips/core-flags.mk" -j1 \
    platform=unix BUILD_64BIT=0 LITE=1 NO_WIP=1 USE_LIBCO=0 FORCE_OPENGLNONE=1 \
    USE_MT32EMU=0 USE_FLUIDSYNTH=0 USE_THEORADEC=0 USE_VORBIS=1 \
    USE_BINK=0 USE_FRIBIDI=0 USE_HIGHRES=0 USE_RGB_COLOR=1 ENABLE_VKEYBD=1 \
    USE_MAD=0 USE_FAAD=0 USE_FLAC=0 USE_LUA=0 USE_TINYGL=0 \
    CC="$CC" CXX="$CXX" AR="$AR cru" RANLIB="$RANLIB" \
    LILSMOKE_CORE_MIPS=1 LILSMOKE_CORE_CFLAGS="$CFLAGS" LILSMOKE_CORE_CXXFLAGS="$CXXFLAGS" \
    LILSMOKE_CORE_LDFLAGS="-L$R/engine-sysroot/usr/lib -Wl,-rpath-link,$R/engine-sysroot/usr/lib"
mkdir -p "$R/built-libretro"
install -m 0644 "$back/scummvm_libretro.so" "$R/built-libretro/"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" --strip-unneeded "$R/built-libretro/scummvm_libretro.so"
