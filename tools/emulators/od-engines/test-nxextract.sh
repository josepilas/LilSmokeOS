#!/usr/bin/env bash
set -euo pipefail
ROOT=${OD_BUILD_ROOT:-/root/lilsmoke-emulator-expansion-20261008}
SYS=$ROOT/engine-sysroot
BIN=$ROOT/native-candidates/bin/nxextract
test -x "$BIN"
QA=$(mktemp -d "$ROOT/engine-build/nxextract-qa.XXXXXX")
exec > >(tee -a "$ROOT/engine-logs/nxextract-qa.log") 2>&1
run() {
    timeout 20 qemu-mipsel -cpu 74Kf -L "$SYS" \
        -E LD_LIBRARY_PATH="$SYS/usr/lib:$SYS/lib" "$BIN" "$@"
}
run --help
for mode in missing truncated invalid missing-data; do
    mkdir -p "$QA/$mode"
    cd "$QA/$mode"
    if [ "$mode" != missing ]; then
        printf MZ > Doukutsu.exe
        if [ "$mode" = invalid ]; then
            printf XX > Doukutsu.exe
        fi
        if [ "$mode" = invalid ] || [ "$mode" = missing-data ]; then
            truncate -s 1472396 Doukutsu.exe
        fi
    fi
    if run > result.txt 2>&1; then
        echo "FAIL extractor accepted $mode fixture" >&2
        exit 1
    else
        test "$?" = 1
    fi
    cat result.txt
    test ! -e data/pxt
    test ! -e data/stage.dat
    echo "PASS candidate extractor rejects $mode input without output assets"
done
echo 'Negative synthetic-input tests only. Real game conversion and paired NX acceptance NOT tested.'
