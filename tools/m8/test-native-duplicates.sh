#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
Q=/root/lilsmoke-native-duplicates-20261010
mkdir -p "$Q"
cc -std=c11 -O2 -Wall -Wextra -Werror "$P/tools/m8/test-native-duplicates.c" -ldl -o "$Q/host"
"$Q/host"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -std=c11 -O2 -Wall -Wextra -Werror \
    "$P/tools/m8/test-native-duplicates.c" -ldl -o "$Q/mips"
qemu-mipsel -cpu 74Kf -L "$B/staging" "$Q/mips"
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$P/tools/m8/test-native-duplicates.c" -ldl -o "$Q/asan"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$Q/asan"
