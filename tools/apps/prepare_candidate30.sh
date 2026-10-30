#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
payload=$P/build/m8-payload-candidate30
test ! -e "$payload"
install -m 755 "$P/package/lilsmoke-scripts/src/lilsmoke-retroarch" "$B/target/usr/lib/lilsmoke/lilsmoke-retroarch"
mkdir "$payload"
python3 "$P/tools/m8/refresh_candidate_runtime.py" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --payload "$payload" --build "$B" --full-runtime --sync-all
