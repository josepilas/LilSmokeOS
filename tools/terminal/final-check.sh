#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
P=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
B=${LILSMOKE_TERMINAL_TEST_OUT:-/tmp/lilsmoke-terminal-test}
OUT=${LILSMOKE_TERMINAL_REPORT_OUT:-$B/report}
mkdir -p "$OUT"
export LD_LIBRARY_PATH="$B/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
ctest --test-dir "$B/host" --output-on-failure
cp "$B/host/Testing/Temporary/LastTest.log" "$OUT/host-tests.log"
ctest --test-dir "$B/asan" --output-on-failure
cp "$B/asan/Testing/Temporary/LastTest.log" "$OUT/asan-tests.log"
LILSMOKE_TERMINAL_TEST_SHELL="$P/tools/terminal/mips-shell-fixture.sh" \
    "$B/host/terminal-tests" >"$OUT/mips-busybox-pty.log" 2>&1
cat "$OUT/mips-busybox-pty.log"
sh "$P/tools/terminal/wrapper_test.sh" >"$OUT/wrapper.log" 2>&1
cat "$OUT/wrapper.log"
SDK=${SDK:-/root/lilsmoke-os/build/buildroot/output}
qemu-mipsel -cpu 74Kf -L "$SDK/staging" "$B/mips/lilsmoke-terminal" --check-pty >"$OUT/mips-probe.log" 2>&1
cat "$OUT/mips-probe.log"
shellcheck "$P/tools/terminal/"*.sh "$P/package/lilsmoke-terminal/src/lilsmoke-terminal-launch"
echo 'PASS shellcheck for terminal launch and test scripts'
