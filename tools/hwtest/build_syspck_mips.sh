#!/bin/bash
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
root=$(cd "$(dirname "$0")/../.." && pwd)
br=/root/lilsmoke-os/build/buildroot
out="$root/test-results/syspck"
mkdir -p "$out"
make -C "$br" O="$br/output" BR2_EXTERNAL="$root" olddefconfig
make -C "$br" O="$br/output" BR2_EXTERNAL="$root" -j2 lilsmoke-scripts-rebuild lilsmoke-syspck-rebuild
cp "$br/output/target/usr/bin/lilsmoke-syspck" "$out/lilsmoke-syspck-mips"
"$br/output/host/bin/mipsel-buildroot-linux-musl-strip" "$out/lilsmoke-syspck-mips"
"$br/output/host/bin/mipsel-buildroot-linux-musl-readelf" -h "$out/lilsmoke-syspck-mips"
echo SYSPCK_MIPS_BUILD_OK
