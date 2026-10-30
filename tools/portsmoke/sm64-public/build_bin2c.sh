#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build only this asset-free tool, serially. Never installs or touches the SD.
# Usage: sh build_bin2c.sh OUTPUT_DIRECTORY
# SDK and HOST_CC may override the SDK root and host compiler executable.
set -eu
if [ "$#" -ne 1 ] || [ -z "$1" ]; then
    printf '%s\n' 'Usage: build_bin2c.sh OUTPUT_DIRECTORY' >&2
    exit 2
fi
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
HOST_CC=${HOST_CC:-cc}
mkdir -p -- "$1"
OUT=$(CDPATH= cd -- "$1" && pwd)
"$HOST_CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
    "$HERE/bin2c.c" -o "$OUT/bin2c-host"
"$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" \
    -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
    -mips32r2 -mtune=74kc -mabi=32 -mfp32 -mhard-float -static -s \
    "$HERE/bin2c.c" -o "$OUT/bin2c-mips"
"$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" \
    -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
    -mips32r2 -mtune=74kc -mabi=32 -mfp32 -mhard-float -s \
    "$HERE/bin2c.c" -o "$OUT/sm64-bin2c"
printf 'Host: %s\nMIPS (static musl): %s\nMIPS (dynamic musl): %s\n' \
    "$OUT/bin2c-host" "$OUT/bin2c-mips" "$OUT/sm64-bin2c"
