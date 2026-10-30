#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
python3 "$P/tools/m8/candidate_vm_smoke.py" \
    --build /root/lilsmoke-os/build/buildroot/output \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --qemu /root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
    --archive '/path/to/local-input/Super Mario World (PT-BR) - [snesforever.com.br].7z' \
    --out "$P/test-results/lilsmoke-applications-style-2026-10-09/full-ui-verified" \
    --fat-persistence --applications "$P/build/lilsmoke-applications" \
    --application-version 1.2.0 --application-update-version 1.3.0
