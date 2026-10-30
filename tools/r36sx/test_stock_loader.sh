#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
[ "$#" = 3 ] || { echo 'usage: test_stock_loader.sh SDK STOCK NEW_OUTPUT' >&2; exit 2; }
SDK=$(realpath "$1"); STOCK=$(realpath "$2"); OUT=$3
HERE=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
[ ! -e "$OUT" ] || exit 2
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
CC=$SDK/host/bin/mipsel-buildroot-linux-musl-gcc
LIB=$STOCK/rootfs/lib
"$CC" -std=c99 -Os -fPIC -G0 -mips32r2 -mfp32 -mhard-float -fno-stack-protector -fno-builtin \
    -Wall -Wextra -Werror -c "$HERE/tools/r36sx/test_stock_loader.c" -o "$OUT/loader.o"
"$CC" -nostdlib -Wl,-e,_start -Wl,--dynamic-linker,/lib/ld.so.1 -Wl,--no-undefined "-Wl,-rpath-link,$LIB" \
    "$OUT/loader.o" "$LIB/libdl.so.2" "$LIB/libc.so.6" -o "$OUT/stock-loader"
"$CC" -std=c99 -Os -fPIC -G0 -mips32r2 -mfp32 -mhard-float -fno-stack-protector -fno-builtin \
    -shared -nostdlib -nostartfiles -nodefaultlibs -Wl,--no-undefined \
    -DR36_BOOTLOG_NO_CONSOLE -DR36_EXEC_SHELL=\"/missing-r36-qa-shell\" -DR36_JOURNAL_PATH=\""$OUT/journal.bin"\" \
    "$HERE/board/r36sx-2.7-stock/bootstrap_exec.c" -o "$OUT/guest.so"
truncate -s 16384 "$OUT/journal.bin"
qemu-mipsel -cpu 74Kf -L "$STOCK/rootfs" "$OUT/stock-loader" "$OUT/guest.so"
python3 - "$OUT/journal.bin" "$HERE" <<'PY'
import json,sys
from pathlib import Path
sys.path.insert(0,str(Path(sys.argv[2])/'tools/r36sx'))
from boot_report import decode_journal
report=decode_journal(Path(sys.argv[1]).read_bytes())
assert [r['event'] for r in report['records']]==[
 'PLUGIN_DLOPEN','PLUGIN_INIT','PLUGIN_LOAD_GAME','PLUGIN_EXEC_SHELL','PLUGIN_EXEC_FAILED'],report
print(json.dumps(report,indent=2))
PY
"$SDK/host/bin/mipsel-buildroot-linux-musl-readelf" -d "$OUT/stock-loader" > "$OUT/loader.elf.txt"
sha256sum "$LIB/libdl.so.2" "$LIB/libc.so.6" "$LIB/ld.so.1" "$OUT/guest.so" > "$OUT/inputs.sha256"
