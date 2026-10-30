#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=$(printenv OD_BUILD || printf /root/lilsmoke-opendingux-qa)
B=$(printenv OD_BUILDROOT_OUTPUT || printf /root/lilsmoke-os/build/buildroot/output)
mkdir -p "$OUT/fixture/tree"
"$B/host/bin/mipsel-buildroot-linux-musl-gcc" -Os -no-pie -march=mips32r2 -mabi=32 -mtune=74kc \
    -Wall -Wextra -Werror -I"$B/staging/usr/include/SDL2" "$P/tools/opendingux/native-qa.c" \
    -lSDL2 -o "$OUT/fixture/tree/app"
cp "$P/tools/opendingux/native-qa.desktop" "$OUT/fixture/tree/native-qa.desktop"
mksquashfs "$OUT/fixture/tree" "$OUT/fixture/native-qa.opk" -noappend -processors 1 \
    -comp gzip -b 131072 -no-progress
sha256sum "$OUT/fixture/native-qa.opk"
echo "Disposable VM artifact: $OUT/fixture/native-qa.opk"
echo "Install ONLY in the disposable VM /storage/opendingux/apps."
echo "Software pattern, arrow-key tile movement, Start+Select/Enter+RightShift exit, 45-second timeout."
echo "Persistent evidence: HOME/opk-qa.log (OPK_QA_START/READY/KEY/JOY/CHORD/EXIT)."
