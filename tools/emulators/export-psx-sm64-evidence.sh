#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
Q=/root/lilsmoke-psx-20261010
S=/root/lilsmoke-sm64-progress-20261010
OUT=$P/test-results/psx-sm64-progress-2026-10-10
mkdir -p "$OUT/sm64" "$OUT/psx" "$OUT/adapter"
cp "$S/tests/result.json" "$OUT/sm64/interactions-audio.json"
cp "$S/tests/host.log" "$S/tests/mips.log" "$S/tests/asan.log" "$OUT/sm64/"
cp "$S/test-mips/result.json" "$OUT/sm64/render-mips.json"
cp "$S/test-mips/"*.png "$OUT/sm64/"
cp /root/psx-regression-local-20261010/result.json "$OUT/psx/result.json"
cp /root/psx-regression-local-20261010/native.log /root/psx-regression-local-20261010/libretro.log "$OUT/psx/"
cp /root/psx-regression-local-20261010/native.ppm /root/psx-regression-local-20261010/last-frame.ppm "$OUT/psx/"
cp "$Q/engine-logs/shim-qa.log" "$Q/engine-logs/production-qa.log" "$OUT/adapter/"
mkdir -p "$OUT/addons/emulators/libretro" "$OUT/addons/emulators/info" "$OUT/addons/emulators/licenses/pcsx_rearmed"
cp "$Q/addons/emulators/libretro/pcsx_rearmed_libretro.so" "$OUT/addons/emulators/libretro/"
cp "$Q/addons/emulators/info/pcsx_rearmed_libretro.info" "$OUT/addons/emulators/info/"
cp "$Q/addons/emulators/licenses/pcsx_rearmed/"* "$OUT/addons/emulators/licenses/pcsx_rearmed/"
printf '%s\n' "Evidence exported to $OUT"
