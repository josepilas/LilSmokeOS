#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
S=$B/build/retroarch-mips-v1.19.1
Q=${1:?usage: build-retroarch-identity-fix.sh NEW_OUTPUT_DIRECTORY}
test ! -e "$Q"
mkdir -p "$Q"
cp "$S/retroarch" "$Q/retroarch.before"
if ! grep -q 'Keep the logical basename for core-info' "$S/runloop.c"; then
    patch -d "$S" -p1 < "$P/package/retroarch-mips/0011-preserve-m8-logical-core-identity.patch"
fi
make -C "$S" -j2 > "$Q/build.log" 2>&1
cp "$S/retroarch" "$Q/retroarch"
"$B/host/bin/mipsel-buildroot-linux-musl-strip" "$Q/retroarch"
"$B/host/bin/mipsel-buildroot-linux-musl-readelf" -h "$Q/retroarch" > "$Q/elf.txt"
sha256sum "$Q/retroarch" "$Q/retroarch.before"
