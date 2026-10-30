#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
payload=$P/build/m8-payload-candidate21
test ! -e "$payload" || { echo 'Candidate21 already exists; inspect before regenerating.' >&2; exit 1; }
mkdir "$payload"
python3 "$P/tools/m8/refresh_candidate_runtime.py" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --payload "$payload" --build "$B" --full-runtime --sync-all
python3 "$P/tools/m8/seed_psx_addon.py" --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --addon "$P/build/psx-addon-candidate20"
