#!/bin/sh
set -eu
if [ "$#" != 2 ]; then echo "usage: $0 SDK_OUTPUT NEW_TEST_OUTPUT" >&2; exit 2; fi
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SDK=$(CDPATH= cd -- "$1" && pwd)
mkdir -- "$2"
OUT=$(CDPATH= cd -- "$2" && pwd)
CC=$SDK/host/bin/mipsel-buildroot-linux-musl-gcc
CORE=$P/board/segam-m8-v6-stock/m8-boot-logo.c
for name in test_boot_logo test_boot_logo_gate; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -DM8_BOOT_CORE_ONLY "$P/tools/m8/$name.c" "$CORE" -ldl -pthread -o "$OUT/$name-host"
    "$OUT/$name-host"
    cc -std=c11 -O1 -g -Wall -Wextra -Werror -DM8_BOOT_CORE_ONLY -fsanitize=address,undefined \
        "$P/tools/m8/$name.c" "$CORE" -ldl -pthread -o "$OUT/$name-sanitized"
    "$OUT/$name-sanitized"
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -DM8_BOOT_CORE_ONLY -mips32r2 -mtune=74kc -mdspr2 -mfp32 -mhard-float \
        -static "$P/tools/m8/$name.c" "$CORE" -ldl -pthread -o "$OUT/$name-mips"
    qemu-mipsel -cpu 74Kf "$OUT/$name-mips"
done
sh -n "$P/board/segam-m8-v6-stock/m8-launch.sh"
sh -n "$P/tools/m8/build_boot_logo.sh"
echo 'PASS launcher/build shell syntax; no SDK target or device writes.'
