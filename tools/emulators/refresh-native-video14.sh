#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
R=/root/lilsmoke-emulator-expansion-20261008
cp "$R/adapter/libod-sdl.so" "$R/built-native/libod-sdl.so"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$R/built-native/libod-sdl.so"
install -m 0755 "$R/built-native/libod-sdl.so" "$B/target/usr/lib/lilsmoke/libod-sdl.so"
python3 "$P/tools/m8/refresh_candidate_runtime.py" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --payload "$P/build/m8-payload-candidate14" --build "$B" \
    --runtime-path usr/lib/lilsmoke/libod-sdl.so
