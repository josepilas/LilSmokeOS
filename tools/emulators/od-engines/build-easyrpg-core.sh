#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
R=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
export OD_SDK=${OD_SDK:-/root/lilsmoke-os/build/buildroot/output/host} OD_SYSROOT="$R/engine-sysroot"
cmake -S "$R/sources/easyrpg" -B "$R/engine-build/easyrpg-libretro" \
    -DCMAKE_TOOLCHAIN_FILE="$P/tools/emulators/od-engines/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_C_FLAGS='-Os -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -fPIC' \
    -DCMAKE_CXX_FLAGS='-Os -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -fPIC' \
    -DPLAYER_TARGET_PLATFORM=libretro -DPLAYER_WITH_HARFBUZZ=OFF -DPLAYER_WITH_LHASA=OFF \
    -DPLAYER_WITH_NLOHMANN_JSON=OFF -DPLAYER_WITH_NATIVE_MIDI=OFF \
    -DPLAYER_WITH_MPG123=OFF -DPLAYER_WITH_LIBSNDFILE=OFF -DPLAYER_WITH_OGGVORBIS=ON \
    -DPLAYER_WITH_OPUS=OFF -DPLAYER_WITH_WILDMIDI=OFF -DPLAYER_WITH_FLUIDSYNTH=OFF \
    -DPLAYER_WITH_FLUIDLITE=OFF -DPLAYER_WITH_XMP=OFF -DPLAYER_AUDIO_RESAMPLER=OFF -DPLAYER_ENABLE_TESTS=OFF
cmake --build "$R/engine-build/easyrpg-libretro" --target easyrpg_libretro -j1
mkdir -p "$R/built-libretro"
install -m 0644 "$R/engine-build/easyrpg-libretro/easyrpg_libretro.so" "$R/built-libretro/"
"$OD_SDK/bin/mipsel-buildroot-linux-musl-strip" --strip-unneeded "$R/built-libretro/easyrpg_libretro.so"
