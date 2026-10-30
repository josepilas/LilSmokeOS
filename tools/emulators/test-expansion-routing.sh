#!/bin/sh
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
QA=/root/lilsmoke-emulator-expansion-20261008/routing-qa
report=$P/test-results/emulator-expansion-2026-10-08
mkdir -p "$QA" "$report"
src=$P/package/lilsmoke-scripts/src
g++ -std=c++17 -O2 -Wall -Wextra "$src/lilsmoke-rom.cpp" -larchive -lsodium -o "$QA/resolver-host"
"$B/host/bin/mipsel-buildroot-linux-musl-g++" -std=c++17 -O2 -Wall -Wextra \
    "$src/lilsmoke-rom.cpp" -larchive -lsodium -o "$QA/resolver-mips"
python3 "$P/tools/m8/test_rom_archives.py" --resolver "$QA/resolver-host" > "$report/archives-host.txt" 2>&1
tail -n 6 "$report/archives-host.txt"
python3 "$P/tools/m8/test_rom_archives.py" --resolver "$QA/resolver-mips" \
    --mips --sysroot "$B/staging" > "$report/archives-mips.txt" 2>&1
tail -n 6 "$report/archives-mips.txt"
sh "$P/tools/emulators/test_registry.sh" > "$report/registry.txt" 2>&1
cat "$report/registry.txt"
