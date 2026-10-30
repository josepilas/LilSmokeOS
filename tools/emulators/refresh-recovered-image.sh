#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
ROOT=/root/lilsmoke-emulator-expansion-20261008
bash "$P/tools/emulators/od-engines/build.sh" shim
install -m 0755 "$ROOT/built-native/libod-sdl.so" "$B/target/usr/lib/lilsmoke/"
stage=$(mktemp -d "$ROOT/runtime-refresh-XXXXXX")
export PYTHONPATH="$P/tools/m8"
python3 - "$B/target" "$stage" "$P/build/m8-payload-candidate08" <<'PY'
from pathlib import Path
import shutil
import sys
from build_stock_candidate import prepare_runtime
target, stage, payload = map(Path, sys.argv[1:])
print(prepare_runtime(target, stage))
shutil.copytree(stage / 'files', payload / 'files', dirs_exist_ok=True)
shutil.copyfile(stage / 'runtime.tsv', payload / 'runtime.tsv')
PY
python3 "$P/tools/m8/refresh_candidate_runtime.py" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --payload "$P/build/m8-payload-candidate08" --build "$B" --sync-all
