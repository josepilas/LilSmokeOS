#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
[ "$#" = 1 ] || { echo 'Usage: build_shell.sh OUTPUT_DIRECTORY' >&2; exit 2; }
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
mkdir -p "$1"
OUT=$(CDPATH= cd -- "$1" && pwd)
cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic "$HERE/build_shell.c" -o "$OUT/build-shell-host"
cc -shared -fPIC -O2 -Wall -Wextra -Werror "$HERE/test_clock_legacy.c" -o "$OUT/legacy-host.so"
"$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
    -mips32r2 -mtune=74kc -mabi=32 -mfp32 -mhard-float -s \
    "$HERE/build_shell.c" -o "$OUT/sm64-build-shell"
"$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" -shared -fPIC -O2 -Wall -Wextra -Werror \
    -mips32r2 -mtune=74kc -mabi=32 -mfp32 -mhard-float \
    "$HERE/test_clock_legacy.c" -o "$OUT/legacy-mips.so"
