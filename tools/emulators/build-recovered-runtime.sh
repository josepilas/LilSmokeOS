#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
make -C /root/lilsmoke-os/build/buildroot O="$B" PARALLEL_JOBS="${LILSMOKE_BUILD_JOBS:-1}" -j"${LILSMOKE_BUILD_JOBS:-1}" \
    opendingux-emulators-rebuild lilsmoke-opendingux-rebuild pcsx4all-rebuild
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-od-launch" "$B/target/usr/lib/lilsmoke/"
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-retroarch" "$B/target/usr/lib/lilsmoke/"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O3 -s \
    "$P/package/lilsmoke-scripts/src/lilsmoke-rom.cpp" -larchive -lsodium -o "$B/target/usr/lib/lilsmoke/lilsmoke-rom"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O3 -s \
    "$P/package/lilsmoke-scripts/src/lilsmoke-emulators.cpp" -o "$B/target/usr/lib/lilsmoke/lilsmoke-emulators"
