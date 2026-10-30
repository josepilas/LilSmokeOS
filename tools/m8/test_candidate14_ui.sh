#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
image=$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img
python3 "$P/tools/m8/refresh_candidate_runtime.py" --image "$image" \
    --payload "$P/build/m8-payload-candidate14" --build "$B" --sync-all
python3 "$P/tools/m8/candidate_vm_smoke.py" --build "$B" --image "$image" \
    --qemu /root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
    --archive '/path/to/local-input/Super Mario World (PT-BR) - [snesforever.com.br].7z' \
    --addons "$P/build/candidate14-addons/rom-partition" \
    --extended-controls --fat-persistence --retroarch-menu --menu-cycles 3 --opendingux \
    --out "$P/test-results/opendingux-native-video-2026-10-09/ui-regression"
python3 "$P/tools/m8/candidate_vm_smoke.py" --build "$B" --image "$image" \
    --qemu /root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
    --archive '/path/to/local-input/Super Mario World (PT-BR) - [snesforever.com.br].7z' \
    --addons "$P/build/candidate14-addons/rom-partition" \
    --gba "$P/../../../work/m8-added-roms/gba.zip" \
    --native-new-menus --native-subset nxengine,easyrpg,scummvm,mgba --native-only --fat-persistence \
    --out "$P/test-results/opendingux-native-video-2026-10-09/ui-native-apps"
