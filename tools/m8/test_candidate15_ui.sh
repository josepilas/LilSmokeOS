#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
image=$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img
name=${1:-ui-regba}
case "$name" in *[!a-z0-9-]*|'') exit 2 ;; esac
if [ "${LILSMOKE_SKIP_IMAGE_REFRESH:-0}" != 1 ]; then
    python3 "$P/tools/m8/refresh_candidate_runtime.py" --image "$image" \
        --payload "$P/build/m8-payload-candidate15" --build "$B" --sync-all
fi
python3 "$P/tools/m8/candidate_vm_smoke.py" --build "$B" --image "$image" \
    --qemu /root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
    --archive '/path/to/local-input/Super Mario World (PT-BR) - [snesforever.com.br].7z' \
    --addons "$P/build/candidate15-addons/rom-partition" \
    --gba "$P/../../../work/m8-added-roms/gba.zip" \
    --user-gba-bios '/path/to/local-input/gba_bios.bin' \
    --native-new-menus --native-subset regba --native-only --fat-persistence \
    --out "$P/test-results/opendingux-pico-gba-2026-10-09/$name"
