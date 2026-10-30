#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=$(mktemp -d /root/lilsmoke-stability-test-XXXXXX)
S=$P/package/lilsmoke-ui/src/src
g++ -std=c++17 -O2 -I"$S" "$P/tools/m8/test_persistence.cpp" "$S/Config.cpp" "$S/Log.cpp" \
    -Wl,--wrap=fsync -o "$Q/host-test"
"$Q/host-test" "$Q/host-files"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -I"$S" \
    "$P/tools/m8/test_persistence.cpp" "$S/Config.cpp" "$S/Log.cpp" \
    -Wl,--wrap=fsync -o "$Q/mips-test"
QEMU_CPU=74Kf qemu-mipsel -L "$B/staging" "$Q/mips-test" "$Q/mips-files"
helper=$P/package/lilsmoke-scripts/src/lilsmoke-session-log
result=$(LILSMOKE_LOG_DIR=/proc/invalid-logs LILSMOKE_RECOVERY_LOG_DIR="$Q/recovery" \
    LILSMOKE_RAM_LOG_DIR="$Q/ram" sh "$helper" sm64-public)
test "$result" = "$Q/recovery/sm64-public.log"
result=$(LILSMOKE_LOG_DIR=/proc/invalid-logs LILSMOKE_RECOVERY_LOG_DIR=/proc/invalid-logs \
    LILSMOKE_RAM_LOG_DIR="$Q/ram" sh "$helper" retroarch)
test "$result" = "$Q/ram/retroarch.log"
for source in lilsmoke-retroarch lilsmoke-od-launch lilsmoke-session-log; do
    sh -n "$P/package/lilsmoke-scripts/src/$source"
done
printf '%s\n' "PASS writable recovery/RAM logs and launcher shell syntax; $Q"
