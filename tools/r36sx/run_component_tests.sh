#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
[ "$#" = 2 ] || { echo 'usage: run_component_tests.sh BUILDROOT_OUTPUT NEW_OUTPUT' >&2; exit 2; }
SDK=$(realpath "$1")
OUT=$2
HERE=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
[ ! -e "$OUT" ] || { echo 'output must be new' >&2; exit 2; }
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
CC=$SDK/host/bin/mipsel-buildroot-linux-musl-gcc
READELF=$SDK/host/bin/mipsel-buildroot-linux-musl-readelf
for test in bootstrap input display audio; do
    gcc -std=c11 -O2 -Wall -Wextra -Werror -DOD_INPUT_WIRE_QA -I"$SDK/staging/usr/include/SDL" \
        "$HERE/tools/r36sx/test_$test.c" -ldl -o "$OUT/host-$test"
    "$OUT/host-$test"
done
gcc -std=c99 -Os -Wall -Wextra -Werror "$HERE/board/r36sx-2.7-stock/r36-bootlog.c" -o "$OUT/host-bootlog"
"$CC" -std=c99 -Os -static -Wall -Wextra -Werror -DR36_BOOTLOG_NO_CONSOLE "$HERE/board/r36sx-2.7-stock/r36-bootlog.c" -o "$OUT/mips-bootlog"
R36_BOOTLOG_COMMAND="$OUT/host-bootlog" python3 "$HERE/tools/r36sx/test_bootlog.py"
R36_BOOTLOG_COMMAND="qemu-mipsel -cpu 74Kf $OUT/mips-bootlog" python3 "$HERE/tools/r36sx/test_bootlog.py"
R36_BOOTLOG_NATIVE_BIN="$OUT/host-bootlog" python3 "$HERE/tools/r36sx/test_launch_paths.py"
for test in input display audio; do
    "$CC" -std=c11 -O2 -static -Wall -Wextra -Werror -I"$SDK/staging/usr/include/SDL" \
        "$HERE/tools/r36sx/test_$test.c" -ldl -o "$OUT/mips-$test"
    qemu-mipsel -cpu 74Kf "$OUT/mips-$test"
done
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$HERE/tools/r36sx/test_plugin_guest.c" -ldl -o "$OUT/mips-plugin-loader"
for mode in fail success; do
    if [ "$mode" = fail ]; then shell=/r36-test-missing-shell; else shell=/bin/sh; fi
    "$CC" -std=c99 -O2 -fPIC -G0 -fno-stack-protector -fno-builtin -mips32r2 -mfp32 -mhard-float \
        -shared -nostdlib -nostartfiles -nodefaultlibs -Wl,--no-undefined \
        -DR36_BOOTLOG_NO_CONSOLE -DR36_EXEC_SHELL=\""$shell"\" -DR36_EXEC_LAUNCHER=\""$HERE/tools/r36sx/test_exec_target.sh"\" \
        "$HERE/board/r36sx-2.7-stock/bootstrap_exec.c" -o "$OUT/guest-$mode.so"
    "$READELF" -d "$OUT/guest-$mode.so" > "$OUT/guest-$mode.elf.txt"
    if grep -q '(NEEDED)' "$OUT/guest-$mode.elf.txt"; then echo 'unexpected runtime dependency' >&2; exit 1; fi
    set +e
    qemu-mipsel -cpu 74Kf -L "$SDK/target" "$OUT/mips-plugin-loader" "$OUT/guest-$mode.so"
    result=$?
    set -e
    if [ "$mode" = fail ]; then expected=0; else expected=37; fi
    [ "$result" = "$expected" ] || { echo "MIPS $mode result=$result expected=$expected" >&2; exit 1; }
done
sh -n "$HERE/board/r36sx-2.7-stock/r36-launch.sh"
sh -n "$HERE/board/r36sx-2.7-stock/r36-entry.sh"
sh -n "$HERE/tools/r36sx/build_components.sh"
echo 'PASS component tests; only tiny qemu-user processes, no card, display, audio or HID hardware accessed'
