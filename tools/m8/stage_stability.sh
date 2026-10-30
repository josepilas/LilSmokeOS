#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
for name in lilsmoke-od-launch lilsmoke-retroarch lilsmoke-session-log; do
    install -m 0755 "$P/package/lilsmoke-scripts/src/$name" "$B/target/usr/lib/lilsmoke/$name"
done
out=$P/build/m8-stability-candidate33
test ! -e "$out"
mkdir -p "$out/files"
cp "$B/target/usr/bin/lilsmoke-ui" "$out/files/000000.bin"
cp "$B/target/usr/bin/lilsmoke-keyboard" "$out/files/000001.bin"
cp "$B/target/usr/lib/lilsmoke/lilsmoke-od-launch" "$out/files/000002.bin"
cp "$B/target/usr/lib/lilsmoke/lilsmoke-retroarch" "$out/files/000003.bin"
cp "$B/target/usr/lib/lilsmoke/lilsmoke-session-log" "$out/files/000004.bin"
cp "$P/board/segam-m8-v6-stock/m8-launch.sh" "$out/m8-launch.sh"
printf 'usr/bin/lilsmoke-ui\t000000.bin\nusr/bin/lilsmoke-keyboard\t000001.bin\nusr/lib/lilsmoke/lilsmoke-od-launch\t000002.bin\nusr/lib/lilsmoke/lilsmoke-retroarch\t000003.bin\nusr/lib/lilsmoke/lilsmoke-session-log\t000004.bin\n' > "$out/changes.tsv"
printf '%s\n' "$out"
