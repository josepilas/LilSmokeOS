#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
P=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
B=${LILSMOKE_TERMINAL_TEST_OUT:-/tmp/lilsmoke-terminal-test}
mkdir -p "$B"
if [ ! -f "$B/libvterm-0.3.3.tar.gz" ]; then
    curl -fL --max-time 90 -o "$B/libvterm-0.3.3.tar.gz" \
        https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz
fi
printf '%s  %s\n' 09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0 \
    "$B/libvterm-0.3.3.tar.gz" | sha256sum -c -
if [ ! -d "$B/libvterm-0.3.3" ]; then tar -xzf "$B/libvterm-0.3.3.tar.gz" -C "$B"; fi
make -C "$B/libvterm-0.3.3" -j1 PREFIX="$B/prefix" libvterm.la install-inc install-lib
export PKG_CONFIG_PATH="$B/prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$B/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S "$P/package/lilsmoke-terminal/src" -B "$B/host" \
    -DCMAKE_BUILD_TYPE=Debug -DLILSMOKE_TERMINAL_TESTS=ON
cmake --build "$B/host" --parallel 1
ctest --test-dir "$B/host" --output-on-failure
"$B/host/lilsmoke-terminal" --check-pty
sh -n "$P/package/lilsmoke-terminal/src/lilsmoke-terminal-launch"
