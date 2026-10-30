#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
ROOT=/root/lilsmoke-emulator-expansion-20261008
QA=$ROOT/core-qa
report=$P/test-results/emulator-expansion-2026-10-08
mkdir -p "$QA/user-content" "$report"
export LILSMOKE_ARCHIVE_CACHE="$QA/user-content"
export LILSMOKE_BENCH_INPUT=1
resolver=$ROOT/routing-qa/resolver-mips
cd "$QA"
for spec in snes:snes9x_2002 gba:gpsp md:picodrive; do
    system=${spec%:*}; core=${spec#*:}
    case "$system" in
        snes) archive='/path/to/local-input/Super Mario World (PT-BR) - [snesforever.com.br].7z' ;;
        gba) archive="$P/../../../work/m8-added-roms/gba.zip" ;;
        md) archive="$P/../../../work/m8-added-roms/md.zip" ;;
    esac
    rom=$(qemu-mipsel -cpu 74Kf -L "$B/staging" "$resolver" "$system" "$archive")
    timeout 180 \
        qemu-mipsel -cpu 74Kf -L "$B/staging" "$QA/core-benchmark" \
        "$ROOT/built-libretro/${core}_libretro.so" "$rom" 600 1 \
        > "$report/game-$core.txt" 2>&1
    cat "$report/game-$core.txt"
    test -s last-frame.ppm
    cp last-frame.ppm "$report/$core-frame.ppm"
    convert last-frame.ppm "$report/$core-frame.png"
done
printf '%s\n' 'QEMU functional diagnostics, NOT real-device FPS or RAM measurements.'
