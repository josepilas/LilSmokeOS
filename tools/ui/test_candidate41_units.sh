#!/bin/sh
set -eu
P=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
S=${QA_SOURCE:-$P/package/lilsmoke-ui/src/src}
B=${SDK_OUTPUT:-/root/lilsmoke-os/build/buildroot/output}
Q=${QA_OUTPUT:-/root/ui-perf-c41/units}
mkdir -p "$Q"
python3 "$P/tools/ui/test_candidate41_perf.py" --source "$S" --output "$Q/host" --units --sanitize
python3 "$P/tools/ui/test_candidate41_perf.py" --source "$S" --output "$Q/mips" --units --mips
