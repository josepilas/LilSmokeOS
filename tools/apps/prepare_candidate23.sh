#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
test ! -e "$P/build/m8-payload-candidate23"
cp -a "$P/build/m8-payload-candidate22" "$P/build/m8-payload-candidate23"
python3 "$P/tools/m8/refresh_candidate_runtime.py" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --payload "$P/build/m8-payload-candidate23" --build "$B" --runtime-path usr/bin/retroarch
