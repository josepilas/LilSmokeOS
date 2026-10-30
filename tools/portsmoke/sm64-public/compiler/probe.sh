#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=/root/lilsmoke-public-compiler
SDK=/root/lilsmoke-os/build/buildroot/output
TOOLS="$WORK/runtime/wrappers"
GCC="$WORK/runtime/usr/lib/gcc/mipsel-linux-gnu/12"
export QEMU_LD_PREFIX="$SDK/staging"
export QEMU_CPU=74Kf
CRT1=$("$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" -print-file-name=crt1.o)
CRTI=$("$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" -print-file-name=crti.o)
CRTN=$("$SDK/host/bin/mipsel-buildroot-linux-musl-gcc" -print-file-name=crtn.o)
mkdir -p "$WORK/probe"
/usr/bin/time -v "$TOOLS/cc" -O2 -mips32r2 -mabi=32 -mhard-float \
    -nostdinc -isystem "$GCC/include" -isystem "$SDK/staging/usr/include" \
    -c "$HERE/probe.c" -o "$WORK/probe/probe.o" 2> "$WORK/probe/compile.log"
"$TOOLS/ld" -m elf32ltsmip --dynamic-linker /lib/ld-musl-mipsel.so.1 \
    -o "$WORK/probe/probe" "$CRT1" \
    "$CRTI" "$WORK/probe/probe.o" \
    -L "$SDK/staging/usr/lib" -lc "$CRTN"
/usr/bin/qemu-mipsel -cpu 74Kf -L "$SDK/staging" "$WORK/probe/probe"
"$SDK/host/bin/mipsel-buildroot-linux-musl-readelf" -l "$WORK/probe/probe" | sed -n '/interpreter/p'
printf '%s\n' 'PASS: native MIPS compiler produced and ran a musl executable.'
