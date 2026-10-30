#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(printenv OD_BUILD || printf /root/lilsmoke-opendingux-qa)
B=$(printenv OD_BUILDROOT_OUTPUT || printf /root/lilsmoke-os/build/buildroot/output)
S="$P/package/lilsmoke-opendingux"
mkdir -p "$OUT/unit"
for mode in host sanitize mips; do
    CC=cc
    FLAGS="-O2"
    if [ "$mode" = sanitize ]; then
        FLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
    elif [ "$mode" = mips ]; then
        CC="$B/host/bin/mipsel-buildroot-linux-musl-gcc"
        FLAGS="-Os -march=mips32r2 -mabi=32"
    fi
    "$CC" -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DOD_TEST_NO_SYSTEM_DEPS \
        -I"$S/vendor/libini" $FLAGS -std=c99 -Wall -Wextra -Werror \
        "$P/tools/opendingux/unit.c" "$S/src/common.c" "$S/src/desktop.c" \
        "$S/src/elf.c" "$S/src/sha256.c" "$S/vendor/libini/libini.c" \
        -lsodium -o "$OUT/unit/$mode"
    if [ "$mode" = mips ]; then
        qemu-mipsel -cpu 74Kf -L "$B/staging" "$OUT/unit/$mode"
    else
        "$OUT/unit/$mode"
    fi
done
