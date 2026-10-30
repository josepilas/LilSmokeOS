#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
[ "$#" = 3 ] || { echo 'usage: build_components.sh BUILDROOT_OUTPUT SELECTED_STOCK NEW_OUTPUT' >&2; exit 2; }
SDK=$(realpath "$1")
STOCK=$(realpath "$2")
OUT=$3
HERE=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
[ ! -e "$OUT" ] || { echo 'output must be new' >&2; exit 2; }
mkdir -p "$OUT/payload"
OUT=$(realpath "$OUT")
CC=$SDK/host/bin/mipsel-buildroot-linux-musl-gcc
READELF=$SDK/host/bin/mipsel-buildroot-linux-musl-readelf
STRIP=$SDK/host/bin/mipsel-buildroot-linux-musl-strip
LIBC=$STOCK/rootfs/lib/libc.so.6
[ -x "$CC" ] && [ -s "$LIBC" ] || { echo 'missing compiler or exact stock glibc' >&2; exit 2; }
# Only freestanding C objects use this compiler. -nostdlib and the explicit
# stock glibc input prevent any musl/crt/libgcc runtime from entering the plugin.
"$CC" -std=c99 -O2 -mips32r2 -mfp32 -mhard-float -fPIC -G0 -fno-stack-protector -fno-builtin \
    -Wall -Wextra -Werror -c "$HERE/board/r36sx-2.7-stock/bootstrap.c" -o "$OUT/bootstrap.o"
"$CC" -shared -nostdlib -Wl,--no-undefined -Wl,-soname,libemu_md.so \
    "$OUT/bootstrap.o" "$LIBC" -o "$OUT/libemu_md.glibc-fork.so"
"$READELF" -h -A -d -V "$OUT/libemu_md.glibc-fork.so" > "$OUT/bootstrap-elf.txt"
grep -q 'Shared library: \[libc.so.6\]' "$OUT/bootstrap-elf.txt"
if grep -q -e musl -e ld-musl "$OUT/bootstrap-elf.txt"; then echo 'musl leaked into bootstrap' >&2; exit 1; fi
[ "$(grep -c '(NEEDED)' "$OUT/bootstrap-elf.txt")" = 1 ] || { echo 'unexpected bootstrap dependencies' >&2; exit 1; }
"$CC" -std=c99 -O2 -mips32r2 -mfp32 -mhard-float -fPIC -G0 -fno-stack-protector -fno-builtin \
    -Wall -Wextra -Werror -shared -nostdlib -nostartfiles -nodefaultlibs -Wl,--no-undefined \
    -Wl,-soname,libemu_md.so "$HERE/board/r36sx-2.7-stock/bootstrap_exec.c" -o "$OUT/payload/libemu_md.so"
"$READELF" -h -A -d "$OUT/payload/libemu_md.so" > "$OUT/bootstrap-exec-elf.txt"
if grep -q '(NEEDED)' "$OUT/bootstrap-exec-elf.txt"; then echo 'freestanding bootstrap has dependencies' >&2; exit 1; fi
"$CC" -static -O2 -Wall -Wextra -Werror "$HERE/board/segam-m8-v6-stock/m8-probe.c" -o "$OUT/payload/m8-probe"
"$CC" -static -O2 -Wall -Wextra -Werror "$HERE/board/segam-m8-v6-stock/m8-grow.c" -o "$OUT/payload/m8-grow"
"$CC" -std=c99 -static -Os -Wall -Wextra -Werror "$HERE/board/r36sx-2.7-stock/r36-bootlog.c" -o "$OUT/payload/r36-bootlog"
"$CC" -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror \
    -I"$SDK/staging/usr/include/SDL" \
    "$HERE/board/r36sx-2.7-stock/r36-display.c" "$HERE/board/r36sx-2.7-stock/r36-audio-compat.c" \
    -ldl -o "$OUT/payload/libr36-display.so"
"$CC" -std=c11 -O2 -static -Wall -Wextra -Werror \
    "$HERE/board/r36sx-2.7-stock/r36-input.c" -o "$OUT/payload/r36-input"
"$CC" -std=c11 -Os -static -Wall -Wextra -Werror -I"$SDK/staging/usr/include/SDL" \
    "$HERE/board/r36sx-2.7-stock/r36-safe-power.c" "$HERE/board/r36sx-2.7-stock/r36-power-ui.c" \
    "$HERE/board/r36sx-2.7-stock/r36-display.c" -ldl -o "$OUT/payload/m8-safe-power"
for name in libemu_md.so libr36-display.so r36-input m8-probe m8-grow m8-safe-power r36-bootlog; do "$STRIP" "$OUT/payload/$name"; done
cp "$HERE/board/r36sx-2.7-stock/r36-launch.sh" "$OUT/payload/r36-launch.sh"
cp "$HERE/board/r36sx-2.7-stock/r36-entry.sh" "$OUT/payload/r36-entry.sh"
chmod 0755 "$OUT/payload/r36-input" "$OUT/payload/r36-launch.sh"
sha256sum "$LIBC" "$CC" "$HERE/board/r36sx-2.7-stock/"* > "$OUT/inputs.sha256"
sha256sum "$OUT/payload/"* > "$OUT/payload.sha256"
echo 'PASS freestanding stock-ABI exec plugin, stock-glibc fork alternative, standalone musl helpers and display shim; no device or SDK target writes'
