#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
QA=/root/lilsmoke-emulator-expansion-20261008/routing-qa
src=$P/package/lilsmoke-ui/src/src
for mode in host mips; do
    cxx=g++
    [ "$mode" != mips ] || cxx=$B/host/bin/mipsel-buildroot-linux-musl-g++
    "$cxx" -std=c++17 -O2 -Wall -Wextra -I"$src" -I"$P/package/lilsmoke-scripts/src" \
        "$P/tools/emulators/playlists-test.cpp" "$src/RetroArchPlaylists.cpp" "$src/Log.cpp" -o "$QA/playlists-$mode"
    if [ "$mode" = mips ]; then qemu-mipsel -cpu 74Kf -L "$B/staging" "$QA/playlists-$mode";
    else "$QA/playlists-$mode"; fi
done
