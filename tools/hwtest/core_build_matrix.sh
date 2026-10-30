#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
out="$root/test-results/emulation-performance/core-builds"
mkdir -p "$out"
failed=0
for core do
    if sh "$root/tools/hwtest/build_pinned_cores.sh" "$core" >"$out/$core.log" 2>&1; then
        echo "CORE_BUILD_OK $core"
    else
        echo "CORE_BUILD_FAILED $core"
        tail -15 "$out/$core.log"
        failed=1
    fi
done
exit "$failed"
