#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=test-results/storage-layout
tc=/root/lilsmoke-os/build/buildroot/output/host
cc=$tc/bin/mipsel-buildroot-linux-musl-gcc
mkdir -p "$out"
"$cc" -static -Os -mips32r2 -Wall -Wextra -Werror \
    package/lilsmoke-scripts/src/lilsmoke-disk.c -o "$out/lilsmoke-disk-mips"
"$tc/bin/mipsel-buildroot-linux-musl-strip" "$out/lilsmoke-disk-mips"
"$tc/bin/mipsel-buildroot-linux-musl-readelf" -h "$out/lilsmoke-disk-mips"
python3 - "$out/mips-geometry.img" <<'PY'
import struct, sys
from pathlib import Path
p=Path(sys.argv[1])
m=bytearray(512)
m[510:]=b'\x55\xaa'
struct.pack_into('<B3sB3sII',m,446,128,b'\0'*3,12,b'\0'*3,2048,1048576)
struct.pack_into('<B3sB3sII',m,462,0,b'\0'*3,7,b'\0'*3,1050624,260096)
with p.open('wb') as f:
    f.write(m)
    f.truncate(896*1024*1024)
PY
qemu-mipsel -cpu 74Kf "$out/lilsmoke-disk-mips" info "$out/mips-geometry.img"
qemu-mipsel -cpu 74Kf "$out/lilsmoke-disk-mips" grow "$out/mips-geometry.img"
qemu-mipsel -cpu 74Kf "$out/lilsmoke-disk-mips" info "$out/mips-geometry.img" | grep -q 'p2_sectors=784384'
rm "$out/mips-geometry.img"
for script in package/lilsmoke-scripts/src/lilsmoke-{storage,usb,update}; do
    qemu-mipsel -cpu 74Kf -L "$tc/mipsel-buildroot-linux-musl/sysroot" \
        /root/lilsmoke-os/build/buildroot/output/target/bin/busybox sh -n "$script"
done
shellcheck -S warning package/lilsmoke-scripts/src/lilsmoke-{storage,usb} scripts/mk-sdcard.sh
echo STORAGE_MIPS_VALIDATION_OK
