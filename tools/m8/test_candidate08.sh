#!/bin/sh
# Refresh generated runtime only, then test a disposable MIPS card copy.
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
QA=/root/lilsmoke-m8-bringup-2026-10-06
name=${1:?provide a new evidence directory name}
mode=${2:-all}
case "$mode" in all|focus|native) ;; *) exit 2 ;; esac
case "$name" in *[!a-z0-9-]*|'') exit 2 ;; esac
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-launch" "$B/target/usr/lib/lilsmoke/"
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-storage" "$B/target/usr/lib/lilsmoke/"
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-retroarch" "$B/target/usr/lib/lilsmoke/"
install -m 0755 "$P/package/lilsmoke-scripts/src/lilsmoke-od-launch" "$B/target/usr/lib/lilsmoke/"
install -m 0755 "$P/package/opendingux-emulators/src/od-launch" "$B/target/usr/lib/lilsmoke/"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O3 -s \
    "$P/package/lilsmoke-scripts/src/lilsmoke-emulators.cpp" -o "$B/target/usr/lib/lilsmoke/lilsmoke-emulators"
runtime=$(mktemp -d "$QA/runtime08-XXXXXX")
export PYTHONPATH="$P/tools/m8"
python3 - "$B/target" "$runtime" "$P/build/m8-payload-candidate08" <<'PY'
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
set --
if [ "$mode" = all ]; then
    set -- --gba "$P/../../../work/m8-added-roms/gba.zip" --md "$P/../../../work/m8-added-roms/md.zip"
fi
if [ "$mode" = native ]; then set -- --gba "$P/../../../work/m8-added-roms/gba.zip" --native-new-menus --native-subset "${LILSMOKE_NATIVE_SUBSET:-nxengine,easyrpg,scummvm,mgba}"; fi
if [ -n "${LILSMOKE_OPK_QA:-}" ]; then set -- "$@" --opk-qa "$LILSMOKE_OPK_QA"; fi
if [ -n "${LILSMOKE_QA_ADDONS:-}" ]; then set -- "$@" --addons "$LILSMOKE_QA_ADDONS"; fi
python3 "$P/tools/m8/candidate_vm_smoke.py" --build "$B" \
    --image "$P/build/LilSmokeOS-SEGAM-M8-V6-CANDIDATE-08.img" \
    --qemu /root/lilsmoke-performance/qemu-profile/build/qemu-system-mipsel \
    --archive '/path/to/local-input/Super Mario World (PT-BR) - [snesforever.com.br].7z' \
    "$@" \
    --extended-controls --fat-persistence --retroarch-menu --menu-cycles 3 --pointer-smoke --opendingux \
    --out "$P/test-results/m8-candidate08-2026-10-07/$name"
