#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Rebuild input consumers only; retain all emulator core archives and ROMs.
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
OUT=${LILSMOKE_INPUT_BUILD_OUT:-/root/lilsmoke-input-consumers-20261018}
CC=$B/host/bin/mipsel-buildroot-linux-musl-gcc
CXX=$B/host/bin/mipsel-buildroot-linux-musl-g++
STRIP=$B/host/bin/mipsel-buildroot-linux-musl-strip
I=$P/package/lilsmoke-ui/src/src
if [ "${LILSMOKE_INPUT_RESUME_PCSX:-0}" != 1 ]; then
test ! -e "$OUT"
mkdir -p "$OUT/od-sdl"
# The daemon alone owns HID interfaces. Graphics/audio configuration is unchanged.
"$CC" -O2 -mips32r2 -mtune=74kc -mfp32 -mhard-float -mdspr2 -std=gnu11 -Wall -Wextra \
    -I"$B/staging/usr/include/SDL2" -I"$I" \
    "$P/package/lilsmoke-usb-gamepads/src/usb-gamepads.c" -lSDL2 -o "$OUT/lilsmoke-usb-gamepads"
"$STRIP" "$OUT/lilsmoke-usb-gamepads"
install -m 0755 "$OUT/lilsmoke-usb-gamepads" "$B/target/usr/bin/lilsmoke-usb-gamepads"
pocket=$B/build/opendingux-emulators-6fff59072821a68859d0d4d5caeec36d02ea264f
make -C "$pocket" -f "$P/package/opendingux-emulators/Makefile.lilsmoke" -j1 \
    CC="$CC" CXX="$CXX" TARGET_CFLAGS="-O3 -mips32r2 -mfp32 -mhard-float -mdspr2" \
    TARGET_LDFLAGS="" SDL_CFLAGS="-I$B/staging/usr/include/SDL" \
    SDL_LIBS="-L$B/staging/usr/lib -lSDL"
"$STRIP" --strip-unneeded "$pocket/pocketsnes/pocketsnes.dge"
install -m 0755 "$pocket/pocketsnes/pocketsnes.dge" "$B/target/usr/lib/lilsmoke/od-emulators/pocketsnes"
ra=$B/build/retroarch-mips-v1.19.1
for name in m8_lilsmoke_input.c m8_lilsmoke_input.h; do
    install -m 0644 "$P/package/retroarch-mips/$name" "$ra/input/drivers_joypad/$name"
done
install -m 0644 "$I/gamepad-state.h" "$ra/input/drivers_joypad/gamepad-state.h"
make -C "$ra" -j1
"$STRIP" --strip-unneeded "$ra/retroarch"
install -m 0755 "$ra/retroarch" "$B/target/usr/bin/retroarch"
make -C "$OUT/od-sdl" -f "$P/package/lilsmoke-od-sdl/Makefile" -j1 \
    PROJECT="$P" CC="$CC" CXX="$CXX" CFLAGS="-O2 -mips32r2 -mfp32 -mhard-float -mdspr2" \
    CXXFLAGS="-O2 -mips32r2 -mfp32 -mhard-float -mdspr2" \
    SDL2_INCLUDE="$B/staging/usr/include/SDL2" SDL12_INCLUDE="$B/staging/usr/include/SDL"
"$STRIP" --strip-unneeded "$OUT/od-sdl/libod-sdl.so"
install -m 0755 "$OUT/od-sdl/libod-sdl.so" "$B/target/usr/lib/lilsmoke/libod-sdl.so"
else
    test -f "$OUT/od-sdl/libod-sdl.so"
fi
pcsx=/root/lilsmoke-psx-20261010/native
test -f "$pcsx/libpcsx4all_rkgame.a"
printf '%s  %s\n' 1dd1db27ebf46965cbd24b78880337fe60be571a3906e5187ade52090bc1359a \
    "$pcsx/libpcsx4all_rkgame.a" | sha256sum -c -
sha256sum "$pcsx/libpcsx4all_rkgame.a" >"$OUT/pcsx-core-qualified.sha256"
"$CXX" -std=c++17 -O3 -mips32r2 -mplt -mno-shared \
    -include "$P/package/lilsmoke-od-sdl/src/input-wire.h" \
    -I"$P/package/pcsx4all/src/src/port/rkgame" -I"$I" \
    -I"$P/package/lilsmoke-scripts/src" -I"$B/staging/usr/include/SDL2" \
    "$P/package/pcsx4all/src/pcsx4all-launch.cpp" "$P/package/pcsx4all/src/native-menu.cpp" \
    "$I/Input.cpp" "$I/Keymap.cpp" "$I/Log.cpp" "$pcsx/libpcsx4all_rkgame.a" \
    -no-pie -Wl,-Ttext-segment=0x40000000 -lSDL2 -lSDL2_ttf -lpng -lz -lm -lpthread -lrt \
    -o "$OUT/pcsx4all-launch"
"$STRIP" --strip-unneeded "$OUT/pcsx4all-launch"
sha256sum -c "$OUT/pcsx-core-qualified.sha256"
python3 "$P/tools/apps/build_pcsx4all.py" --binary "$OUT/pcsx4all-launch" \
    --output "$OUT/PCSX4ALL-1.0.3.lilpck" --replace
install -m 0644 "$OUT/PCSX4ALL-1.0.3.lilpck" "$B/target/usr/share/lilsmoke/builtin-apps/"
sha256sum "$B/target/usr/bin/lilsmoke-usb-gamepads" "$B/target/usr/bin/retroarch" \
    "$B/target/usr/lib/lilsmoke/libod-sdl.so" \
    "$B/target/usr/lib/lilsmoke/od-emulators/pocketsnes" >"$OUT/SHA256SUMS"
echo "Input consumer builds finished: $OUT"

