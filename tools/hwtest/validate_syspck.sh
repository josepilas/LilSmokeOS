#!/bin/bash
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd "$(dirname "$0")/../.."
out=test-results/syspck
mkdir -p "$out"
g++ -std=c++17 -Os -Wall -Wextra -Werror package/lilsmoke-syspck/src/syspck.cpp \
    -larchive -lfdt -lsodium -o "$out/lilsmoke-syspck-host"
python3 tools/hwtest/syspck_smoke.py > "$out/host-tests.log" 2>&1
cp "$out/checks.json" "$out/host-checks.json"
SYSPCK_BINARY="$out/lilsmoke-syspck-mips" \
SYSPCK_RUNNER='["qemu-mipsel","-cpu","74Kf","-L","/root/lilsmoke-os/build/buildroot/output/target"]' \
    python3 tools/hwtest/syspck_smoke.py > "$out/mips-tests.log" 2>&1
cp "$out/checks.json" "$out/mips-checks.json"
tail -n 1 "$out/host-tests.log"
tail -n 1 "$out/mips-tests.log"
echo SYSPCK_NATIVE_VALIDATION_OK
