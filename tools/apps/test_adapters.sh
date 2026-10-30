#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
Q=/root/lilsmoke-applications-20261009
mkdir -p "$Q/built-native"
install -m 0755 "$Q/input-adapter/libod-sdl.so" "$Q/built-native/libod-sdl.so"
mkdir -p "$Q/engine-build/sdl-qa"
ln -sf /root/lilsmoke-emulator-expansion-20261008/engine-build/sdl-qa/libSDL2-2.0.so.0 "$Q/engine-build/sdl-qa/libSDL2-2.0.so.0"
ln -sf /root/lilsmoke-emulator-expansion-20261008/engine-sysroot/usr/lib/libSDL-1.2.so.0 "$Q/engine-build/sdl-qa/libSDL-1.2.so.0"
OD_BUILD_ROOT="$Q" bash "$P/tools/emulators/od-engines/test-production.sh"
OD_BUILD_ROOT="$Q" bash "$P/tools/emulators/od-engines/test-shim.sh"
