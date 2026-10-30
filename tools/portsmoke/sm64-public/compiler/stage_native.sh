#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=/root/lilsmoke-public-compiler
SDK=/root/lilsmoke-os/build/buildroot/output
INPUT=${1:?provide the directory of verified Debian mipsel packages}
mkdir -p "$WORK/runtime/wrappers"
found=0
for archive in "$INPUT"/*_mipsel.deb; do
    [ -f "$archive" ] || continue
    [ "$(dpkg-deb -f "$archive" Architecture)" = mipsel ] || exit 1
    dpkg-deb -x "$archive" "$WORK/runtime"
    found=$((found + 1))
done
[ "$found" -eq 16 ] || { printf '%s\n' 'Incomplete native compiler dependency set' >&2; exit 1; }
make_archive="$WORK/debs/make_4.3-4.1_mipsel.deb"
[ -f "$make_archive" ] || { printf '%s\n' 'Verified native GNU make package missing' >&2; exit 1; }
dpkg-deb -x "$make_archive" "$WORK/runtime"
"$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" -std=c11 -Os -Wall -Wextra \
    "$HERE/native_tool.c" -o "$WORK/native-tool"
for name in cc gcc cpp cc1 as ld objcopy collect2 make; do
    cp "$WORK/native-tool" "$WORK/runtime/wrappers/$name"
done
file "$WORK/runtime/usr/lib/gcc/mipsel-linux-gnu/12/cc1"
printf '%s\n' 'Native compiler staged; only compiler dependencies use glibc.'
