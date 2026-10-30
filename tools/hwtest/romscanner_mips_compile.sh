#!/bin/sh
set -eu
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
compiler=${1:-/root/lilsmoke-os/build/buildroot/output/host/bin/mipsel-linux-g++}
build=$(mktemp -d /tmp/lilsmoke-scanner-mips-XXXXXX)
trap 'rm -f "$build/ROMScanner.o"; rmdir "$build"' EXIT HUP INT TERM
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
    -march=mips32r2 -mabi=32 -mtune=74kc -mdspr2 -mfp32 -mhard-float -mplt -mlong-calls \
    -I"$repo/package/lilsmoke-ui/src/src" -c "$repo/package/lilsmoke-ui/src/src/ROMScanner.cpp" \
    -o "$build/ROMScanner.o"
printf '%s\n' ROMSCANNER_MIPS_COMPILE_OK
