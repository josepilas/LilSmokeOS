#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
R=/root/lilsmoke-emulator-expansion-20261008
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O3 -s \
    "$P/package/lilsmoke-scripts/src/lilsmoke-emulators.cpp" -o "$B/target/usr/lib/lilsmoke/lilsmoke-emulators"
install -m 0755 "$R/built-native/bin/lilsmoke-nx-data" "$B/target/usr/lib/lilsmoke/lilsmoke-nx-data"
install -m 0755 "$R/built-native/libod-sdl.so" "$B/target/usr/lib/lilsmoke/libod-sdl.so"
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-od-launch" "$B/target/usr/lib/lilsmoke/"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -static -Os -std=c11 -Wall -Wextra -Werror \
    "$P/board/segam-m8-v6-stock/m8-safe-power.c" -o "$B/target/usr/lib/lilsmoke/m8-safe-power"
make -C /root/lilsmoke-os/build/buildroot O="$B" BR2_EXTERNAL="$P" -j1 lilsmoke-ui-rebuild
python3 "$P/tools/emulators/stage-candidate13.py"
