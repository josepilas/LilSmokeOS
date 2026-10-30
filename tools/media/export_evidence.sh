#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=$P/test-results/media-sm64-2026-10-12
mkdir -p "$out"
cp /root/lilsmoke-media-final-asan-qa/report.json "$out/host-asan.json"
cp /root/lilsmoke-media-final-qa/report.json "$out/host-playback.json"
for pair in 'media-explorer-vm-d video' 'media-explorer-vm-audio music' 'media-explorer-vm-photo photos'; do
    set -- $pair
    source=/root/$1
    dest=$out/mips-$2
    mkdir -p "$dest"
    cp "$source/report.json" "$source/ui.log" "$source/media-player.log" \
       "$source/process.log" "$source/memory.txt" "$source"/*.png "$dest/"
done
cp "$P/tools/portsmoke/sm64-original/_build/quality-20261012/delivery.json" "$out/sm64-quality.json"
