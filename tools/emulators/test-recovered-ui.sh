#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
name=${1:?provide a new evidence directory}
case "$name" in ''|*[!a-z0-9-]*) exit 2 ;; esac
python3 "$P/tools/m8/candidate_vm_smoke.py" --build "$B" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --qemu /root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
    --archive '/path/to/local-input/Super Mario World (PT-BR) - [snesforever.com.br].7z' \
    --extended-controls --fat-persistence --retroarch-menu --menu-cycles 1 --pointer-smoke --opendingux --pcsx-menu \
    --out "$P/test-results/m8-candidate08-2026-10-07/$name"
