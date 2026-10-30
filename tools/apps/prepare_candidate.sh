#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
payload=$P/build/m8-payload-candidate16
if [ -e "$payload" ]; then
    printf 'Candidate16 payload already exists; inspect it before regenerating.\n' >&2
    exit 1
fi
mkdir "$payload"
python3 "$P/tools/m8/refresh_candidate_runtime.py" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --payload "$payload" --build "$B" --full-runtime --sync-all
