#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
B=/root/lilsmoke-os/build/buildroot/output
OUT=$(mktemp -d /root/lilsmoke-menu-tests-XXXXXX)
trap 'rm -rf -- "$OUT"' EXIT HUP INT TERM
CC="$B/host/bin/mipsel-buildroot-linux-musl-gcc"
for test in test_m8_native_frame test_m8_native_video test_m8_damage; do
    includes="-I$B/staging/usr/include/SDL"
    cc -std=gnu99 -O2 -Wall -Wextra -Werror $includes "$P/tools/m8/$test.c" -o "$OUT/host"
    "$OUT/host"
    cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined $includes "$P/tools/m8/$test.c" -o "$OUT/asan"
    "$OUT/asan"
    "$CC" -std=gnu99 -O3 -Wall -Wextra -Werror -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float -static $includes "$P/tools/m8/$test.c" -o "$OUT/mips"
    qemu-mipsel -cpu 74Kf "$OUT/mips"
done
